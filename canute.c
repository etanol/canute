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

/* Working directory */
static char cwd_buf[PATH_MAX];


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
int main (int argc, char **argv)
{
        SOCKET          sk = -1; /* Quest for a warning free compilation */
        char           *port_str, *cwd;
        unsigned short  port;
        int             i, err, last, arg = 0;
#ifdef HASEFROCH
        WSADATA  ws;

        if (WSAStartup(MAKEWORD(1, 1), &ws))
                fatal("Starting WinSock");
        atexit ((void (*)()) WSACleanup);
#endif

        if (argc < 2)
                help(argv[0]);

        /* See if there is a port specification to override default */
        port     = CANUTE_DEFAULT_PORT;
        port_str = strchr(argv[1], ':');
        if (port_str != NULL)
        {
                *port_str = '\0';
                port_str++;
                port = (unsigned short) atoi(port_str);
        }

        if (strncmp(argv[1], "send", 4) == 0)
        {
                /*********************/
                /***  SENDER MODE  ***/
                /*********************/

                /* Open connection */
                if (strcmp(argv[1], "send") == 0)
                {
                        if (argc < 3)
                                help(argv[0]);
                        sk  = open_connection_server(port);
                        arg = 2;
                }
                else if (strcmp(argv[1], "sendto") == 0)
                {
                        if (argc < 4)
                                help(argv[0]);
                        sk  = open_connection_client(argv[2], port);
                        arg = 3;
                }
                else
                        help(argv[0]);

                /* Save current working directory */
                cwd = getcwd(cwd_buf, PATH_MAX);
                if (cwd == NULL)
                        error("Could not retrieve working directory."
                              " This may produce some path errors.\n");

                /* Adjust send buffer */
                i = CANUTE_BLOCK_SIZE;
                setsockopt(sk, SOL_SOCKET, SO_SNDBUF, CCP_CAST &i, sizeof(i));

                /* Now we have the transmission channel open, so let's send
                 * everything we're supposed to send */
                for (i = arg;  i < argc;  i++)
                {
                        send_item(sk, argv[i]);
                        /* Return to original working directory.  This fixes a
                         * potential bug when giving multiple arguments with
                         * different path prefixes. */
                        err = chdir(cwd);
                        if (err == -1)
                                error("Could not change working directory."
                                      " This may produce some path errors.\n");
                }

                /* It's over. Notify the receiver to finish as well, please */
                send_message(sk, REQUEST_END, 0, NULL);
        }
        else if (strncmp(argv[1], "get", 3) == 0)
        {
                /***********************/
                /***  RECEIVER MODE  ***/
                /***********************/

                /* Open connection */
                if (strcmp(argv[1], "get") == 0)
                {
                        if (argc < 3)
                                help(argv[0]);
                        sk = open_connection_client(argv[2], port);
                }
                else if (strcmp(argv[1], "getserv") == 0)
                        sk = open_connection_server(port);
                else
                        help(argv[0]);

                /* Adjust receive buffer */
                i = CANUTE_BLOCK_SIZE;
                setsockopt(sk, SOL_SOCKET, SO_RCVBUF, CCP_CAST &i, sizeof(i));

                do {
                        last = receive_item(sk);
                } while (!last);
        }
        else
                help(argv[0]);

        closesocket(sk);
        return EXIT_SUCCESS;
}

