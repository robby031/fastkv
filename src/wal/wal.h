#ifndef FASTKV_WAL_H
#define FASTKV_WAL_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include "util/uuid7/uuid7.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Write-Ahead Log (WAL)
 * VLen == WAL_VLEN_TOMBSTONE (UINT32_MAX) signals a deletion record.
 * Segments are named  wal-NNNNNNNNNNNNNNNNNNNN.log  (20-digit zero-padded ID).
 */

#define WAL_VLEN_TOMBSTONE UINT32_MAX
#define WAL_HEADER_SIZE (4 + 1 + 8 + 4 + 4) /* bytes before key payload */

typedef struct fastkv_wal fastkv_wal_t;

typedef enum {
    WAL_REC_PUT    = 0x01,
    WAL_REC_DELETE = 0x02,
    WAL_REC_COMMIT = 0x03,
    WAL_REC_BEGIN  = 0x04,
} fastkv_wal_rec_type_t;

typedef struct {
    uint32_t              crc;
    fastkv_ts_t           ts;
    fastkv_wal_rec_type_t type;
    fastkv_slice_t        key;
    fastkv_slice_t        value; /* .data == NULL → tombstone */
} fastkv_wal_record_t;

/* callback dipanggil setelah setiap record ditulis ke buffer WAL */
typedef void (*fastkv_wal_hook_fn)(const void *data, size_t len, void *udata);

/* Lifecycle */
fastkv_err_t fastkv_wal_open(
    fastkv_wal_t **wal, const char *dir, bool sync_writes, uuid7_ctx *uuid7);
fastkv_err_t fastkv_wal_close(fastkv_wal_t *wal);

/* Pasang hook replikasi — dipanggil dari dalam WAL lock */
void fastkv_wal_set_hook(fastkv_wal_t *wal, fastkv_wal_hook_fn fn, void *udata);

/* Writing */
fastkv_err_t fastkv_wal_append(fastkv_wal_t *wal, fastkv_wal_rec_type_t type, fastkv_ts_t ts,
    fastkv_slice_t key, fastkv_slice_t value);

fastkv_err_t fastkv_wal_sync(fastkv_wal_t *wal);

/* Recovery */

typedef fastkv_err_t (*fastkv_wal_replay_fn)(const fastkv_wal_record_t *rec, void *udata);

/*
 * Replay all WAL segments in dir whose records have ts > since_ts.
 * Stops cleanly at any torn/partial record at the crash boundary.
 * Returns the highest ts seen (useful for advancing the oracle clock).
 */
fastkv_err_t fastkv_wal_replay(const char *dir, fastkv_ts_t since_ts, fastkv_wal_replay_fn fn,
    void *udata, fastkv_ts_t *max_ts_out);

/* Maintenance */
fastkv_err_t fastkv_wal_rotate(fastkv_wal_t *wal, uuid7_ctx *uuid7);

/* Hapus segmen yang namanya secara leksikografis lebih kecil dari keep_from_name. */
fastkv_err_t fastkv_wal_trim(const char *dir, const char *keep_from_name);

/* Salin nama segment aktif (32 char hex + null) ke buf. */
void     fastkv_wal_current_segment(fastkv_wal_t *wal, char *buf, size_t cap);
uint64_t fastkv_wal_bytes_written(fastkv_wal_t *wal);

#endif /* FASTKV_WAL_H */
