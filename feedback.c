/******************************************************************************/
/*                ____      _      _   _   _   _   _____   _____              */
/*               / ___|    / \    | \ | | | | | | |_   _| | ____|             */
/*              | |       / _ \   |  \| | | | | |   | |   |  _|               */
/*              | |___   / ___ \  | |\  | | |_| |   | |   | |___              */
/*               \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|             */
/*                                                                            */
/*                         OUTPUT FEEDBACK FUNCTIONS                          */
/*                                                                            */
/******************************************************************************/

#include "canute.h"

#define BAR_REFRESH_DELAY 1000
#define BAR_DATA_WIDTH    47
#define BAR_DEFAULT_WIDTH 80
#define BAR_MINIMUM_WIDTH (BAR_DATA_WIDTH + 4)


/****************  PRIVATE DATA (Progress state information)  ****************/

static long long      total_size;
static long long      completed_size;
static long long      initial_offset;
static int            delta_index;
static int            delta_bytes[8];
static int            delta_msecs[8];
static char           bar[512];       /* A reasonable unreachable value */
static struct timeval init_time;
static struct timeval last_time;


/****************************  PRIVATE FUNCTIONS  ****************************/

/*
 * query_terminal_width
 *
 * Return the number of columns in the current terminal, so we can fit better
 * the progress bar. Code stolen from GNU Wget.
 */
static int query_terminal_width (void)
{
        int w = BAR_DEFAULT_WIDTH;
#ifdef HASEFROCH
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi))
                w = csbi.dwSize.X;
#else
        struct winsize wsz;

        if (ioctl(fileno(stderr), TIOCGWINSZ, &wsz) != -1)
                w = wsz.ws_col;
#endif
        return (w < BAR_MINIMUM_WIDTH ? BAR_MINIMUM_WIDTH : w);
}


/*
 * timeval_diff_in_millis
 *
 * Calculate the time difference between two timeval structures in miliseconds
 * and return the value as an integer number.  The value will bi positive if
 * t1 > t2, that is, t1 is later than t2.
 */
static int timeval_diff_in_millis (const struct timeval *t1,
                                   const struct timeval *t2)
{
    return (t1->tv_sec  - t2->tv_sec)  * 1000 +
           (t1->tv_usec - t2->tv_usec) / 1000;
}


#ifdef HASEFROCH
/*
 * gettimeofday
 *
 * Simulate gettimeofday within win32 platforms.  This procedure is mainly
 * obtained from glib2/glib/gmain.c with a slight modification to avoid a
 * compilation warning.  The resulting assembler code is exactly the same.
 */
static void gettimeofday (struct timeval *time, void *dummy)
{
        union {
                FILETIME as_ft;
                uint64_t as_long;
        } ft;

        GetSystemTimeAsFileTime(&ft.as_ft);
        ft.as_long   -= 116444736000000000ULL;
        ft.as_long   /= 10;
        time->tv_sec  = ft.as_long / 1000000;
        time->tv_usec = ft.as_long % 1000000;
}
#endif /* HASEFROCH */


/*
 * pretty_number
 *
 * Return a beautified string representation of an integer. String will contain
 * thousand separators.
 */
static char *pretty_number (long long num)
{
        static char str[16];
        char        ugly[12];
        int         i, j;

#ifdef HASEFROCH
        i = snprintf(ugly, 12, "%I64d", num);
#else
        i = snprintf(ugly, 12, "%lld", num);
#endif
        j = i + ((i - 1) / 3);
        str[j] = '\0';
        do {
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ugly[--i]; if (i == 0) break;
                str[--j] = ',';
        } while (1);

        return str;
}


/*
 * pretty_time
 *
 * Return a beautified string representation of an integer holding a time value,
 * in seconds. String format is "hour:min:sec".
 */
static char *pretty_time (int secs)
{
        static char str[12];
        int         hour, min, sec;

        min  = secs / 60;
        sec  = secs % 60;
        hour = min / 60;
        min  = min % 60;

        if (hour > 99)
                strcpy(str, ">4 Days");
        else if (hour > 0)
                snprintf(str, 10, "%d:%02d:%02d", hour, min, sec);
        else
                snprintf(str, 6, "%d:%02d", min, sec);

        return str;
}


/*
 * pretty_speed
 *
 * Scale and convert a given transfer rate to a beautified string. Metrics are
 * changed when value is scaled.
 */
static char *pretty_speed (float rate)
{
        static char str[16];
        char       *metric;

        if (rate > 1024.0 * 1024.0 * 1024.0)
        {
                rate  /= 1024.0 * 1024.0 * 1024.0;
                metric = "G/s";
        }
        else if (rate > 1024.0 * 1024.0)
        {
                rate  /= 1024.0 * 1024.0;
                metric = "M/s";
        }
        else if (rate > 1024.0)
        {
                rate  /= 1024.0;
                metric = "K/s";
        }
        else
                metric = "B/s";

        snprintf(str, 16, "%4.1f %s", rate, metric);
        return str;
}


