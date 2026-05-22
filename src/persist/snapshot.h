#ifndef FASTKV_PERSIST_SNAPSHOT_H
#define FASTKV_PERSIST_SNAPSHOT_H

#include "fastkv/error.h"
#include "fastkv/types.h"

/*
 * Snapshot — consistent point-in-time dump of the in-memory state.
 * File naming:  snapshot-{ts:020}.bin
 * Binary format:
 */

#define SNAPSHOT_MAGIC 0x464B5350UL /* "FKSP" */
#define SNAPSHOT_VERSION 1

struct fastkv_db;

/* Write a consistent snapshot of the current engine state */
fastkv_err_t fastkv_snapshot_write(const char *dir, fastkv_ts_t ts, struct fastkv_db *db);

/*
 * Load the most recent snapshot into the engine.
 * Sets *ts_out to the snapshot timestamp (caller must advance the oracle
 * clock to at least this value before replaying WAL records).
 * Returns FASTKV_OK with *ts_out == 0 if no snapshot exists.
 */
fastkv_err_t fastkv_snapshot_load(const char *dir, struct fastkv_db *db, fastkv_ts_t *ts_out);

/* Delete snapshot files older than keep_ts */
fastkv_err_t fastkv_snapshot_trim(const char *dir, fastkv_ts_t keep_ts);

#endif /* FASTKV_PERSIST_SNAPSHOT_H */
