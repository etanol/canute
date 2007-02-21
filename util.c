/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                             UTILITY FUNCTIONS                              */
/*                                                                            */
/******************************************************************************/

#include "canute.h"


/*
 * basename
 *
 * Strip absolute path. Similar to UNIX "basename" command but with a somewhat
 * dirty implementation.
 */
char *
basename (char *path)
{
        int p = strlen(path) - 1;

        while (IS_PATH_SEPARATOR(path[p])) {
                path[p] = '\0';
                p--;
        }
        while (p > 0 && !IS_PATH_SEPARATOR(path[p]))
                p--;
        if (IS_PATH_SEPARATOR(path[p]))
                p++;
        return path + p;
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

        fputs("\nFATAL ERROR: ", stderr);
        va_start(pars, msg);
        vsnprintf(s, 128, msg, pars);
        va_end(pars);
        perror(s);
        exit(EXIT_FAILURE);
}


#ifdef HASEFROCH
/*
 * fseeko
 *
 * As fsetpos() is also implemented in UNIX, that function could have been used
 * instead. But fpos_t is a composite type (struct) rather than a 64 bit integer
 * as in Hasefroch.
 */
int
fseeko (FILE *stream, off_t offset, int whence)
{
        int    e;
        fpos_t pos;

        if (whence != SEEK_SET)
                return -1;

        pos = (fpos_t)offset;
        e   = fsetpos(stream, &pos);
        if (e != 0)
                return -1;

        return 0;
}
#endif


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

