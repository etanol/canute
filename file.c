/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                            FILESYSTEM FUNCTIONS                            */
/*                                                                            */
/******************************************************************************/

#include "canute.h"

#ifdef HASEFROCH
/*
 * Seems that this prototype is missing in MinGW headers. Even though the symbol
 * exists and is linkable.
 */
extern int _stat64 (const char *path, struct __stat64 *buffer);
#endif


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
        int p = strlen(path) - 1;

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


/*
 * file_size
 *
 * Return path's size in bytes. Report a zero size in case of an error (file
 * unavailable, permission denied, etc).
 *
 * It comes handy to return 0 as an error because then empty files (with zero
 * size) are treated as non-existent, then skipped by the sender. From the
 * pragmatic point of view is desirable because files without content don't have
 * any interest.
 *
 * From the receiver perspective it is also handy because if the file doesn't
 * exist then the whole file is requested (zero initial offset).
 */
int64_t
file_size (char *path)
{
        int e;
#ifdef HASEFROCH
        struct __stat64 st;
        e = _stat64(path, &st);
#else
        struct stat st;
        e = stat(path, &st);
#endif

        return (e == -1 ? 0LL : (int64_t) st.st_size);
}


/*
 * change_wd
 *
 * Change working directory, similar to "cd" command. Abort on error. Specially
 * important if used just after a try_mkdir() because its silent errors will be
 * noticeable here.
 */
void
change_wd (char *path)
{
        int e;

        e = chdir(path);
        if (e == -1)
                fatal("Changing current directory to '%s'", path);
}


/*
 * try_mkdir
 *
 * Create a directory. But fail silently if it couldn't be created.
 */
void
try_mkdir (char *path)
{
#ifdef HASEFROCH
        mkdir(path);
#else
        mkdir(path, 0755);
#endif
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

        pos = (fpos_t) offset;
        e   = fsetpos(stream, &pos);
        if (e != 0)
                return -1;

        return 0;
}
#endif

