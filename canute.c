/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                      Original idea      : C2H5OH                           */
/*                      Feedback patch     : MKD                              */
/*                      Win32 contributions: Plimo San                        */
/*                      Testing            : Tito Houzy                       */
/*                                                                            */
/* Pseudoprotocol and application for transferring files and directories over */
/* TCP/IP. No need to authenticate, root privileges, cypher or any other      */
/* stuff; just data sending. If you're too lazy to set up a FTP server to     */
/* batch copy lots of files when you're connected on LAN with your friends,   */
/* this could be an interesting alternative.                                  */
/*                                                                            */
/* A single source file, compile and go. The same binary for both peers.      */
/******************************************************************************/

/* $Id */

/* Constants */
#define CANUTE_DEFAULT_PORT 1121
#define CANUTE_BLOCK_SIZE   65536
#define CANUTE_NAME_LENGTH  247 /* This is measured so header_t is 256 bytes */
#define C_ITEM_END  0
#define C_ITEM_FILE 1
#define C_ITEM_DIR  2

/* Common headers */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/timeb.h>

#if defined(__WIN32__) || defined(WIN32)
/* Definitions and headers (Hasefroch) */
#include <winsock.h>
#define  CCP_CAST (const char *)
#define  CloseSocket(sk) closesocket( sk )
#define  HASEFROCH
typedef int socklen_t;
#else
/* Definitions and headers (UNIX) */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define  INVALID_SOCKET -1
#define  SOCKET_ERROR   -1
#define  CCP_CAST  /* Cast to (const char *) not needed in UNIX */
#define  SOCKADDR struct sockaddr
#define  closesocket(sk) close(sk)
typedef int SOCKET;
#endif /* __WIN32__ */

/*
 * Transmission header. This defines what kind of item is going to be
 * transferred or instructions to the receiver.
 */
typedef struct s_Header {
    unsigned char type; /* We're lazy to switch endianness */
    unsigned char pad[3];
    unsigned int  size; /* 4GByte limit, fopen() wouldn't handle it anyway */
    char          name[CANUTE_NAME_LENGTH + 1];
} header_t;

/*
 * Progress information. Only used by feedback functions.
 */
typedef struct s_Progress {
    unsigned long total_bytes;
    unsigned long completed_bytes;
    unsigned long last_transfer;
    int           term_width;
    float         last_delta;
    struct timeb  last_timestamp;
} progress_t;

/*
 * Send/receive buffer. Global may be uglier but is a lot handier.
 */
static char buffer[CANUTE_BLOCK_SIZE];

/* 
 * Auxiliary functions (prototypes)
 */
void   help            (char *);
void   error           (char *, ...);
void   fatal           (char *, ...);
char  *basename        (char *);
SOCKET open_connection (char *, uint16_t);
void   send_data       (SOCKET, char *, size_t);
void   send_file       (SOCKET, char *);
int    send_dir        (SOCKET, char *);
void   receive_data    (SOCKET, char *, size_t);
void   change_wd       (char *);

/*
 * Output feedback functions (prototypes)
 */
void setup_progress (progress_t *, char *, unsigned long);
void show_progress  (progress_t *, unsigned long);


/*****************************  MAIN FUNCTION  *****************************/

/*
 * Four concepts are important here: server, client, sender and receiver. For
 * the sake of flexiblity wether the sender and receiver can be server or
 * client.
 *
 * Server listens for connections and client needs to know the server host.
 * Sender manages the transmission, needs to know what items to send, and
 * receiver just obeys what sender says.
 *
 * Almost on any error the program aborts.
 */
