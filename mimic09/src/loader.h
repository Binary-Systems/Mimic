/* loader.h — S-Record, Intel HEX, raw binary load/save */

#ifndef MIMIC09_LOADER_H
#define MIMIC09_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load a file. If force_addr != 0 and format is raw, load at that address.
 * Returns number of bytes loaded, or negative on error.
 */
int load_file(const char *path, uint16_t force_addr, int force);

/* Save region. fmt = "S19", "IHEX", "BIN"
 * Returns bytes written or negative.
 */
int save_file(const char *path, uint16_t start, uint16_t end, const char *fmt);

#ifdef __cplusplus
}
#endif
#endif