/*
 * draw_bar
 *
 * Calculate and displays the GNU Wget style progress bar. Then, this is mostly
 * stolen from GNU Wget too.
 *
 * This is the format:
 *
 * 999% [===...] 99,999,999,999 9999.9 X/s ETA 99:99:99
 *
 * Where each part needs:
 *
 *      999%            -->  4 chars + 1 space
 *      [===...]        -->  2 chars (and the remaining) + 1 space
 *      99,999,999,999  --> 14 chars + 1 space
 *      9999.9 X/s      -->  9 chars + 2 spaces
 *      ETA 99:99:99    --> 11 chars + 2 spaces
 *
 *      TOTAL           --> 40 chars + 7 spaces = 47
 *      (As defined by BAR_DATA_WIDTH)
 */
static void draw_bar (void)
{
        int   eta, bar_size = query_terminal_width() - BAR_DATA_WIDTH;
        int   bytes, msecs;
        float percent, fill, speed;
        float ofill; /* For initial offset */

        /* Some temporary calculations have to done in floating point
         * representation because of overflow issues */
        percent = ((float) completed_size / (float) total_size) * 100.0;
        fill    = ((float) bar_size * percent) / 100.0;

        memset(bar, ' ', bar_size);
        memset(bar, '=', (size_t) fill);
        if (initial_offset > 0)
        {
                ofill = ((float) initial_offset / (float) total_size)
                        * (float) bar_size;
                memset(bar, '+', (size_t) ofill);
        }
        bar[bar_size] = '\0';

        /* Caculate average speed and Estimated Time of Arrival.  Loops
         * unrolled */
        bytes =   delta_bytes[0] + delta_bytes[1] + delta_bytes[2]
                + delta_bytes[3] + delta_bytes[4] + delta_bytes[5]
                + delta_bytes[6] + delta_bytes[7];
        msecs =   delta_msecs[0] + delta_msecs[1] + delta_msecs[2]
                + delta_msecs[3] + delta_msecs[4] + delta_msecs[5]
                + delta_msecs[6] + delta_msecs[7];

        speed = (float) bytes / ((float) msecs * 1e-3);
        eta   = (int) ((float) (total_size - completed_size) / speed);

        /* Print all */
        printf("\r%3d%% [%s] %-14s %10s ETA %-8s", (int) percent, bar,
               pretty_number(completed_size), pretty_speed(speed),
               pretty_time(eta));
        fflush(stdout);
}


/*****************************  PUBLIC FUNCTIONS  *****************************/

/*
 * setup_progress
 *
 * Prepare progress output for a single file.
 */
void setup_progress (char *name, long long size, long long offset)
{
        int i;

        /* Initialize the delta arrays before every single transfer */
        memset(delta_bytes, 0, sizeof(int) * 8);
        for (i = 0;  i < 8;  i += 4)
        {
                delta_msecs[i]     = 1;
                delta_msecs[i + 1] = 1;
                delta_msecs[i + 2] = 1;
                delta_msecs[i + 3] = 1;
        }

        delta_index    = 0;
        total_size     = size;
        initial_offset = offset;
        completed_size = offset;

        printf("*** Transferring '%s' (%s bytes)\n", name, pretty_number(size));

        /* We watch the clock before and after the whole transfer to estimate an
         * average speed to be shown at the end. */
        gettimeofday(&init_time, NULL);
        last_time = init_time;
}


/*
 * show_progress
 *
 * Update the history ring and show amount of transfer and percentage.
 */
void update_progress (size_t increment)
{
        struct timeval now;
        int            delay;

        gettimeofday(&now, NULL);
        delay = timeval_diff_in_millis(&now, &last_time);

        delta_bytes[delta_index] += increment;
        completed_size           += increment;

        if (delay > BAR_REFRESH_DELAY)
        {
                delta_msecs[delta_index] = delay;

                delta_index++;
                delta_index &= 0x07;     /* delta_index %= 8; */
                last_time    = now;

                draw_bar();

                delta_bytes[delta_index] = 0;
        }
}


/*
 * finish_progress
 *
 * Show the average transfer rate and other general information.
 */
void finish_progress (void)
{
        struct timeval now;
        float          total_elapsed, av_rate;

        if (total_size == 0)
        {
                printf("\n");
                return;
        }

        gettimeofday(&now, NULL);

        total_elapsed = (float) timeval_diff_in_millis(&now, &init_time) * 1e-3;
        av_rate       = (float) (total_size - initial_offset) / total_elapsed;

        draw_bar();
        printf("\nCompleted %s bytes in %s (Average Rate: %s)\n\n",
               pretty_number(total_size - initial_offset),
               pretty_time(total_elapsed), pretty_speed(av_rate));
}

