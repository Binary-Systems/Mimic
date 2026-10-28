/* util.c */
#include "util.h"
#include <string.h>
#include <time.h>

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void banner(const char *tool, const char *ver, const char *copy) {
    /* Match style of original strings */
    printf("%s, Version %s\n", tool, ver);
    if (copy) printf("%s\n", copy);
}
