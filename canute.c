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

/* Constants */
#define CANUTE_VERSION_STR  "v0.5"
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
#include <time.h>

#if defined(__WIN32__) || defined(WIN32)
/* Definitions and headers (Hasefroch) */
#include <windows.h>
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
#include <sys/time.h>
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
 * Send/receive buffer. Global may be uglier but is a lot handier.
 */
char buffer[CANUTE_BLOCK_SIZE];

/* 
 * Auxiliary functions (prototypes)
 */
void   help         (char *argv0);
void   error        (char *msg, ...);
void   fatal        (char *msg, ...);
char  *basename     (char *path);
void   send_data    (SOCKET sk, char *data, size_t count);
void   send_file    (SOCKET sk, char *filename);
int    send_dir     (SOCKET sk, char *dirname);
void   receive_data (SOCKET sk, char *data, size_t count);
void   change_wd    (char *dirname);

SOCKET open_connection_server (uint16_t port);
SOCKET open_connection_client (char *host, uint16_t port);

/*
 * Output feedback functions (prototypes)
 */
void setup_progress  (char *filename, unsigned long size);
void show_progress   (unsigned long increment);
void finish_progress (void);


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
int
main (int argc, char **argv)
{
        SOCKET sk = -1; /* Quest for a warning free compilation */ 
        header_t header;
        char    *port_str;
        uint16_t port;
#ifdef HASEFROCH 
        WSADATA ws;

        if (WSAStartup (MAKEWORD (1, 1), &ws)) 
                fatal ("Starting WinSock");
        atexit ((void (*)()) WSACleanup);
#endif

        if (argc < 2) 
                help (argv[0]);

        /* See if there is a port specification to override default */
        port     = CANUTE_DEFAULT_PORT;
        port_str = strchr (argv[1], ':');
        if (port_str != NULL) {
                *port_str = '\0';
                port_str++;
                port = atoi (port_str);
        }

        if (strncmp (argv[1], "send", 4) == 0) {

                /*********************/
                /***  SENDER MODE  ***/
                /*********************/
                int i, arg = -1;

                /* Open connection */
                if (strcmp (argv[1], "send") == 0) {
                        if (argc < 3)
                                help (argv[0]);
                        sk  = open_connection_server (port);
                        arg = 2;
                } else if (strcmp (argv[1], "sendto") == 0) {
                        if (argc < 4)
                                help (argv[0]);
                        sk  = open_connection_client (argv[2], port);
                        arg = 3;
                }

                /* Adjust send buffer */
                i = CANUTE_BLOCK_SIZE;
                setsockopt (sk, SOL_SOCKET, SO_SNDBUF, CCP_CAST &i, sizeof (i));

                /* Now we have the transmission channel open, so let's send
                 * everything we're supposed to send. This isn't the most
                 * elegant way to do it but is compact and keeps portability
                 * (deciding wether to send a file or a directory, also used in
                 * send_dir()) */
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

                /* Open connection */
                if (strcmp (argv[1], "get") == 0) {
                        if (argc < 3)
                                help (argv[0]);
                        sk = open_connection_client (argv[2], port);
                } else if (strcmp (argv[1], "getserv") == 0) {
                        sk = open_connection_server (port);
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
                        setup_progress (header.name, total_bytes);

                        while (received_bytes < total_bytes) {
                                b = total_bytes - received_bytes;
                                if (b > CANUTE_BLOCK_SIZE)
                                        b = CANUTE_BLOCK_SIZE;

                                receive_data (sk, buffer, b);
                                fwrite       (buffer, 1, b, file);

                                show_progress (b);
                                received_bytes += b;
                        }

                        finish_progress ();
                        fflush (file);
                        fclose (file);
                } while (1);

        } else {
                help (argv[0]);
        }

        closesocket (sk);
#ifdef HASEFROCH
        /* Avoid clobbering the last printed line */
        printf ("\r\n");
#endif
        return EXIT_SUCCESS;
}


/**************************  AUXILIARY FUNCTIONS  **************************/

/*
 * help
 *
 * Show command syntax and exit not successfully.
 */
void
help (char *argv0)
{
        printf ("Canute " CANUTE_VERSION_STR "\n\n"
                "Syntax:\n"
                "\t%s send[:port]   <file/directory> [<file/directory> ...]\n"
                "\t%s get[:port]    <host/IP>\n"
                "\t%s sendto[:port] <host/IP> <file/directory> [<file/directory> ...]\n"
                "\t%s getserv[:port]\n", argv0, argv0, argv0, argv0);
        exit (EXIT_FAILURE);
}


/*
 * error
 *
 * Error message a la printf(). Custom message + system error string.
 */
void
error (char *msg, ...)
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
 * fatal
 * 
 * Fatal error. Same as error() but also exit failing.
 */