int main (int argc, char **argv)
{
    SOCKET sk = -1; /* Quest for a warning free compilation */ 
    header_t header;
    char    *port_str;
    uint16_t port;

    if (argc < 2) 
        help (argv[0]);

    /* See if there is a port specification to override default */
    port     = CANUTE_DEFAULT_PORT;
    port_str = strchr (argv[1], ':');
    if (port_str != NULL) {
        *port_str++ = '\0';
        port = atoi (port_str);
    }

    if (strncmp (argv[1], "send", 4) == 0) {

        /*********************/
        /***  SENDER MODE  ***/
        /*********************/
        int i, arg = -1;

        /* Open connection */
        if (strcmp (argv[1], "send") == 0) {
            /* As server */
            if (argc < 3)
                help (argv[0]);
            sk  = open_connection (NULL, port);
            arg = 2;
        } else if (strcmp (argv[1], "sendto") == 0) {
            /* As client */
            if (argc < 4)
                help (argv[0]);
            sk  = open_connection (argv[2], port);
            arg = 3;
        }

        /* Adjust send buffer */
        i = CANUTE_BLOCK_SIZE;
        setsockopt (sk, SOL_SOCKET, SO_SNDBUF, CCP_CAST &i, sizeof (i));

        /* Now we have the transmission channel open, so let's send everything
         * we're supposed to send. This isn't the most elegant way to do it but
         * is compact and keeps portability (deciding wether to send a file or a
         * directory, also used in send_dir()) */
        for (i = arg; i < argc; i++)
            if (!send_dir (sk, argv[i]))
                send_file (sk, argv[i]);

        /* It's over. Notify the receiver to finish as well, please */
        header.type = C_ITEM_END;
        send_data (sk, (char *) &header, sizeof (header));

    } else if (strncmp (argv[1], "get", 3) == 0) {

        /***********************/
        /***  RECEIVER MODE  ***/
        /***********************/
        unsigned long received_bytes, total_bytes, b;
        FILE *file;
        progress_t pr_info;

        /* Open connection */
        if (strcmp (argv[1], "get") == 0) {
            /* As client */
            if (argc < 3)
                help (argv[0]);
            sk = open_connection (argv[2], port);
        } else if (strcmp (argv[1], "getserv") == 0) {
            /* As server */
            sk = open_connection (NULL, port);
        }

        /* Adjust receive buffer */
        b = CANUTE_BLOCK_SIZE;
        setsockopt (sk, SOL_SOCKET, SO_RCVBUF, CCP_CAST &b, sizeof (b));

        do {
            /* Header tells us how to proceed */
            receive_data (sk, (char *) &header, sizeof (header));

            if (header.type == C_ITEM_END)
                /* End of transmission, leave */
                break;

            if (header.type == C_ITEM_DIR) {
                /* Directory, recurse */
                change_wd (header.name);
                continue;
            }

            /* In other case, it's a file */
            file = fopen (header.name, "wb");
            if (file == NULL)
                fatal ("Could not create '%s'", header.name);

            received_bytes = 0;
            total_bytes    = ntohl (header.size);
            setup_progress (&pr_info, header.name, total_bytes);

            while (received_bytes < total_bytes) {
                b = total_bytes - received_bytes;
                if (b > CANUTE_BLOCK_SIZE)
                    b = CANUTE_BLOCK_SIZE;

                receive_data (sk, buffer, b);
                fwrite (buffer, 1, b, file);

                received_bytes += b;
                show_progress (&pr_info, received_bytes);
            }

            fflush (file);
            fclose (file);
        } while (1);

    } else {
        help (argv[0]);
    }

    closesocket (sk);
#ifdef HASEFROCH
    printf ("\r\n");
#endif
    return EXIT_SUCCESS;
}


/**************************  AUXILIARY FUNCTIONS  **************************/


/*
 * help - Show command syntax and exit not successfully.
 */
void help (char *argv0)
{
    printf ("Syntax:\n" );
    printf ("\t%s send[:port]   <file/directory> [<file/directory> ...]\n",
            argv0);
    printf ("\t%s get[:port]    <host/IP>\n", argv0);
    printf ("\t%s sendto[:port] <host/IP> <file/directory> [<file/directory> ...]\n",
            argv0);
    printf ("\t%s getserv[:port]\n", argv0);
    exit (EXIT_FAILURE);
}


/*
 * error - Error message a la printf(). Custom message + system error string.
 */
void error (char *msg, ...)
{
    va_list pars;
    char s[128];

    fputs ("ERROR: ", stderr);
    va_start (pars, msg);
    vsnprintf (s, 128, msg, pars);
    va_end (pars);
    perror (s);
}


/*
 * Fatal - Fatal error. Same as error() but also exit failing.
 */
void fatal (char *msg, ...)
{
    va_list pars;
    char s[128];

    fputs ("FATAL ERROR: ", stderr);
    va_start (pars, msg);
    vsnprintf (s, 128, msg, pars);
    va_end (pars);
    perror (s);
    exit (EXIT_FAILURE);
}


/*
 * open_connection - Set up a connection whit the remote host in a portable
 *                   manner. If host is NULL then act as a server, otherwise
 *                   connect as a client to 'host'. On error abort so when this
 *                   function returns it does it always successfully.
 */
