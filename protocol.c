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

/*
 * EXPLANATION
 *
 * The protocol used (if could be labeled as "protocol") is very simple. There
 * is a header packet (struct header) which contains the packet or message type,
 * the size in a custom representation and the name of the item. A header is
 * always sent to make the receiver host know the size of the contents when
 * required. Even though the same data structure is used for all messages, the
 * sender's messages are requests and the receiver's are replies.
 *
 * There are four types of requests (sender) and two types of replies
 * (receiver); the requests identify the kind of item that has to be negotiated.
 * In some cases the receiver must answer the sender requests. Those messages
 * are identified by a self-explanatory constant.
 *
 * When a file is about to be transferred, the sender uses a REQUEST_FILE. The
 * receiver must reply this message with a REPLY_ACCEPT if it wants the file,
 * indicating in the same message the initial offset of the file (resume
 * support). In case the receiver doesn't want or need the file, it must answer
 * with a REPLY_SKIP.
 *
 * If the sender needs to send a directory, a REQUEST_BEGINDIR with the name of
 * the directory has to be sent. The receiver must accept or skip the directory
 * (directory skipping normally happens on error situations). When a directory
 * has been accepted is walked through recursively sending each directory entry
 * with the corresponding header. In order to make directory recursion easier,
 * after a directory has been accepted, both peers should move into it.
 *
 * After the directory has been completely read, a REQUEST_ENDDIR must be sent
 * and both peers should return to the parent directory. This "end of directory"
 * request must NOT be answered and any error occured when moving to parent
 * directory is considered fatal.
 *
 * Finally, when no more items are left, a REQUEST_END is sent to notify the
 * receiver about the situation. Then both hosts close the connection and
 * exit.
 *
 *
 * ABOUT FILE SIZES
 *
 * When large file support (LFS) came into scene, some issues arised. The most
 * important was the handling of 64 bit numbers in 32 bit machines. Internal
 * details about representation and handling (parameter passing, etc) were not
 * clear and could obviously vary from platform to platform.
 *
 * Initial tests failed because middle endianness in 64 bit numbers was
 * discarded. And there isn't too much documentation about it. Having a beach
 * bath with MKD (see README), he gave me the idea of using a multiplier as the
 * transfer was block based. With this approach, up to 47 bits (without sign)
 * can be used to represent a size, which can represent 128 TeraBytes of
 * storage.
 *
 * This way we can transfer large sizes without worrying about local byte order
 * (ntohl and htonl are transparent) and 64 bit number representation.
 */
#include "canute.h"

static char databuf[CANUTE_BLOCK_SIZE];


/****************************  PRIVATE FUNCTIONS  ****************************/

/*
 * receive_file
 *
 * A file request has been received from the network. We must reply depending on
 * the local state of the file requested.
 *
 * TODO: Skip policy could be refined.
 */
static void receive_file (SOCKET sk, char *name, long long size)
{
        int              e;
        FILE            *file;
        long long        received_bytes; /* Think about it also as "offset" */
        size_t           b;
        struct stat_info st;

        e = stat(name, &st);
        if (e == -1) {
                /* Most probable: errno == ENOENT */
                received_bytes = 0;
        } else if (st.st_size >= size) {
                printf("--- Skipping file '%s'\n", name);
                send_message(sk, REPLY_SKIP, 0, NULL);
                return;
        } else {
                received_bytes = (long long)st.st_size;
        }

        file = fopen(name, "ab");
        if (file == NULL) {
                error("Cannot open file '%s'", name);
                send_message(sk, REPLY_SKIP, 0, NULL);
                return;
        }

        send_message(sk, REPLY_ACCEPT, received_bytes, NULL);
        setup_progress(name, size, received_bytes);

        while (received_bytes < size) {
                if (size - received_bytes > CANUTE_BLOCK_SIZE)
                        b = CANUTE_BLOCK_SIZE;
                else
                        b = (size_t)(size - received_bytes);

                receive_data(sk, databuf, b);
                fwrite(databuf, 1, b, file);
                update_progress(b);
                received_bytes += b;
        }

        finish_progress();
        fflush(file);
        fclose(file);
}


