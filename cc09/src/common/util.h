/* util.h - small shared helpers for cc09 tools */
#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void die(const char *fmt, ...);
void warn(const char *fmt, ...);
void banner(const char *tool, const char *ver, const char *copy);

#endif
