/* srec.h - Relocatable S-Record (Wintek extended) reader/writer
 *
 * This is the key to the toolchain: A9 emits these, XLD consumes .rel + .lib
 * of this form, and final output is ordinary absolute S-records.
 */

#ifndef SREC_H
#define SREC_H

#include <stdio.h>
#include <stdint.h>

typedef enum {
    SREC_S0 = 0,
    SREC_S1 = 1,
    SREC_S2 = 2,
    SREC_S3 = 3,
    SREC_S4 = 4,   /* Wintek extension: section/alloc metadata */
    SREC_S5 = 5,
    SREC_S6 = 6,
    SREC_S7 = 7,
    SREC_S8 = 8,   /* Wintek extension: symbol export (name + local offset + attrs) */
    SREC_S9 = 9,
    SREC_SD = 13   /* Wintek extension: trailer/summary (SD) */
} srec_type_t;

typedef struct {
    srec_type_t type;
    uint32_t    addr;     /* record address field (16/24/32 depending) */
    uint8_t     data[256];
    int         len;      /* payload len */
    uint8_t     cksum;    /* computed or read */
} srec_t;

/* Open a text S-rec file (CRLF tolerant). Returns FILE* or NULL. */
FILE *srec_open_read(const char *path);

/* Read next record. Returns 1 on success, 0 on EOF, -1 on error (bad cks, format). */
int srec_read(FILE *f, srec_t *out);

/* Write helpers (use CRLF to match original outputs). */
int srec_write(FILE *f, const srec_t *rec);

/* Convenience to emit a well-formed record (computes len + checksum). */
int srec_emit(FILE *f, srec_type_t typ, uint32_t addr, const uint8_t *data, int dlen);

/* Standard checksum (one's complement of sum of bytes after type). */
uint8_t srec_checksum(const uint8_t *bytes, int n);

/* For writing relocatable objects from the assembler. */
typedef struct srec_writer srec_writer_t;
srec_writer_t *srec_writer_open(const char *path, const char *modulename);
void srec_writer_close(srec_writer_t *w);

/* High-level emits used by A9. */
void srec_w_header(srec_writer_t *w, const char *name);
void srec_w_section_meta(srec_writer_t *w, int tag, uint16_t val1, uint16_t val2);
void srec_w_symbol(srec_writer_t *w, const char *name, int section_tag, uint16_t local_off, int flags);
void srec_w_data_chunk(srec_writer_t *w, int section_tag, uint16_t chunk_off, const uint8_t *bytes, int n);
void srec_w_chunk_end(srec_writer_t *w, const char *name, int section_tag, uint16_t info);
void srec_w_trailer(srec_writer_t *w, int tag, uint16_t info);
void srec_w_end(srec_writer_t *w, uint16_t entry);

#endif /* SREC_H */
