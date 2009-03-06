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
SOCKET open_connection_server (unsigned short port)
{
        SOCKET             bsk, sk; /* bsk --> Binding SocKet */
        struct sockaddr_in saddr;
        int                e;
        socklen_t          alen;

        bsk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bsk == INVALID_SOCKET)
                fatal("Could not create socket");

        saddr.sin_family      = AF_INET;
        saddr.sin_port        = htons(port);
        saddr.sin_addr.s_addr = INADDR_ANY;

        /* Ignore errors from setsockopt(), bind() will fail in that case */
        e = 1;
        setsockopt(bsk, SOL_SOCKET, SO_REUSEADDR, CCP_CAST &e, sizeof(e));

        e = bind(bsk, (SOCKADDR *) &saddr, sizeof(saddr));
        if (e == SOCKET_ERROR)
                fatal("Could not open port %d", port);

        e = listen(bsk, 1);
        if (e == SOCKET_ERROR)
                fatal("Could not listen on port %d", port);

        alen = sizeof(saddr);
        sk   = accept(bsk, (SOCKADDR *) &saddr, &alen);
        if (sk == INVALID_SOCKET)
                fatal("Could not accept client connection");

        /* Binding socket not needed anymore */
        closesocket(bsk);
        return sk;
}


/*
 * open_connection_client
 *
 * Set up a connection in client mode. Try to connect to the specified host
 * (either a hostname or an IP address) and return the connected socket ready
 * for transmission.
 */
SOCKET open_connection_client (char *host, unsigned short port)
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
        if (saddr.sin_addr.s_addr == INADDR_NONE)
        {
                /* Invalid IP, maybe we have to do a DNS query */
                he = gethostbyname(host);
                if (he == NULL)
                {
#if defined(HASEFROCH) || defined(OMIT_HERROR)
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
 * send_data
 *
 * Send count bytes over the connection. Doesn't finish until all of them are
 * sent. On error aborts.
 */
void send_data (SOCKET sk, char *buf, size_t count)
{
        int s; /* Sent bytes in one send() call */

        do {
                s = send(sk, buf, count, 0);
                if (s == SOCKET_ERROR)
                        fatal("Sending data");
                count -= s;
                buf   += s;
        } while (count > 0);
}


/*
 * receive_data
 *
 * Receive count bytes from the connection. Doesn't finish until all bytes are
 * received. On error aborts.
 */
void receive_data (SOCKET sk, char *buf, size_t count)
{
        int r; /* Received bytes in one recv() call */

        do {
                r = recv(sk, buf, count, 0);
                if (r == SOCKET_ERROR)
                        fatal("Receiving data");
                count -= r;
                buf   += r;
        } while (count > 0);
}


/*
 * send_message
 *
 * Build a header packet and send it through the connection. All the fields are
 * converted to network byte order if required.
 */
void send_message (SOCKET    sk,
                   int       type,
                   int       is_executable,
                   int       mtime,
                   long long size,
                   char     *name)
{
        int           blocks, extra;
        struct header packet;

        blocks = (int) (size >> CANUTE_BLOCK_BITS);
        extra  = (int) (size &  CANUTE_BLOCK_MASK);

        /* Encode executable bit on the mtime filed;  never mind compatibility
         * because the remote side will ignore the mtime field */
        if (is_executable)
                mtime = -mtime;

        packet.type   = htonl(type);
        packet.mtime  = htonl(mtime);
        packet.blocks = htonl(blocks);  /* Read protocol.c for an explanation */
        packet.extra  = htonl(extra);

        if (name != NULL)
                strncpy(packet.name, name, CANUTE_NAME_LENGTH);
        else
                packet.name[0] = '\0';

        /* Mark the packet as enhanced version and send it */
        packet.name[CANUTE_NAME_LENGTH] = CANUTE_ENHANCED;
        send_data(sk, (char *) &packet, sizeof(struct header));
}


/*
 * receive_message
 *
 * Read from the connection expecting a header packet. Fix byte ordering if
 * necessary, fill the fields (if address was provided by the caller) and return
 * the message type.
 */
int receive_message (SOCKET     sk,
                     int       *is_executable,
                     int       *mtime,
                     long long *size,
                     char      *name)
{
        int           blocks, extra, pmtime = 0, is_x = 0;
        struct header packet;

        receive_data(sk, (char *) &packet, sizeof(struct header));

        if (packet.name[CANUTE_NAME_LENGTH] == CANUTE_ENHANCED)
        {
                /* Decode executable bit from the mtime field */
                pmtime = ntohl(packet.mtime);
                if (pmtime < 0)
                {
                        is_x   = !is_x;
                        pmtime = -pmtime;
                }
        }

        if (is_executable != NULL)
                *is_executable = is_x;

        if (mtime != NULL)
                *mtime = pmtime;

        if (size != NULL)
        {
                /* Read protocol.c for an explanation */
                blocks = ntohl(packet.blocks);
                extra  = ntohl(packet.extra);
                *size  = ((long long) blocks << 16) + extra;
        }

        if (name != NULL)
        {
                strncpy(name, packet.name, CANUTE_NAME_LENGTH);
                name[CANUTE_NAME_LENGTH] = '\0';
        }

        return ntohl(packet.type);
}

