/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                           PLATFORM INDEPENDENCE                            */
/*                                                                            */
/******************************************************************************/

/* Constants */
#define CANUTE_VERSION_STR  "v1.1"
#define CANUTE_DEFAULT_PORT 1121
#define CANUTE_NAME_LENGTH  239  /* Don't touch this */
#define CANUTE_BLOCK_BITS   16
#define CANUTE_BLOCK_SIZE   (1 << CANUTE_BLOCK_BITS)
#define CANUTE_BLOCK_MASK   (CANUTE_BLOCK_SIZE - 1)
#define REQUEST_FILE        1
#define REQUEST_BEGINDIR    2
#define REQUEST_ENDDIR      3
#define REQUEST_END         4
#define REPLY_ACCEPT        5
#define REPLY_SKIP          6

/* Large File Support */
#define _FILE_OFFSET_BITS   64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

/* Common headers */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

#if defined(__WIN32__) || defined(WIN32)

/* Definitions and headers (Hasefroch) */
#include <windows.h>
#include <winsock.h>
#define  HASEFROCH
#define  MSG_WAITALL 0    /* Hasefroch sucks and does not define this */
#define  CCP_CAST (const char *)
#define  IS_PATH_SEPARATOR(x) ((x) == '\\' || (x) == '/')
#define  stat_info __stat64
#define  stat(path, buf) _stat64((path), (buf))
typedef int socklen_t;
extern int _stat64 (const char *path, struct __stat64 *buffer);
extern int fseeko  (FILE *stream, off_t offset, int whence);
/* fseeko() implemented in util.c */

#else

/* Definitions and headers (UNIX) */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#define  INVALID_SOCKET -1
#define  SOCKET_ERROR   -1
#define  SOCKADDR struct sockaddr
#define  CCP_CAST  /* Cast to (const char *) not needed in UNIX */
#define  IS_PATH_SEPARATOR(x) ((x) == '/')
#define  closesocket(sk) close(sk)
#define  mkdir(path) mkdir(path, 0755)
#define  stat_info stat
typedef int SOCKET;

#endif  /* WIN32 */

/* Solaris needs this */
#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

/* PATH_MAX does not seem to be defined in Hasefroch nor in Solaris */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Helper macro: True if str is NOT "." or ".." */
#define NOT_SELF_OR_PARENT(str) \
        (str[0] != '.' \
         || (str[1] != '\0' && (str[1] != '.' || str[2] != '\0')))

/*
 * Transmission header packet. This defines what kind of item is going to be
 * transferred to the receiver.
 */
struct header {
        int  type;
        int  reserved;
        int  blocks;
        int  extra;
        char name[CANUTE_NAME_LENGTH + 1];
};


/***************************  FUNCTION PROTOTYPES  ***************************/

/* feedback.c */
void setup_progress  (char *name, long long size, long long offset);
void update_progress (size_t increment);
void finish_progress (void);

/* net.c */
SOCKET open_connection_server (unsigned short port);
SOCKET open_connection_client (char *host, unsigned short port);
void   send_data              (SOCKET sk, char *buf, size_t count);
void   receive_data           (SOCKET sk, char *buf, size_t count);
void   send_message           (SOCKET sk, int type, long long size, char *name);
int    receive_message        (SOCKET sk, long long *size, char *name);

/* protocol.c */
void send_item    (SOCKET sk, char *name);
int  receive_item (SOCKET sk);

/* util.c */
char *basename  (char *path);
void  error     (char *msg, ...);
void  fatal     (char *msg, ...);
void  help      (char *argv0);
/* fseeko() also implemented, but only in HASEFROCH */

