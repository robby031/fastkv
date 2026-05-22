#ifndef FASTKV_PERSIST_SNAPSHOT_H
#define FASTKV_PERSIST_SNAPSHOT_H

#include "fastkv/types.h"
#include "fastkv/error.h"

/*
 * Snapshot — consistent point-in-time dump of the in-memory state.
 *
 * Format (memory-mapped):
 *
 * ┌──────────────────────────────────┐
 * │ Magic (4B) | Version (2B) │
 * │ Timestamp (8B)  │
 * │ Num keys (8B) │
 * │ Data checksum CRC32C (4B) │
 * │ Bucket array offset (8B)  │
 * ├──────────────────────────────────┤
 * │ Bucket array  [N × 8B offsets] │
 * ├──────────────────────────────────┤
 * │ Data region │
 * │ klen(4) | vlen(4) | key | val  │
 * │ ... repeated ... │
 * └──────────────────────────────────┘
 */

#define SNAPSHOT_MAGIC 0x464B5350UL /* "FKSP" */
#define SNAPSHOT_VERSION 1

typedef struct fastkv_snapshot fastkv_snapshot_t;

typedef struct {
 uint32_t magic;
 uint16_t version;
 uint16_t _pad;
 uint64_t ts;
 uint64_t num_keys;
 uint32_t data_crc;
 uint32_t _pad2;
 uint64_t bucket_offset;
 uint64_t data_offset;
} fastkv_snapshot_hdr_t;

/* Write a snapshot of the current engine state to disk */
fastkv_err_t fastkv_snapshot_write(const char *dir,  fastkv_ts_t ts,  struct fastkv_db *db);

/* Load the most recent snapshot and replay into the engine */
fastkv_err_t fastkv_snapshot_load(const char  *dir, struct fastkv_db *db, fastkv_ts_t *ts_out);

#endif /* FASTKV_PERSIST_SNAPSHOT_H */
