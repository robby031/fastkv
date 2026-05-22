#ifndef FASTKV_WAL_H
#define FASTKV_WAL_H

#include "fastkv/types.h"
#include "fastkv/error.h"

#include <stdint.h>

/*
 * Write-Ahead Log (WAL)
 *
 * Record format (from DESIGN.md §4.1):
 *
 * ┌──────────┬──────────┬──────────┬──────────┬─────────────┬──────────────┐
 * │ CRC32(4) │  TS (8)  │ KLen (4) │ VLen (4) │ Key (var) │ Value (var)  │
 * └──────────┴──────────┴──────────┴──────────┴─────────────┴──────────────┘
 *
 * A VLen of UINT32_MAX signals a deletion (tombstone) record.
 * Records are appended in commit order; fsync frequency is controlled by
 * opts->sync_writes.
 */

#define WAL_VLEN_TOMBSTONE UINT32_MAX

typedef struct fastkv_wal fastkv_wal_t;

/* Record types (written as first byte after CRC header) */
typedef enum {
 WAL_REC_PUT = 0x01,
 WAL_REC_DELETE = 0x02,
 WAL_REC_COMMIT = 0x03, /* commit marker — only timestamp, no key/value */
 WAL_REC_BEGIN  = 0x04, /* optional begin marker for crash analysis */
} fastkv_wal_rec_type_t;

typedef struct {
 uint32_t crc;
 fastkv_ts_t ts;
 fastkv_wal_rec_type_t  type;
 fastkv_slice_t  key;
 fastkv_slice_t  value; /* .data == NULL → tombstone */
} fastkv_wal_record_t;

/* Lifecycle */
fastkv_err_t fastkv_wal_open(fastkv_wal_t **wal, const char *dir, bool sync_writes);
fastkv_err_t fastkv_wal_close(fastkv_wal_t *wal);

/* Writing */
fastkv_err_t fastkv_wal_append(fastkv_wal_t  *wal, fastkv_wal_rec_type_t type, fastkv_ts_t ts, fastkv_slice_t key, fastkv_slice_t value);

fastkv_err_t fastkv_wal_sync(fastkv_wal_t *wal);

/* Recovery — iterate records from disk */
typedef fastkv_err_t (*fastkv_wal_replay_fn)(const fastkv_wal_record_t *rec, void *udata);

fastkv_err_t fastkv_wal_replay(const char  *dir, fastkv_wal_replay_fn fn, void *udata);

/* Maintenance */
fastkv_err_t fastkv_wal_rotate(fastkv_wal_t *wal); /* start new segment */
uint64_t fastkv_wal_bytes_written(fastkv_wal_t *wal);

#endif /* FASTKV_WAL_H */
