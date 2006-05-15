/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                      Original idea      : C2H5OH                           */
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
#define CANUTE_PORT        1121
#define CANUTE_BLOCK_SIZE  65536
#define CANUTE_NAME_LENGTH 247 /* This is measured so header_t is 256 bytes */
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define  INVALID_SOCKET -1
#define  SOCKET_ERROR   -1
#define  CCP_CAST  /* Cast to (const char *) not needed in UNIX */
#define  SOCKADDR struct sockaddr
#define  CloseSocket(sk) close( sk )
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
    char name[CANUTE_NAME_LENGTH + 1];
} header_t;

/*
 * Send/receive buffer. Global may be uglier but is a lot handier.
 */
static char buffer[CANUTE_BLOCK_SIZE];

/* 
 * Auxiliary functions (prototypes)
 */
void   Help           (char *);
void   Error          (char *, ...);
void   Fatal          (char *, ...);
char  *BaseName       (char *);
SOCKET OpenConnection (char *);
void   SendData       (SOCKET, char *, size_t);
void   SendFile       (SOCKET, char *);
int    SendDir        (SOCKET, char *);
void   ReceiveData    (SOCKET, char *, size_t);
void   ChangeCurDir   (char *);

/*
 * Output feedback functions (prototypes)
 */
void SetupProgress (char *, unsigned long);
void ShowProgress  (unsigned long);


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

    if( argc < 2 ) 
        Help( argv[0] );

    if( strncmp( argv[1], "send", 4 ) == 0 ) {

        /*********************/
        /***  SENDER MODE  ***/
        /*********************/
        int i, arg = -1;

        /* Open connection */
        if( strcmp( argv[1], "send" ) == 0 ) {
            /* As server */
            if( argc < 3 )
                Help( argv[0] );
            sk  = OpenConnection( NULL );
            arg = 2;
        } else if( strcmp( argv[1], "sendto" ) == 0 ) {
            /* As client */
            if( argc < 4 )
                Help( argv[0] );
            sk  = OpenConnection( argv[2] );
            arg = 3;
        }

        /* Adjust send buffer */
        i = CANUTE_BLOCK_SIZE;
        setsockopt( sk, SOL_SOCKET, SO_SNDBUF, CCP_CAST &i, sizeof( i ) );

        /* Now we have the transmission channel open, so let's send everything
         * we're supposed to send. This isn't the most elegant way to do it but
         * is compact and keeps portability (deciding wether to send a file or a
         * directory, also used in send_dir()) */
        for( i = arg; i < argc; i++ )
            if( ! SendDir( sk, argv[i] ) )
                SendFile( sk, argv[i] );

        /* It's over. Notify the receiver to finish as well, please */
        header.type = C_ITEM_END;
        SendData( sk, (char *) &header, sizeof( header ) );

    } else if( strncmp( argv[1], "get", 3 ) == 0 ) {

        /***********************/
        /***  RECEIVER MODE  ***/
        /***********************/
        unsigned long received_bytes, total_bytes, b;
        FILE *file;

        /* Open connection */
        if( strcmp( argv[1], "get" ) == 0 ) {
            /* As client */
            if( argc < 3 )
                Help( argv[0] );
            sk = OpenConnection( argv[2] );
        } else if( strcmp( argv[1], "getserv" ) == 0 ) {
            /* As server */
            sk = OpenConnection( NULL );
        }

        /* Adjust receive buffer */
        b = CANUTE_BLOCK_SIZE;
        setsockopt( sk, SOL_SOCKET, SO_RCVBUF, CCP_CAST &b, sizeof( b ) );

        do {
            /* Header tells us how to proceed */
            ReceiveData( sk, (char *) &header, sizeof( header ) );

            if( header.type == C_ITEM_END )
                /* End of transmission, leave */
                break;

            if( header.type == C_ITEM_DIR ) {
                /* Directory, recurse */
                ChangeCurDir( header.name );
                continue;
            }

            /* In other case, it's a file */
            file = fopen( header.name, "wb" );
            if( file == NULL )
                Fatal( "Could not create '%s'", header.name );

            received_bytes = 0;
            total_bytes    = ntohl( header.size );
            SetupProgress( header.name, total_bytes );

            while( received_bytes < total_bytes ) {
                b = total_bytes - received_bytes;
                if( b > CANUTE_BLOCK_SIZE )
                    b = CANUTE_BLOCK_SIZE;

                ReceiveData( sk, buffer, b );
                fwrite( buffer, 1, b, file );

                received_bytes += b;
                ShowProgress( received_bytes );
            }

            fflush( file );
            fclose( file );
        } while( 1 );

    }

    CloseSocket( sk );
#ifdef HASEFROCH
    printf("\r\n");
#endif
    return EXIT_SUCCESS;
}


/**************************  AUXILIARY FUNCTIONS  **************************/


/*
 * Help - Show command syntax and exit not successfully.
 */
void Help (char *argv0)
{
    printf( "Syntax:\n" );
    printf( "\t%s send    <file/directory> [<file/directory> ...]\n",
            argv0 );
    printf( "\t%s get     <host/IP>\n", argv0 );
    printf( "\t%s sendto  <host/IP> <file/directory> [<file/directory> ...]\n",
            argv0 );
    printf( "\t%s getserv\n", argv0 );
    exit( EXIT_FAILURE );
}


