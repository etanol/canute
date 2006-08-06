/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                          PROTOCOL IMPLEMENTATION                           */
/*                                                                            */
/******************************************************************************/

#include "canute.h"

#define REQUEST_FILE     1
#define REQUEST_BEGINDIR 2
#define REQUEST_ENDDIR   3
#define REQUEST_END      4

#define REPLY_ACCEPT     5
#define REPLY_SKIP       6

/* True if str is NOT "." or ".." */
#define NOT_SELF_OR_PARENT(str) \
        (str[0] != '.' \
         || (str[1] != '\0' && (str[1] != '.' || str[2] != '\0')))


/******************************  PRIVATE TYPES  ******************************/

/*
 * Long number (64 bit). Access as a whole or splitted in two 32 bit numbers to
 * handle network endianness. Two unions are used to solve endianness issues.
 */
union longnum_host {
#if defined(HASEFROCH) || __BYTE_ORDERING == __LITTLE_ENDIAN
        struct {
                int32_t lo;
                int32_t hi;
        } half;
#else
        struct {
                int32_t hi;
                int32_t lo;
        } half;
#endif
        int64_t full;
};

union longnum_net {
        struct {
                int32_t hi;
                int32_t lo;
        } half;
        int64_t full;
};


/*
 * Transmission header packet. This defines what kind of item is going to be
 * transferred to the receiver.
 */
struct header {
        int               type;
        int               pad;
        union longnum_net size;
        char              name[CANUTE_NAME_LENGTH + 1];
};


/*****************************  PRIVATE BUFFERS  ******************************/

static char databuf[CANUTE_BLOCK_SIZE];
static char namebuf[CANUTE_NAME_LENGTH];


/****************************  PRIVATE FUNCTIONS  ****************************/

/*
 * send_message
 *
 * Build a header packet and send it through the connection. All the fields are
 * converted to network byte order if required.
 */
static void
send_message (SOCKET sk, int type, int64_t size, char *name)
{
        union longnum_host sz;
        struct header      packet;

        packet.type = htonl(type);

        if (size != 0) {
                sz.full = size;
                packet.size.half.hi = htonl(sz.half.hi);
                packet.size.half.lo = htonl(sz.half.lo);
        } else {
                packet.size.full = 0;
        }

        if (name != NULL) {
                strncpy(packet.name, name, CANUTE_NAME_LENGTH);
                packet.name[CANUTE_NAME_LENGTH] = '\0';
        } else {
                packet.name[0] = '\0';
        }

        send_data(sk, (char *) &packet, sizeof(struct header));
}


/*
 * receive_message
 *
 * Read from the connection expecting a header packet. Fix byte ordering if
 * necessary, fill the fields (if address was provided by the caller) and return
 * the message type.
 */
int
receive_message (SOCKET sk, int64_t *size, char *name)
{
        int                type;
        union longnum_host sz;
        struct header      packet;

        receive_data(sk, (char *) &packet, sizeof(struct header));

        if (size != NULL) {
                sz.half.hi = ntohl(packet.size.half.hi);
                sz.half.lo = ntohl(packet.size.half.lo);
                *size      = sz.full;
        }

        if (name != NULL) {
                strncpy(name, packet.name, CANUTE_NAME_LENGTH);
                name[CANUTE_NAME_LENGTH] = '\0';
        }

        type = ntohl(packet.type);
        return type;
}

/*
 * receive_file
 *
 * Receive file contents. Only the receiver uses this function. It is called
 * just after receiving a header requesting a file to be transferred. This
 * function is the counterpart to send_file() but it is not public because
 * there's no need for that; so the protocol can remain hidden within this
 * module.
 *
 * As the resume is automatic, every file request expects a reply indicating the
 * initial offset from which to transfer. A zero offset means the whole file
 * content must be transferred. If the local file is greather than or equal to
 * the remote one, then the sender is informed to skip it.
 *
 * Feedback for the file is managed here.
 *
 * TODO: Skip policy could be refined.
 */
