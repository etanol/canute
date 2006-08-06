/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                             COMMON DEFINITIONS                             */
/*                                                                            */
/******************************************************************************/

/* Constants */
#define CANUTE_VERSION_STR  "v0.999"
#define CANUTE_DEFAULT_PORT 1121
#define CANUTE_BLOCK_SIZE   65536
#define CANUTE_NAME_LENGTH  239 /* Don't touch this */

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
#define HASEFROCH
#define CCP_CAST (const char *)
typedef int socklen_t;

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
typedef int SOCKET;

#endif  /* WIN32 */


/***************************  FUNCTION PROTOTYPES  ***************************/

/* canute.c */
void help  (char *argv0);
void error (char *msg, ...);
void fatal (char *msg, ...);

/* feedback.c */
void setup_progress  (char *filename, int64_t size, int64_t offset);
void show_progress   (size_t increment);
void finish_progress (void);

/* file.c */
char   *basename  (char *path);
int64_t file_size (char *path);
void    change_wd (char *path);
void    try_mkdir (char *path);
int     fseeko    (FILE *stream, off_t offset, int whence);

/* net.c */
SOCKET open_connection_server (uint16_t port);
SOCKET open_connection_client (char *host, uint16_t port);
void   close_connection       (SOCKET sk);
void   send_data              (SOCKET sk, char *buf, size_t count);
void   receive_data           (SOCKET sk, char *buf, size_t count);

/* protocol.c */
void send_file    (SOCKET sk, char *filename);
int  try_send_dir (SOCKET sk, char *dirname);
int  receive_item (SOCKET sk);
void end_session  (SOCKET sk);