SOCKET open_connection (char *host, uint16_t port)
{
    SOCKET sk, sk2;
    struct sockaddr_in saddr;
    struct hostent *he;
    int e;
#ifdef HASEFROCH 
    WSADATA ws;

    /* This is safe here because open_connection() is only called once */
    if (WSAStartup (MAKEWORD (1, 1), &ws)) 
        fatal ("Starting WinSock");
    atexit ((void (*)()) WSACleanup);
#endif

    sk = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sk == INVALID_SOCKET) 
        fatal ("Creating socket");

    saddr.sin_family = AF_INET;
    saddr.sin_port   = htons (port);

    if (host == NULL) {
        /*********************/
        /***  SERVER MODE  ***/
        /*********************/
        saddr.sin_addr.s_addr = INADDR_ANY;

        /* Ignore errors from setsockopt(), bind() will fail in that case */
        e = 1;
        setsockopt (sk, SOL_SOCKET, SO_REUSEADDR, CCP_CAST &e, sizeof (e));
        e = bind (sk, (SOCKADDR *) &saddr, sizeof (saddr));
        if (e == SOCKET_ERROR)
            fatal ("Opening port %d", port);

        e = listen (sk, 1);
        if (e == SOCKET_ERROR)
            fatal ("Listening port %d", port);

        e   = sizeof (saddr);
        sk2 = accept (sk, (SOCKADDR *) &saddr, (socklen_t *) &e);
        if (sk2 == INVALID_SOCKET)
            fatal ("Accepting client connection");

        /* Close unneeded sockets */
        closesocket (sk);
        sk = sk2;

    } else {

        /*********************/
        /***  CLIENT MODE  ***/
        /*********************/
        saddr.sin_addr.s_addr = inet_addr (host);

        if( saddr.sin_addr.s_addr == INADDR_NONE ) {
            /* Invalid IP, maybe we have to do a DNS query */
            he = gethostbyname (host);
            if (he == NULL) {
#ifdef HASEFROCH
                fatal ("Invalid IP or hostname");
#else
                herror ("FATAL ERROR: Invalid IP or hostname");
                exit (EXIT_FAILURE);
#endif
            }
            saddr.sin_addr.s_addr = ((struct in_addr *) he->h_addr)->s_addr;
        }

        /* Now we have a destination host */
        e = connect (sk, (SOCKADDR *) &saddr, sizeof (saddr));
        if (e == SOCKET_ERROR) 
            fatal ("Connecting to host '%s'", host);
    }
    /* Connection is ready */
    return sk; 
}


/*
 * send_data - Send count bytes over the connection. Doesn't finish until all of
 *             them are sent. On error aborts.
 */
void send_data (SOCKET sk, char *data, size_t count)
{
    int s; /* Sent bytes in one send() call */

    while (count > 0) {
        s = send (sk, data, count, 0);
        if (s == SOCKET_ERROR) 
            fatal ("Sending data");
        count -= s;
        data  += s;
    }
}


/*
 * receive_data - Receive count bytes from the connection. Doesn't finish until
 *                all bytes are received. On error aborts.
 */
void receive_data (SOCKET sk, char *data, size_t count)
{
    int r; /* Received bytes in one recv() call */

    while (count > 0) {
        r = recv (sk, data, count, 0);
        if (r == SOCKET_ERROR) 
            fatal ("Receiving data");
        count -= r;
        data  += r;
    }
}


/*
 * send_file - Send a whole file. If there's an error before sending the file
 *             content, the file is skipped. On transmission error aborts.
 */
void send_file (SOCKET sk, char *filename)
{
    unsigned long total_bytes, sent_bytes, b;
    int e;
    FILE *file;
    header_t header;
    progress_t pr_info;

    file = fopen (filename, "rb");
    if (file == NULL) {
        error ("Opening file '%s'", filename);
        return;
    }

    /* Get the file size (cross-platform) */
    e = fseek (file, 0L, SEEK_END);
    if (e == -1) {
        error ("Checking file size of '%s'", filename);
        return;
    }
    total_bytes = ftell (file);
    rewind (file);

    /* Send a header with relevant file info */
    strncpy (header.name, basename (filename), CANUTE_NAME_LENGTH);
    header.type = C_ITEM_FILE;
    header.name[CANUTE_NAME_LENGTH] = '\0';
    header.size = htonl (total_bytes);
    send_data (sk, (char *) &header, sizeof (header));

    /* Send the contents */
    sent_bytes = 0;
    setup_progress (&pr_info, filename, total_bytes);

    while (sent_bytes < total_bytes) {
        b = fread (buffer, 1, CANUTE_BLOCK_SIZE, file);
        send_data (sk, buffer, b);
        sent_bytes += b;
        show_progress (&pr_info, sent_bytes);
    }

    fclose (file);
}


