/* screen.h — tty screen editor for Mimic SuperMon.
 * A pipe keeps the cooked line reader. See screen.c for the terminal set. */

#ifndef MIMIC09_SCREEN_H
#define MIMIC09_SCREEN_H

#include <stddef.h>

/* 1 when stdin and stdout are terminals and the editor owns the glass. */
int screen_open(void);
void screen_close(void);
int screen_is_active(void);

/* Cooked mode for a program run. screen_resume redraws the monitor buffer. */
void screen_suspend(void);
void screen_resume(void);

/* After a host command that wrote on the glass. Keeps that output when the
 * terminal answers a cursor-position report; otherwise redraws. */
void screen_after_external(void);

void screen_feed(const char *s, size_t n);

/* Bytes on one memory line. 8 when the editor is off. 0 when the window
 * is too narrow for even one byte. */
int screen_mem_bytes(void);

/* 1 and a newline-terminated command, or 0 on EOF.
 * Empty input stays in the editor. */
int screen_readline(char *buf, size_t n);

/* Rest of the open line (the assemble prompt). 1, or 0 on EOF.
 * A blank line is "\n". */
int screen_read_field(char *buf, size_t n);

#endif
