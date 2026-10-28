/* sections.h - section and memory allocation manager (shared by a9 + xld) */
#ifndef SECTIONS_H
#define SECTIONS_H

int sections_init(void);
int section_define(const char *name, int type, unsigned start, unsigned end);
int section_lookup(const char *name, int *type, unsigned *start, unsigned *end);

#endif /* SECTIONS_H */