/*
 * send_file
 *
 * Treat the item as a file and try to send it.
 */
static void
send_file (SOCKET sk, char *name, long long size)
{
        int       e, reply;
        long long sent_bytes; /* Size reported remotely */
        size_t    b;
        char     *bname;
        FILE     *file;

        bname = basename(name);
        file  = fopen(name, "rb");
        if (file == NULL) {
                error("Cannot open file '%s'", bname);
                return;
        }

        send_message(sk, REQUEST_FILE, size, bname);
        reply = receive_message(sk, &sent_bytes, NULL);
        if (reply == REPLY_SKIP) {
                fclose(file);
                printf("--- Skipping file '%s'\n", bname);
                return;
        }

        if (sent_bytes > 0) {
                e = fseeko(file, (off_t)sent_bytes, SEEK_SET);
                if (e == -1)
                        fatal("Could not seek file '%s'", bname);
        }

        setup_progress (bname, size, sent_bytes);

        while (sent_bytes < size) {
                b = fread(databuf, 1, CANUTE_BLOCK_SIZE, file);
                send_data(sk, databuf, b);
                update_progress(b);
                sent_bytes += b;
        }

        finish_progress();
        fclose(file);
}


/*****************************  PUBLIC FUNCTIONS  *****************************/

/*
 * send_item
 *
 * Discover what kind of filesystem item 'name' represents and send it over the
 * connection.
 */
void send_item (SOCKET sk, char *name)
{
        int              e, reply;
        char            *bname;
        DIR             *dir;
        struct dirent   *dentry;
        struct stat_info st;

        e = stat(name, &st);
        if (e == -1) {
                error("Cannot stat item '%s'", basename(name));
                return;
        }

        if (S_ISDIR(st.st_mode)) {
                bname = basename(name);
                dir   = opendir(name);
                if (dir == NULL) {
                        error("Cannot open dir '%s'", bname);
                        return;
                }

                e = chdir(name);
                if (e == -1) {
                        closedir(dir);
                        error("Cannot change to dir '%s'", bname);
                        return;
                }

                send_message(sk, REQUEST_BEGINDIR, 0, bname);
                reply = receive_message(sk, NULL, NULL);
                if (reply == REPLY_SKIP) {
                        closedir(dir);
                        printf("--- Skipping directory '%s'\n", bname);
                        e = chdir("..");
                        if (e == -1)
                                fatal("Could not change to parent directory");
                        return;
                }

                printf(">>> Entering directory '%s'\n", bname);
                dentry = readdir(dir);
                while (dentry != NULL) {
                        if (NOT_SELF_OR_PARENT(dentry->d_name))
                                send_item(sk, dentry->d_name);
                        dentry = readdir(dir);
                }

                closedir(dir);
                e = chdir("..");
                if (e == -1)
                        fatal("Could not change to parent directory");
                send_message(sk, REQUEST_ENDDIR, 0, NULL);
        } else {
                send_file(sk, name, st.st_size);
        }
}


/*
 * receive_item
 *
 * Parse a header packet containing a request and act accordingly. Return true
 * if the read packet is the last one for the session (no more items to come)
 * and false otherwise (still more stuff pending).
 */
int
receive_item (SOCKET sk)
{
        static char namebuf[CANUTE_NAME_LENGTH];
        int         e, request;
        long long   size;

        request = receive_message(sk, &size, namebuf);

        switch (request) {
        case REQUEST_FILE:
                receive_file(sk, namebuf, size);
                break;

        case REQUEST_BEGINDIR:
                mkdir(namebuf);
                e = chdir(namebuf);
                if (e == -1) {
                        error("Cannot change to dir '%s'", namebuf);
                        send_message(sk, REPLY_SKIP, 0, NULL);
                } else {
                        printf(">>> Entering directory '%s'\n",  namebuf);
                        send_message(sk, REPLY_ACCEPT, 0, NULL);
                }
                break;

        case REQUEST_ENDDIR:
                e = chdir("..");
                if (e == -1)
                        fatal("Could not change to parent directory");
                break;

        case REQUEST_END:
                return 1;

        default:
                fatal("Unexpected header type (%d)", request);
        }

        return 0;
}