/*
 * Error - Error message a la printf(). Custom message + system error string.
 */
void Error (char *msg, ...)
{
    va_list pars;
    char s[128];

    fputs( "ERROR: ", stderr );
    va_start( pars, msg );
    vsnprintf( s, 128, msg, pars );
    va_end( pars );
    perror( s );
}


/*
 * Fatal - Fatal error. Same as error() but also exit failing.
 */
void Fatal (char *msg, ...)
{
    va_list pars;
    char s[128];

    fputs( "FATAL ERROR: ", stderr );
    va_start( pars, msg );
    vsnprintf( s, 128, msg, pars );
    va_end( pars );
    perror( s );
    exit( EXIT_FAILURE );
}


/*
 * OpenConnection - Set up a connection whit the remote host in a portable
 *                  manner. If host is NULL then act as a server, otherwise
 *                  connect as a client to 'host'. On error abort so when this
 *                  function returns it does it always successfully.
 */
SOCKET OpenConnection (char *host)
{
    SOCKET sk, sk2;
    struct sockaddr_in saddr;
    struct hostent *he;
    int e;
#ifdef HASEFROCH 
    WSADATA ws;

    /* This is safe here because open_connection() is only called once */
    if( WSAStartup( MAKEWORD( 1,1 ), &ws ) ) 
        Fatal( "Starting WinSock" );
    atexit( (void (*)()) WSACleanup );
#endif

    sk = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( sk == INVALID_SOCKET ) 
        Fatal( "Creating socket" );

    saddr.sin_family = AF_INET;
    saddr.sin_port   = htons( CANUTE_PORT );

    if( host == NULL ) {

        /*********************/
        /***  SERVER MODE  ***/
        /*********************/
        saddr.sin_addr.s_addr = INADDR_ANY;

        /* Ignore errors from setsockopt(), bind() will fail in that case */
        e = 1;
        setsockopt( sk, SOL_SOCKET, SO_REUSEADDR, CCP_CAST &e, sizeof( e ) );
        e = bind( sk, (SOCKADDR *) &saddr, sizeof( saddr ) );
        if( e == SOCKET_ERROR )
            Fatal( "Opening port %d", CANUTE_PORT );

        e = listen( sk, 1 );
        if( e == SOCKET_ERROR )
            Fatal( "Listening port %d", CANUTE_PORT );

        e = sizeof( saddr );
        sk2 = accept( sk, (SOCKADDR *) &saddr, (socklen_t *) &e );
        if( sk2 == INVALID_SOCKET )
            Fatal( "Accepting client connection" );

        /* Close unneeded sockets */
        CloseSocket( sk );
        sk = sk2;

    } else {

        /*********************/
        /***  CLIENT MODE  ***/
        /*********************/
        saddr.sin_addr.s_addr = inet_addr( host );

        if( saddr.sin_addr.s_addr == INADDR_NONE ) {
            /* Invalid IP, maybe we have to do a DNS query */
            he = gethostbyname( host );
            if( he == NULL ) {
#ifdef HASEFROCH
                Fatal( "Invalid IP or hostname" );
#else
                herror( "FATAL ERROR: Invalid IP or hostname" );
                exit( EXIT_FAILURE );
#endif
            }
            saddr.sin_addr.s_addr = ((struct in_addr *)he->h_addr)->s_addr;
        }

        /* Now we have a destination host */
        e = connect( sk, (SOCKADDR *) &saddr, sizeof( saddr ) );
        if( e == SOCKET_ERROR ) 
            Fatal( "Connecting to host '%s'", host );

    }

    /* Connection is ready */
    return sk; 
}


/*
 * SendData - Send count bytes over the connection. Doesn't finish until all of
 *            them are sent. On error aborts.
 */
void SendData (SOCKET sk, char *data, size_t count)
{
    int s; /* Sent bytes in one send() call */

    while( count > 0 ) {
        s = send( sk, data, count, 0 );
        if( s == SOCKET_ERROR ) 
            Fatal( "Sending data" );
        count -= s;
        data  += s;
    }
}


/*
 * ReceiveData - Receive count bytes from the connection. Doesn't finish until
 *               all bytes are received. On error aborts.
 */
void ReceiveData (SOCKET sk, char *data, size_t count)
{
    int r; /* Received bytes in one recv() call */

    while( count > 0 ) {
        r = recv( sk, data, count, 0 );
        if( r == SOCKET_ERROR ) 
            Fatal( "Receiving data" );
        count -= r;
        data  += r;
    }
}


/*
 * SendFile - Send a whole file. If there's an error before sending the file
 *            content, the file is skipped. On transmission error aborts.
 */
