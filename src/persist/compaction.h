#ifndef FASTKV_PERSIST_COMPACTION_H
#define FASTKV_PERSIST_COMPACTION_H

#include "fastkv/error.h"

#include <stdbool.h>

struct fastkv_db;

/*
 * Checkpoint — atomic: snapshot + WAL trim + MVCC GC.
 *
 * Called explicitly via fastkv_sync() or by the background thread.
 * After a successful checkpoint, WAL segments older than the snapshot ts
 * are deleted and MVCC versions invisible to all active txns are freed.
 */
fastkv_err_t fastkv_checkpoint(struct fastkv_db *db);

/*
 * Background compaction thread — runs checkpoint periodically.
 * Started by fastkv_open(); stopped by fastkv_close().
 */
fastkv_err_t fastkv_compaction_start(struct fastkv_db *db);
void         fastkv_compaction_stop(struct fastkv_db *db);

#endif /* FASTKV_PERSIST_COMPACTION_H */