static void
receive_file (SOCKET sk, char *filename, int64_t size)
{
        FILE    *file;
        int64_t  received_bytes; /* Think about it also as "offset" */
        size_t   b;

        received_bytes = file_size(filename); /* Local file */

        if (received_bytes < size) {
                send_message(sk, REPLY_ACCEPT, received_bytes, NULL);

                file = fopen(filename, "ab");
                if (file == NULL)
                        fatal("Creating or appending to file '%s'", filename);

                setup_progress(filename, size, received_bytes);

                while (received_bytes < size) {
                        if (size - received_bytes > CANUTE_BLOCK_SIZE)
                                b = CANUTE_BLOCK_SIZE;
                        else
                                b = (size_t) (size - received_bytes);

                        receive_data(sk, databuf, b);
                        fwrite(databuf, 1, b, file);
                        show_progress(b);
                        received_bytes += b;
                }

                finish_progress();
                fflush(file);
                fclose(file);

        } else {
                printf("Skipping file '%s'\n", filename);
                send_message(sk, REPLY_SKIP, 0, NULL);
        }
}


/*****************************  PUBLIC FUNCTIONS  *****************************/

/*
 * try_send_dir
 *
 * If dirname is, indeed, a directory then it's sent recursively over the
 * connection and a true value is returned (because the try was successful).
 * Otherwise false is returned and nothing else happens.
 *
 * It comes pretty handy as a portable and compact filetype tester (between
 * directory and the rest of objects, usually files). Try to send the item as a
 * directory, if it fails then probably is not a directory.
 */
int
try_send_dir (SOCKET sk, char *dirname)
{
        DIR           *dir;
        struct dirent *dentry;

        dir = opendir(dirname);
        if (dir == NULL)
                return 0;

        send_message(sk, REQUEST_BEGINDIR, 0, basename(dirname));
        change_wd(dirname);

        dentry = readdir(dir);
        while (dentry != NULL) {
                if (NOT_SELF_OR_PARENT(dentry->d_name) 
                    && !try_send_dir(sk, dentry->d_name))
                        send_file(sk, dentry->d_name);
                dentry = readdir(dir);
        }

        send_message(sk, REQUEST_ENDDIR, 0, NULL);
        change_wd("..");

        closedir(dir);
        return 1;
}


/*
 * send_file
 *
 * Send a file request and wait for an answer from the receiver. If we're told
 * to skip the file then proceed and return. In other case the file is accepted
 * with a given inital offset (resume, if offset > 0).
 *
 * Feedback for the file is managed here.
 *
 * NOTE: Now a fail in the fopen() call becomes a fatal error because the
 * receiver has accepted the file and is waiting for it. Before, this used to be
 * a non-fatal error and fopen() errors were just skipped.
 */
void
send_file (SOCKET sk, char *filename)
{
        int      e, reply;
        int64_t  sent_bytes = 0, size, offset;
        size_t   b;
        FILE    *file;

        size = file_size(filename);
        if (size == 0)
                return; /* Empty files are not interesting */

        send_message(sk, REQUEST_FILE, size, basename(filename));
        reply = receive_message(sk, &offset, NULL);
        if (reply == REPLY_SKIP) {
                printf("Skipping file '%s'\n", basename(filename));
                return;
        }

        file = fopen(filename, "rb");
        if (file == NULL)
                fatal("Opening file '%s'", basename(filename));

        if (offset > 0) {
                e = fseeko(file, offset, SEEK_SET);
                if (e == -1)
                        fatal("Seeking file '%s'", filename);
                sent_bytes = offset;
        }

        setup_progress(basename(filename), size, offset);

        while (sent_bytes < size) {
                b = fread(databuf, 1, CANUTE_BLOCK_SIZE, file);
                send_data(sk, databuf, b);
                show_progress(b);
                sent_bytes += b;
        }

        finish_progress();
        fclose(file);
}


/*
 * receive_item
 *
 * Read a sender request and proceed as needed. Return false if sender requested
 * the end of the session (no more items to come) and true otherwise (still more
 * stuff to transfer).
 */
int
receive_item (SOCKET sk)
{
        int      request;
        int64_t  size;

        request = receive_message(sk, &size, namebuf);

        switch (request) {
        case REQUEST_FILE:
                receive_file(sk, namebuf, size);
                break;

        case REQUEST_BEGINDIR:
                try_mkdir(namebuf);
                change_wd(namebuf);
                break;

        case REQUEST_ENDDIR:
                change_wd("..");
                break;

        case REQUEST_END:
                return 0;

        default:
                fatal("Unexpected header type (%d)", request);
        }

        return 1;
}


/*
 * end_session
 *
 * Send an END request. Without this function the protocol cannot be hidden
 * because the sender needs to notify the receiver the end of the data stream.
 */
void
end_session (SOCKET sk)
{
        send_message(sk, REQUEST_END, 0, NULL);
}