void
fatal (char *msg, ...)
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
 * open_connection_server
 *
 * Set up a connection in server mode. It opens the specified port for listening
 * and waits for someone to connect. It returns the connected socket ready for
 * transmission. On error aborts so when this function returns, it does it
 * always successfully.
 */
SOCKET
open_connection_server (uint16_t port)
{
        SOCKET bsk, sk; /* Binding socket and the real transmision socket */
        struct sockaddr_in saddr;
        int e;

        bsk = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bsk == INVALID_SOCKET) 
                fatal ("Creating socket");

        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons (port);
        saddr.sin_addr.s_addr = INADDR_ANY;

        /* Ignore errors from setsockopt(), bind() will fail in that case */
        e = 1;
        setsockopt (bsk, SOL_SOCKET, SO_REUSEADDR, CCP_CAST &e, sizeof (e));

        e = bind (bsk, (SOCKADDR *) &saddr, sizeof (saddr));
        if (e == SOCKET_ERROR)
                fatal ("Opening port %d", port);

        e = listen (bsk, 1);
        if (e == SOCKET_ERROR)
                fatal ("Listening port %d", port);

        e  = sizeof (saddr);
        sk = accept (bsk, (SOCKADDR *) &saddr, (socklen_t *) &e);
        if (sk == INVALID_SOCKET)
                fatal ("Accepting client connection");

        /* Close unneeded sockets */
        closesocket (bsk);
        return sk;
}


/*
 * open_connection_client
 *
 * Set up a connection in client mode. It tries to connect to the specified host
 * (either a hostname or an IP address) and returns the connected socket ready
 * for transmission. Similary to open_connection_server, on error aborts.
 */
SOCKET
open_connection_client (char *host, uint16_t port)
{
        SOCKET sk;
        struct sockaddr_in saddr;
        struct hostent *he;
        int e;

        sk = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sk == INVALID_SOCKET) 
                fatal ("Creating socket");

        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons (port);
        saddr.sin_addr.s_addr = inet_addr (host);
        if (saddr.sin_addr.s_addr == INADDR_NONE) {
                /* Invalid IP, maybe we have to do a DNS query */
                he = gethostbyname (host);
                if (he == NULL) {
#ifdef HASEFROCH
                        fatal ("Invalid IP or hostname");
#else
                        herror ("FATAL ERROR: Invalid IP or hostname");
                        exit   (EXIT_FAILURE);
#endif
                }
                saddr.sin_addr.s_addr = ((struct in_addr *) he->h_addr)->s_addr;
        }

        /* Now we have a destination host */
        e = connect (sk, (SOCKADDR *) &saddr, sizeof (saddr));
        if (e == SOCKET_ERROR) 
                fatal ("Connecting to host '%s'", host);

        return sk; 
}


/*
 * send_data
 *
 * Send count bytes over the connection. Doesn't finish until all of them are
 * sent. On error aborts.
 */
void
send_data (SOCKET sk, char *data, size_t count)
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
 * receive_data
 *
 * Receive count bytes from the connection. Doesn't finish until all bytes are
 * received. On error aborts.
 */
void
receive_data (SOCKET sk, char *data, size_t count)
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
 * send_file
 *
 * Send a whole file. If there's an error before sending the file content, the
 * file is skipped. On transmission error aborts.
 */
void
send_file (SOCKET sk, char *filename)
{
        unsigned long total_bytes, sent_bytes, b;
        int e;
        FILE *file;
        header_t header;

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
        setup_progress (filename, total_bytes);

        while (sent_bytes < total_bytes) {
                b = fread (buffer, 1, CANUTE_BLOCK_SIZE, file);
                send_data (sk, buffer, b);
                show_progress (b);
                sent_bytes += b;
        }

        finish_progress ();
        fclose (file);
}


/*
 * send_dir
 *
 * Send dirname files and subdirectories recursively. To make code more compact
 * and portable we rely on the behaviour of opendir() which fails when trying to
 * open a file. Returns false if dirname is a file and true if directory was
 * completely sent.
 */
