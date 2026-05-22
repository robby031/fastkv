#include "snapshot.h"
#include "util/log.h"

fastkv_err_t fastkv_snapshot_write(const char  *dir,  fastkv_ts_t  ts,  struct fastkv_db *db)
{
 /* TODO: Phase 2 — serialize engine state to mmap'd file */
 (void)dir; (void)ts; (void)db;
 LOG_WARN("snapshot write not yet implemented");
 return FASTKV_OK;
}

fastkv_err_t fastkv_snapshot_load(const char  *dir, struct fastkv_db *db, fastkv_ts_t *ts_out)
{
 /* TODO: Phase 2 — find latest snapshot file and restore engine state */
 (void)dir; (void)db;
 if (ts_out) *ts_out = 0;
 LOG_WARN("snapshot load not yet implemented");
 return FASTKV_OK;
}
