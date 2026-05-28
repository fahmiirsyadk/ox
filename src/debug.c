#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "debug.h"

int oxbar_debug = 0;

void debug_log(const char *fmt, ...)
{
    if (!oxbar_debug)
        return;

    static FILE *f = NULL;
    if (f == NULL) {
        f = fopen("/tmp/oxbar-debug.log", "a");
        if (f == NULL)
            return;
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    fprintf(f, "%02d:%02d:%02d ", tm->tm_hour, tm->tm_min, tm->tm_sec);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fflush(f);
}
