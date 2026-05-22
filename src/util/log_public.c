#define _POSIX_C_SOURCE 200809L

/* Implementasi API log publik. File ini hanya include public header agar tidak
 * bentrok dengan enum internal fastkv_log_level_t di log.h. */

#include "fastkv.h"

/* g_level dideklarasikan di log.c — akses via fungsi internal */
void fastkv_log_set_level_int(int raw_level);

void fastkv_set_log_level(int level) {
    fastkv_log_set_level_int(level >= FASTKV_LOG_SILENT ? 255 : level);
}
