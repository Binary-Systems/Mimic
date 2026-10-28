/* sections.c - section and memory allocation manager (shared by a9 + xld) */
#include "sections.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>

typedef struct {
    char name[32];
    int type;
    unsigned start, end;
} sec_t;

static sec_t secs[32];
static int nsec = 0;

int sections_init(void) {
    nsec = 0;
    /* Preload the common ones observed: PSCT DSCT BSCT ASCT */
    section_define("PSCT", 2, 0, 0xffff);
    section_define("DSCT", 3, 0, 0xffff);
    section_define("BSCT", 1, 0, 0xff);
    section_define("ASCT", 4, 0, 0xffff);
    return 0;
}

int section_define(const char *name, int type, unsigned start, unsigned end) {
    if (nsec >= 32) return -1;
    strncpy(secs[nsec].name, name ? name : "?", sizeof(secs[nsec].name)-1);
    secs[nsec].type = type;
    secs[nsec].start = start;
    secs[nsec].end = end;
    nsec++;
    return 0;
}

int section_lookup(const char *name, int *type, unsigned *start, unsigned *end) {
    for (int i = 0; i < nsec; i++) {
        if (strcasecmp(secs[i].name, name) == 0) {
            if (type) *type = secs[i].type;
            if (start) *start = secs[i].start;
            if (end) *end = secs[i].end;
            return 0;
        }
    }
    return -1;
}