int
send_dir (SOCKET sk, char *dirname)
{
        DIR *dir;
        struct dirent *dentry;
        header_t header;

        dir = opendir (dirname);
        if (dir == NULL) 
                return 0; 

        /* Tell de receiver to create and change to "dir" */
        header.type = C_ITEM_DIR;
        header.size = 0;
        strncpy (header.name, basename (dirname), CANUTE_NAME_LENGTH);
        header.name[CANUTE_NAME_LENGTH] = '\0';
        send_data (sk, (char *) &header, sizeof (header));

        /* We also move into the directory */
        change_wd (dirname);

        /* Send directory entries as we find them, recursing into subdirectories
         * too */
        dentry = readdir (dir);
        while (dentry != NULL) {
                /* Skip "." and ".." */
                if (strcmp (dentry->d_name, ".") != 0
                    && strcmp (dentry->d_name, "..") != 0) 
                        if (!send_dir (sk, dentry->d_name))
                                send_file (sk, dentry->d_name);
                dentry = readdir (dir);
        }
        closedir (dir);

        /* All dentries processed, notify by telling receiver to go back to
         * parent directory */
        header.type    = C_ITEM_DIR;
        header.name[0] = header.name[1] = '.';
        header.name[2] = '\0';
        send_data (sk, (char *) &header, sizeof (header));

        /* Here too */
        change_wd ("..");
        return 1;
}


/*
 * change_wd
 *
 * Change working directory, similar to "cd" command. If dirname doesn't exists
 * then is created.
 */
void
change_wd (char *dirname)
{
        int e;

#ifdef HASEFROCH
        e = mkdir (dirname);
#else
        e = mkdir (dirname, 0755);
#endif
        if (e == -1 && errno != EEXIST) 
                fatal ("Creating directory '%s'", dirname);

        e = chdir (dirname);
        if (e == -1)
                fatal ("Changing current directory to '%s'", dirname);
}


/*
 * basename
 *
 * Strip absolute path. Similar to UNIX "basename" command but with a somewhat
 * dirty implementation.
 */
char *
basename (char *path)
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
        while (p > 0 && path[p] != PATH_SEPARATOR)
                p--;
        if (path[p] == PATH_SEPARATOR)
                p++;
        return path + p;
}


/************************  OUTPUT FEEDBACK FUNCTIONS  ************************/
/*
 * NOTE: Contributed by MKD: http://www.claudiocamacho.org/tech/canute.phtml
 */
#define BAR_DATA_WIDTH    47
#define BAR_DEFAULT_WIDTH 80
#define BAR_MINIMUM_WIDTH (BAR_DATA_WIDTH + 4)

/* Progress state information */
int            terminal_width;
unsigned long  total_size;
unsigned long  completed_size;
struct timeval init_time;
struct timeval last_time;
float          delta[16];
int            delta_index;


/*
 * query_terminal_width
 *
 * Returns the number of columns in the current terminal, so we can fit better
 * the progress bar. Code stolen from GNU Wget.
 */
static int
query_terminal_width (void)
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


#ifdef HASEFROCH
/*
 * gettimeofday
 *
 * Simulate gettimeofday within win32 platforms.
 */
static void
gettimeofday (struct timeval *time, struct timeval *dummy)
{
        /* This procedure is mainly obtained from glib2/glib/gmain.c */
        FILETIME ft;
        uint64_t *time64 = (uint64_t *) &ft;

        GetSystemTimeAsFileTime (&ft);
        *time64 -= 116444736000000000ULL;
        *time64 /= 10;
        time->tv_sec  = *time64 / 1000000;
        time->tv_usec = *time64 % 1000000;
}
#endif /* HASEFROCH */


/*
 * elapsed_time
 *
 * Calculate the elapsed time from old_time to new_time in seconds as a float to
 * maintain microsecond accuracy. In case elapsed time is less than a
 * microsecond, then round it to that value so something greater than zero is
 * returned.
 */
static float
elapsed_time (struct timeval *old_time, struct timeval *new_time)
{
        struct timeval elapsed_time;
        float          secs;

        elapsed_time.tv_usec = new_time->tv_usec - old_time->tv_usec;
        if (elapsed_time.tv_usec < 0) {
                elapsed_time.tv_usec += 1000000;
                old_time->tv_sec++;
        }
        elapsed_time.tv_sec = new_time->tv_sec - old_time->tv_sec;

        secs = (float) elapsed_time.tv_sec + (float) elapsed_time.tv_usec*1.e-6;
        /* I know, real numbers should never be tested for equality */
        if (secs == 0.0)
                secs = 1.e-6;

        return secs;
}


/*
 * pretty_number
 *
 * Return a beautified string representation of an integer. String will contain
 * thousand separators.
 */
static char *
pretty_number (unsigned long num)
{
        static char str[16];
        char        ugly[16];
        int         i, j;

        i = snprintf (ugly, 16, "%lu", num);
        j = i + (i - 1) / 3;
        str[j] = '\0';
        do {
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ',';
        } while (1);

        return str;
}


/*
 * pretty_time
 *
 * Return a beautified string representation of an integer holding a time value,
 * in seconds. String format is "hour:min:sec".
 */
