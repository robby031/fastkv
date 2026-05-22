#ifndef FASTKV_WAL_H
#define FASTKV_WAL_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include <stdbool.h>
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

/* Lifecycle */
fastkv_err_t fastkv_wal_open(fastkv_wal_t **wal, const char *dir, bool sync_writes);
fastkv_err_t fastkv_wal_close(fastkv_wal_t *wal);

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
fastkv_err_t fastkv_wal_rotate(fastkv_wal_t *wal);

/* Delete segments with ID strictly less than keep_from_id. */
fastkv_err_t fastkv_wal_trim(const char *dir, uint64_t keep_from_id);

uint64_t fastkv_wal_current_segment(fastkv_wal_t *wal);
uint64_t fastkv_wal_bytes_written(fastkv_wal_t *wal);

#endif /* FASTKV_WAL_H */
