/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*               NETWORK MANAGEMENT AND TRANSMISSION FUNCTIONS                */
/*                                                                            */
/******************************************************************************/

#include "canute.h"


/*
 * open_connection_server
 *
 * Set up a connection in server mode. Opens the specified port for listening
 * and wait for someone to connect. Return the connected socket ready for
 * transmission.
 */
SOCKET
open_connection_server (uint16_t port)
{
        SOCKET             bsk, sk; /* bsk --> Binding SocKet */
        struct sockaddr_in saddr;
        int                e;

        bsk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bsk == INVALID_SOCKET) 
                fatal("Creating socket");

        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons(port);
        saddr.sin_addr.s_addr = INADDR_ANY;

        /* Ignore errors from setsockopt(), bind() will fail in that case */
        e = 1;
        setsockopt(bsk, SOL_SOCKET, SO_REUSEADDR, CCP_CAST &e, sizeof(e));

        e = bind(bsk, (SOCKADDR *) &saddr, sizeof(saddr));
        if (e == SOCKET_ERROR)
                fatal("Opening port %d", port);

        e = listen(bsk, 1);
        if (e == SOCKET_ERROR)
                fatal("Listening port %d", port);

        e  = sizeof(saddr);
        sk = accept(bsk, (SOCKADDR *) &saddr, (socklen_t *) &e);
        if (sk == INVALID_SOCKET)
                fatal("Accepting client connection");

        /* Binding socket not needed anymore */
        close_connection(bsk);
        return sk;
}


/*
 * open_connection_client
 *
 * Set up a connection in client mode. Try to connect to the specified host
 * (either a hostname or an IP address) and return the connected socket ready
 * for transmission.
 */
SOCKET
open_connection_client (char *host, uint16_t port)
{
        SOCKET             sk;
        struct sockaddr_in saddr;
        struct hostent    *he;
        int                e;

        sk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sk == INVALID_SOCKET) 
                fatal("Creating socket");

        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons(port);
        saddr.sin_addr.s_addr = inet_addr(host);
        if (saddr.sin_addr.s_addr == INADDR_NONE) {
                /* Invalid IP, maybe we have to do a DNS query */
                he = gethostbyname(host);
                if (he == NULL) {
#ifdef HASEFROCH
                        fatal("Invalid IP or hostname");
#else
                        herror("FATAL ERROR: Invalid IP or hostname");
                        exit(EXIT_FAILURE);
#endif
                }
                saddr.sin_addr.s_addr = ((struct in_addr *) he->h_addr)->s_addr;
        }

        /* Now we have a destination host */
        e = connect(sk, (SOCKADDR *) &saddr, sizeof(saddr));
        if (e == SOCKET_ERROR) 
                fatal("Connecting to host '%s'", host);

        return sk; 
}


/*
 * close_connection
 *
 * Trivial implementation to have consistency in socket manipulation. Close the
 * given connection.
 */
void
close_connection (SOCKET sk)
{
#ifdef HASEFROCH
        closesocket(sk);
#else
        close(sk);
#endif
}


/*
 * send_data
 *
 * Send count bytes over the connection. Doesn't finish until all of them are
 * sent. On error aborts.
 */
void
send_data (SOCKET sk, char *buf, size_t count)
{
        int s; /* Sent bytes in one send() call */

        while (count > 0) {
                s = send(sk, buf, count, 0);
                if (s == SOCKET_ERROR) 
                        fatal("Sending data");
                count -= s;
                buf   += s;
        }
}


/*
 * receive_data
 *
 * Receive count bytes from the connection. Doesn't finish until all bytes are
 * received. On error aborts.
 */
void
receive_data (SOCKET sk, char *buf, size_t count)
{
        int r; /* Received bytes in one recv() call */

        while (count > 0) {
                r = recv(sk, buf, count, 0);
                if (r == SOCKET_ERROR) 
                        fatal("Receiving data");
                count -= r;
                buf   += r;
        }
}