static char *
pretty_time (int secs)
{
        static char str[12];
        int         hour, min, sec;

        min  = secs / 60;
        sec  = secs % 60;
        hour = min / 60;
        min  = min % 60;

        if (hour > 0)
                snprintf (str, 9, "%2d:%02d:%02d", hour, min, sec);
        else
                snprintf (str, 9, "%2d:%02d     ", min, sec);

        return str;
}


/*
 * pretty_speed
 *
 * Scale and convert a given transfer rate to a beautified string. Metrics are
 * changed when value is scaled.
 */
static char *
pretty_speed (float rate)
{
        static char str[16];
        char       *metric;
        
        if (rate > 1024.0 * 1024.0 * 1024.0) {
                rate  /= 1024.0 * 1024.0 * 1024.0;
                metric = "G/s";
        } else if (rate > 1024.0 * 1024.0) {
                rate  /= 1024.0 * 1024.0;
                metric = "M/s";
        } else if (rate > 1024.0) {
                rate  /= 1024.0;
                metric = "K/s";
        } else {
                metric = "B/s";
        }

        snprintf (str, 16, "%4.1f %s", rate, metric);
        return str;
}


/*
 * draw_bar
 *
 * Calculate and displays the GNU Wget style progress bar. Then, this is mostly
 * stolen from GNU Wget too.
 *
 * This is the format: 
 *
 * 999% [===...] 9,999,999,999 9999.9 X/s  ETA 99:99:99
 *
 * Where each part needs:
 *
 *      999%          -->  4 chars + 1 space
 *      [===...]      -->  2 chars (and the remaining) + 1 space
 *      9,999,999,999 --> 13 chars + 1 space
 *      9999.9 X/s    -->  9 chars + 3 spaces
 *      ETA 99:99:99  --> 11 chars + 2 spaces
 *
 *      TOTAL         --> 39 chars + 8 spaces = 47 
 *      (As defined by BAR_DATA_WIDTH)
 */
static void
draw_bar (void)
{
        int   eta, bar_size = terminal_width - BAR_DATA_WIDTH;
        float percent, fill, speed, av_delta;
        char  bar[bar_size];

        /* Some temporary calculations have to done in float representation
         * because of overflow issues */
        percent = ((float) completed_size / (float) total_size) * 100.0;
        fill    = ((float) bar_size * percent) / 100.0;

        memset (bar, ' ', bar_size);
        memset (bar, '=', (size_t) fill);
        bar[bar_size] = '\0';

        av_delta = (delta[0]   + delta[1]  + delta[2]  + delta[3]  + delta[4]
                   + delta[5]  + delta[6]  + delta[7]  + delta[8]  + delta[9]
                   + delta[10] + delta[11] + delta[12] + delta[13] + delta[14]
                   + delta[15]) / 16.0;

        speed = (float) CANUTE_BLOCK_SIZE / av_delta;
        eta   = (int) ((float) (total_size - completed_size) / speed);

        /* Print all */
        printf ("\r%3d%% [%s] %-13s %10s  ETA %s", (int) percent, bar,
                pretty_number (completed_size), pretty_speed (speed),
                pretty_time (eta));
        fflush (stdout);
}


/*
 * setup_progress
 *
 * Prepare progress output for a single file.
 */
void
setup_progress (char *filename, unsigned long size)
{
        int i;

        /* Initialize the delta array before every single transfer */
        for (i = 0; i < 16; i += 4) {
                delta[i]     = 1.0; 
                delta[i + 1] = 1.0;
                delta[i + 2] = 1.0;
                delta[i + 3] = 1.0;
        }

        delta_index    = 0;
        terminal_width = query_terminal_width ();
        total_size     = size;
        completed_size = 0;

        printf ("Transferring '%s' (%s bytes):\n", filename,
                pretty_number (size));

        /* We watch the clock before and after the whole transfer to estimate an
         * average speed to be shown at the end. */
        gettimeofday (&init_time, NULL);
        last_time = init_time;
}


/*
 * show_progress
 *
 * Update the history ring and show amount of transfer and percentage.
 */
void
show_progress (unsigned long increment)
{
        struct timeval now;

        gettimeofday (&now, NULL);

        delta[delta_index] = elapsed_time (&last_time, &now);

        delta_index++;
        if (delta_index >= 16)
                delta_index = 0;

        completed_size += increment;
        last_time       = now;

        draw_bar ();
}


/*
 * finish_progress
 *
 * Show the average transfer rate and other general information.
 */
void
finish_progress (void)
{
        struct timeval now;
        float          total_elapsed, av_rate;

        gettimeofday (&now, NULL);

        total_elapsed = elapsed_time (&init_time, &now);
        av_rate       = (float) total_size / total_elapsed;

        printf ("\nCompleted %s bytes in %s (Average Rate: %s)\n\n",
                pretty_number (total_size), pretty_time (total_elapsed),
                pretty_speed (av_rate));
}