/*
 * send_dir - Send dirname files and subdirectories recursively. To make code
 *            more compact and portable we rely on the behaviour of opendir()
 *            which fails when trying to open a file. Returns false if dirname
 *            is a file and true if directory was completely sent.
 */
int send_dir (SOCKET sk, char *dirname)
{
    DIR *dir;
    struct dirent *dentry;
    header_t header;

    dir = opendir (dirname);
    if( dir == NULL ) 
        return 0; 

    /* Tell de receiver to create and change to "dir" */
    header.type = C_ITEM_DIR;
    header.size = 0;
    strncpy (header.name, basename (dirname), CANUTE_NAME_LENGTH);
    header.name[CANUTE_NAME_LENGTH] = '\0';
    send_data (sk, (char *) &header, sizeof (header));

    /* We also move into the directory */
    change_wd (dirname);

    /* Send directory entries as we find them, recursing into subdirectories */
    dentry = readdir (dir);
    while (dentry != NULL) {
        /* Skip "." and ".." */
        if (strcmp (dentry->d_name, ".") && strcmp (dentry->d_name, "..")) 
            if (!send_dir (sk, dentry->d_name))
                send_file (sk, dentry->d_name);
        dentry = readdir (dir);
    }
    closedir (dir);

    /* All dentries processed, notify by telling receiver to go back to parent
     * directory */
    header.type    = C_ITEM_DIR;
    header.name[0] = header.name[1] = '.';
    header.name[2] = '\0';
    send_data (sk, (char *) &header, sizeof (header));

    /* Here too */
    change_wd ("..");
    return 1;
}


/*
 * change_wd - Change working directory, similar to "cd" command. If dirname
 *             doesn't exists then is created.
 */
void change_wd (char *dirname)
{
    int e;

#ifdef HASEFROCH
    e = mkdir (dirname);
#else
    e = mkdir (dirname, 0755);
#endif
    if ((e == -1) && (errno != EEXIST)) 
        fatal ("Creating directory '%s'", dirname);

    e = chdir (dirname);
    if (e == -1)
        fatal ("Changing current directory to '%s'", dirname);
}


/*
 * basename - Strip absolute path. Similar to UNIX "basename" command but with a
 *            somewhat dirty implementation.
 */
char *basename (char *path)
{
#ifdef HASEFROCH
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif
    int p = strlen (path) - 1;

    while (path[p] == PATH_SEPARATOR) {
        path[p] = '\0';
        p--;
    }
    while ((p > 0) && (path[p] != PATH_SEPARATOR))
        p--;
    if (path[p] == PATH_SEPARATOR)
        p++;
    return path + p;
}


/************************  OUTPUT FEEDBACK FUNCTIONS  ************************/
/*
 * NOTE: Contributed by MKD: http://www.claudiocamacho.org
 */
#define BAR_DATA_WIDTH    46
#define BAR_DEFAULT_WIDTH 80
#define BAR_MINIMUM_WIDTH (BAR_DATA_WIDTH + 4)

static const char *metric[] = { "B/s", "K/s", "M/s" };

/*
 * query_terminal_width - Returns the number of columns in the current terminal,
 *                        so we can fit better the progress bar. Code stolen
 *                        from GNU Wget.
 */
static int query_terminal_width (void)
{
    int w = BAR_DEFAULT_WIDTH;
#ifdef HASEFROCH
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (GetConsoleScreenBufferInfo (GetStdHandle (STD_ERROR_HANDLE), &csbi))
        w = csbi.dwSize.X;
#else
    int fd, e;
    struct winsize wsz;

    fd = fileno (stderr);
    e  = ioctl (fd, TIOCGWINSZ, &wsz);
    if (e > -1)
        w = wsz.ws_col;
#endif /* HASEFROCH */

    return (w < BAR_MINIMUM_WIDTH ? BAR_MINIMUM_WIDTH : w);
}


