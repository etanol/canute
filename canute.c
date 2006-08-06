/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                   MAIN FUNCTION AND SOME UTILITY OTHERS                    */
/*                                                                            */
/******************************************************************************/

#include "canute.h"


/*
 * Four concepts are important here: server, client, sender and receiver. For
 * the sake of flexiblity wether the sender and receiver can be server or
 * client.
 *
 * Server listens for connections and client needs to know the server host.
 * Sender sends transmission requests, needs to know what items to send, and
 * receiver just obeys (or replies, in some cases) what sender says.
 *
 * Almost on any error the program aborts, look for calls to fatal().
 */
int
main (int argc, char **argv)
{
        SOCKET   sk = -1; /* Quest for a warning free compilation */ 
        char    *port_str;
        uint16_t port;
        int      i, arg = 0;
#ifdef HASEFROCH 
        WSADATA  ws;

        if (WSAStartup(MAKEWORD(1, 1), &ws)) 
                fatal("Starting WinSock");
        atexit((void (*)()) WSACleanup);
#endif

        if (argc < 2) 
                help(argv[0]);

        /* See if there is a port specification to override default */
        port     = CANUTE_DEFAULT_PORT;
        port_str = strchr(argv[1], ':');
        if (port_str != NULL) {
                *port_str = '\0';
                port_str++;
                port = atoi(port_str);
        }

        if (strncmp(argv[1], "send", 4) == 0) {

                /*********************/
                /***  SENDER MODE  ***/
                /*********************/

                /* Open connection */
                if (strcmp(argv[1], "send") == 0) {
                        if (argc < 3)
                                help(argv[0]);
                        sk  = open_connection_server(port);
                        arg = 2;
                } else if (strcmp(argv[1], "sendto") == 0) {
                        if (argc < 4)
                                help(argv[0]);
                        sk  = open_connection_client(argv[2], port);
                        arg = 3;
                } else {
                        help(argv[0]);
                }

                /* Adjust send buffer */
                i = CANUTE_BLOCK_SIZE;
                setsockopt(sk, SOL_SOCKET, SO_SNDBUF, CCP_CAST &i, sizeof(i));

                /* Now we have the transmission channel open, so let's send
                 * everything we're supposed to send */
                for (i = arg; i < argc; i++)
                        if (!try_send_dir(sk, argv[i]))
                                send_file(sk, argv[i]);

                /* It's over. Notify the receiver to finish as well, please */
                end_session(sk);

        } else if (strncmp(argv[1], "get", 3) == 0) {

                /***********************/
                /***  RECEIVER MODE  ***/
                /***********************/

                /* Open connection */
                if (strcmp (argv[1], "get") == 0) {
                        if (argc < 3)
                                help(argv[0]);
                        sk = open_connection_client(argv[2], port);
                } else if (strcmp(argv[1], "getserv") == 0) {
                        sk = open_connection_server(port);
                } else {
                        help(argv[0]);
                }

                /* Adjust receive buffer */
                i = CANUTE_BLOCK_SIZE;
                setsockopt(sk, SOL_SOCKET, SO_RCVBUF, CCP_CAST &i, sizeof(i));

                while (receive_item(sk))
                        /* Just receive until end notification */;

        } else {
                help(argv[0]);
        }

        close_connection(sk);
        return EXIT_SUCCESS;
}


/**********************  MISCELLANEOUS HELPER FUNCTIONS  **********************/

/*
 * help
 *
 * Show command syntax and exit not successfully.
 */
void
help (char *argv0)
{
        printf("Canute " CANUTE_VERSION_STR "\n\n"
               "Syntax:\n"
               "\t%s send[:port]   <file/directory> [<file/directory> ...]\n"
               "\t%s get[:port]    <host/IP>\n"
               "\t%s sendto[:port] <host/IP> <file/directory> [<file/directory> ...]\n"
               "\t%s getserv[:port]\n", argv0, argv0, argv0, argv0);
        exit(EXIT_FAILURE);
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
        char    s[128];

        fputs("ERROR: ", stderr);
        va_start(pars, msg);
        vsnprintf(s, 128, msg, pars);
        va_end(pars);
        perror(s);
}


/*
 * fatal
 * 
 * Fatal error. Same as error() but also exit failing (aborting).
 */
void
fatal (char *msg, ...)
{
        va_list pars;
        char    s[128];

        fputs("FATAL ERROR: ", stderr);
        va_start(pars, msg);
        vsnprintf(s, 128, msg, pars);
        va_end(pars);
        perror(s);
        exit(EXIT_FAILURE);
}

