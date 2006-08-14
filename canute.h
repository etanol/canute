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
#define CANUTE_VERSION_STR  "v1.0"
#define CANUTE_DEFAULT_PORT 1121
#define CANUTE_NAME_LENGTH  239    /* Don't touch this */
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
#include <stdint.h>
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
#define  CCP_CAST (const char *)
#define  IS_PATH_SEPARATOR(x) ((x) == '\\' || (x) == '/')
#define  stat __stat64
typedef int socklen_t;
extern int _stat64 (const char *path, struct __stat64 *buffer);

static inline int stat (const char *f, struct stat *s) { return _stat64(f, s); }
static inline int do_mkdir (const char *f)             { return mkdir(f); }

#else

/* Definitions and headers (UNIX) */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#define  INVALID_SOCKET -1
#define  SOCKET_ERROR   -1
#define  SOCKADDR struct sockaddr
#define  CCP_CAST  /* Cast to (const char *) not needed in UNIX */
#define  IS_PATH_SEPARATOR(x) ((x) == '/')
typedef int SOCKET;

static inline int closesocket (SOCKET s)      { return close(s); }
static inline int do_mkdir    (const char *f) { return mkdir(f, 0755); }
#endif  /* WIN32 */

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
void setup_progress  (char *name, int64_t size, int64_t offset);
void show_progress   (size_t increment);
void finish_progress (void);

/* net.c */
SOCKET open_connection_server (uint16_t port);
SOCKET open_connection_client (char *host, uint16_t port);
void   send_data              (SOCKET sk, char *buf, size_t count);
void   receive_data           (SOCKET sk, char *buf, size_t count);
void   send_message           (SOCKET sk, int type, int64_t size, char *name);
int    receive_message        (SOCKET sk, int64_t *size, char *name);

/* protocol.c */
void send_item    (SOCKET sk, char *name);
int  receive_item (SOCKET sk);

/* util.c */
char *basename (char *path);
void  error    (char *msg, ...);
void  fatal    (char *msg, ...);
int   fseeko   (FILE *stream, off_t offset, int whence);
void  help     (char *argv0);