/*
 * draw_bar - Calculate and displays the GNU Wget style progress bar. Then, this
 *            is mostly stolen from GNU Wget too.
 *
 * This is the format (one space margin at the end of the line):
 *
 * 999% [===...] 9,999,999,999 999.9 X/s  ETA 99:99:99
 *
 * Discount characters: 4 + 2 + 13 + 5 + 3 + 3 + 8 = 38
 * Discount spaces    : 1 + 1 +  1 + 1 + 2 + 1 + 1 =  8
 * Total: 46 (As defined by BAR_DATA_WIDTH)
 */
static void draw_bar (progress_t *pr)
{
    int   bar_size = pr->term_width - BAR_DATA_WIDTH;
    int   remaining, i, j, mag;
    float percent, fill, speed;
    int   eta, eta_hour, eta_min, eta_sec;
    char  sent_str[16], sent_str_b[16], speed_str[8], eta_str[12];
    char  bar[bar_size];

    /* Calculate the percentage and the bar fill amount. Some temporary
     * calculations have to done in float representation because of overflow
     * issues */
    percent = ((float) pr->completed_bytes / (float) pr->total_bytes) * 100.0;
    fill    = ((float) bar_size * percent) / 100.0;

    /* Paint the bar */
    memset (bar, ' ', bar_size);
    memset (bar, '=', (size_t) fill);
    bar[bar_size] = '\0';

    /* Beautify the amount of transferred bytes with thousand separators */
    i = snprintf (sent_str, 16, "%lu", pr->completed_bytes);
    j = i + (i - 1) / 3;
    sent_str_b[j] = '\0';
    do {
        sent_str_b[--j] = sent_str[--i]; if (i == 0) break;
        sent_str_b[--j] = sent_str[--i]; if (i == 0) break;
        sent_str_b[--j] = sent_str[--i]; if (i == 0) break;
        sent_str_b[--j] = ',';
    } while (1);

    /* Calculate transfer speed and estimated time */
    if (pr->last_delta == 0.0)
        pr->last_delta = 0.001;
    speed     = ((float) pr->last_transfer) / pr->last_delta;
    remaining = pr->total_bytes - pr->completed_bytes;
    eta       = (int) (remaining / speed);

    /* Adjust speed magnitude */
    if (speed > (1024.0 * 1024.0)) {
        speed /= (1024.0 * 1024.0);
        mag    = 2;
    } else if (speed > 1024.0) {
        speed /= 1024.0;
        mag    = 1;
    } else {
        mag = 0;
    }
    sprintf (speed_str, "%3.1f", speed);

    /* Beautify estimated time value */
    eta_min  = eta / 60;
    eta_sec  = eta % 60;
    eta_hour = eta_min / 60;
    eta_min  = eta_min % 60;
    sprintf (eta_str, "%02d:%02d:%02d", eta_hour, eta_min, eta_sec);

    /* Then print */
#ifdef HASEFROCH
    printf ("\r%3d%% [%s] %-13s %5s %s  ETA %s", (int) percent, bar, sent_str_b,
            speed_str, metric[mag], eta_str);
#else
    printf ("\033[1A%3d%% [%s] %-13s %5s %s  ETA %s\n", (int) percent, bar,
            sent_str_b, speed_str, metric[mag], eta_str);
#endif
}


/*
 * setup_progress - Prepare progress output for a single file.
 */
void setup_progress (progress_t *pr, char *filename, unsigned long size)
{
    pr->term_width      = query_terminal_width ();
    pr->total_bytes     = size;
    pr->completed_bytes = 0;
    pr->last_transfer   = 0;
    pr->last_delta      = 0.0;

    ftime (&pr->last_timestamp);

#ifdef HASEFROCH
    printf ("\r\nTransferring '%s':\r\n", filename);
#else
    printf ("Transferring '%s':\n\n", filename);
#endif
}

/*
 * show_progress - Show amount of transfer and percentage.
 */
void show_progress (progress_t *pr, unsigned long sent)
{
    int millis;
    struct timeb timestamp;

    ftime (&timestamp);

    millis = ((int) timestamp.millitm) - ((int) pr->last_timestamp.millitm);
    if (millis < 0) {
        millis += 1000;
        pr->last_timestamp.time++;
    }
    pr->last_delta      = ((float) (timestamp.time - pr->last_timestamp.time)) +
                          ((float) millis) * 1.0e-3;
    pr->last_transfer   = sent - pr->completed_bytes;
    pr->completed_bytes = sent;
    pr->last_timestamp  = timestamp;

    draw_bar (pr);
}