void SendFile (SOCKET sk, char *filename)
{
    unsigned long total_bytes, sent_bytes, b;
    int e;
    header_t header;
    FILE *file;

    file = fopen( filename, "rb" );
    if( file == NULL ) {
        Error( "Opening file '%s'", filename );
        return;
    }

    /* Get the file size (cross-platform) */
    e = fseek( file, 0L, SEEK_END );
    if( e == -1 ) {
        Error( "Checking file size of '%s'", filename );
        return;
    }
    total_bytes = ftell( file );
    rewind( file );

    /* Send a header with relevant file info */
    strncpy( header.name, BaseName( filename ), CANUTE_NAME_LENGTH );
    header.type = C_ITEM_FILE;
    header.name[CANUTE_NAME_LENGTH] = '\0';
    header.size = htonl( total_bytes );
    SendData( sk, (char *) &header, sizeof( header ) );

    /* Send the contents */
    sent_bytes = 0;
    SetupProgress( filename, total_bytes );

    while( sent_bytes < total_bytes ) {
        b = fread( buffer, 1, CANUTE_BLOCK_SIZE, file );
        SendData( sk, buffer, b );
        sent_bytes += b;
        ShowProgress( sent_bytes );
    }

    fclose( file );
}


/*
 * SendDir - Send dirname files and subdirectories recursively. To make code
 *           more compact and portable we rely on the behaviour of opendir()
 *           which fails when trying to open a file. Returns false if dirname is
 *           a file and true if directory was completely sent.
 */
int SendDir (SOCKET sk, char *dirname)
{
    DIR *dir;
    struct dirent *dentry;
    header_t header;

    dir = opendir( dirname );
    if( dir == NULL ) 
        return 0; 

    /* Tell de receiver to create and change to "dir" */
    header.type = C_ITEM_DIR;
    header.size = 0;
    strncpy( header.name, BaseName( dirname ), CANUTE_NAME_LENGTH );
    header.name[CANUTE_NAME_LENGTH] = '\0';
    SendData( sk, (char *) &header, sizeof( header ) );

    /* We also move into the directory */
    ChangeCurDir( dirname );

    /* Send directory entries as we find them, recursing into subdirectories */
    dentry = readdir( dir );
    while( dentry != NULL ) {
        /* Skip "." and ".." */
        if( strcmp( dentry->d_name, "." ) && strcmp( dentry->d_name, ".." ) ) 
            if( ! SendDir( sk, dentry->d_name ) )
                SendFile( sk, dentry->d_name );
        dentry = readdir( dir );
    }
    closedir( dir );

    /* All dentries processed, notify by telling receiver to go back to parent
     * directory */
    header.type    = C_ITEM_DIR;
    header.name[0] = header.name[1] = '.';
    header.name[2] = '\0';
    SendData( sk, (char *) &header, sizeof( header ) );

    /* Here too */
    ChangeCurDir( ".." );
    return 1;
}


/*
 * ChangeCurDir - Change current directory, similar to "cd" command. If dirname
 *                doesn't exists then is created.
 */
void ChangeCurDir (char *dirname)
{
    int e;

#ifdef HASEFROCH
    e = mkdir( dirname );
#else
    e = mkdir( dirname, 0755 );
#endif
    if( (e == -1) && (errno != EEXIST) ) 
        Fatal( "Creating directory '%s'", dirname );

    e = chdir( dirname );
    if( e == -1 )
        Fatal( "Changing current directory to '%s'", dirname );
}


/*
 * BaseName - Strip absolute path. Similar to UNIX "basename" command but with a
 *            somewhat dirty implementation.
 */
char *BaseName (char *path)
{
#ifdef HASEFROCH
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif
    int p = strlen( path ) - 1;

    while( path[p] == PATH_SEPARATOR ) {
        path[p] = '\0';
        p--;
    }
    while( (p > 0) && (path[p] != PATH_SEPARATOR) )
        p--;
    if( path[p] == PATH_SEPARATOR )
        p++;
    return path + p;
}


/************************  OUTPUT FEEDBACK FUNCTIONS  ************************/

/* Global variables related to output feedback */
static char *pr_filename;
static int   pr_shift;
static char  pr_prefix;
static unsigned long pr_size;


/*
 * SetupProgress - Prepare progress output fo a single file.
 */
void SetupProgress (char *filename, unsigned long size)
{
    pr_filename = filename;
    pr_size     = size;
    pr_shift    = 0;
    pr_prefix   = ' ';
    if( (size >> 20) > 10 ) {
        pr_shift  = 20;
        pr_prefix = 'M';
    } 
#ifdef HASEFROCH
#define FORMAT "\r\n%.40s: 0 / %lu %cbytes (0%%)"
#else
#define FORMAT "%.40s: 0 / %lu %cbytes (0%%)\n"
#endif
    printf( FORMAT, filename, (size >> pr_shift), pr_prefix );
}


/*
 * ShowProgress - Show amount of transfer and percentage.
 */
void ShowProgress (unsigned long sent)
{
#ifdef HASEFROCH
#define FORMAT2 "\r%.40s: %lu / %lu %cbytes (%lu%%)"
#else
#define FORMAT2 "\033[1A%.40s: %lu / %lu %cbytes (%lu%%)\n"
#endif
    printf( FORMAT2, pr_filename, (sent >> pr_shift), (pr_size >> pr_shift),
            pr_prefix, ((sent >> pr_shift) * 100) / (pr_size >> pr_shift) );
}

