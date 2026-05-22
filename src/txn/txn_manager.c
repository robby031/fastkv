#include "txn_manager.h"
#include "mem/allocator.h"
#include "util/log.h"

#include <inttypes.h>
#include <string.h>

/* forward decl */
struct fastkv_db;
fastkv_err_t fastkv_db_apply_write_set(struct fastkv_db *db, fastkv_write_entry_t *head, fastkv_ts_t commit_ts);
fastkv_err_t fastkv_db_check_conflicts(struct fastkv_db *db, fastkv_write_entry_t *head, fastkv_ts_t begin_ts);

/* Oracle */
fastkv_err_t fastkv_txn_mgr_init(fastkv_txn_mgr_t *mgr)
{
 fastkv_oracle_init(&mgr->oracle);
 atomic_init(&mgr->num_committed, 0);
 atomic_init(&mgr->num_aborted, 0);
 atomic_init(&mgr->num_conflicts, 0);
 return FASTKV_OK;
}

void fastkv_txn_mgr_destroy(fastkv_txn_mgr_t *mgr)
{
 (void)mgr;
}

/* Begin */
fastkv_err_t fastkv_txn_mgr_begin(fastkv_txn_mgr_t *mgr, struct fastkv_db *db, bool read_only, fastkv_txn_t **txn_out)
{
 fastkv_txn_t *txn = fkv_malloc(sizeof(*txn));
 if (!txn) return FASTKV_ERR_NOMEM;

 txn->arena = fastkv_arena_create(FASTKV_ARENA_DEFAULT_BLOCK);
 if (!txn->arena) { fkv_free(txn); return FASTKV_ERR_NOMEM; }

 txn->begin_ts = fastkv_oracle_begin(&mgr->oracle);
 txn->commit_ts = FASTKV_TS_INVALID;
 txn->state = TXN_ACTIVE;
 txn->read_only = read_only;
 txn->write_head  = NULL;
 txn->write_count = 0;
 txn->db  = db;

 *txn_out = txn;
 LOG_TRACE("TXN begin ts=%" PRIu64 " ro=%d", txn->begin_ts, read_only);
 return FASTKV_OK;
}

/* Commit */
fastkv_err_t fastkv_txn_mgr_commit(fastkv_txn_mgr_t *mgr, fastkv_txn_t *txn)
{
 if (txn->state != TXN_ACTIVE) return FASTKV_ERR_TXN_CLOSED;

 fastkv_err_t rc = FASTKV_OK;

 if (!txn->read_only && txn->write_head) {
 /* 1. Check write-write conflicts */
 rc = fastkv_db_check_conflicts(txn->db, txn->write_head, txn->begin_ts);
 if (rc == FASTKV_ERR_TXN_CONFLICT) {
 atomic_fetch_add(&mgr->num_conflicts, 1);
 goto abort;
 }
 if (rc != FASTKV_OK) goto abort;

 /* 2. Assign commit timestamp */
 txn->commit_ts = fastkv_oracle_commit(&mgr->oracle);

 /* 3. Apply to storage engine (WAL + in-memory) */
 rc = fastkv_db_apply_write_set(txn->db, txn->write_head, txn->commit_ts);
 if (rc != FASTKV_OK) goto abort;
 }

 txn->state = TXN_COMMITTED;
 atomic_fetch_add(&mgr->num_committed, 1);
 fastkv_oracle_release(&mgr->oracle, txn->begin_ts);
 fastkv_arena_destroy(txn->arena);
 fkv_free(txn);
 return FASTKV_OK;

abort:
 return fastkv_txn_mgr_abort(mgr, txn);
}

/* Abort */
fastkv_err_t fastkv_txn_mgr_abort(fastkv_txn_mgr_t *mgr, fastkv_txn_t *txn)
{
 if (txn->state == TXN_ABORTED) return FASTKV_OK;

 txn->state = TXN_ABORTED;
 atomic_fetch_add(&mgr->num_aborted, 1);
 fastkv_oracle_release(&mgr->oracle, txn->begin_ts);
 fastkv_arena_destroy(txn->arena);
 fkv_free(txn);
 return FASTKV_OK;
}

/* Write buffer operations */
static fastkv_err_t write_entry_add(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value)
{
 if (txn->read_only)  return FASTKV_ERR_TXN_RO;
 if (txn->state != TXN_ACTIVE) return FASTKV_ERR_TXN_CLOSED;

 fastkv_write_entry_t *e = fastkv_arena_alloc(txn->arena, sizeof(*e));
 if (!e) return FASTKV_ERR_NOMEM;

 e->key = FASTKV_SLICE(fastkv_arena_dup(txn->arena, key.data, key.len), key.len);
 e->value = value.data
 ? FASTKV_SLICE(fastkv_arena_dup(txn->arena, value.data, value.len), value.len)
 : FASTKV_SLICE_NULL;

 if (!e->key.data) return FASTKV_ERR_NOMEM;

 e->next  = txn->write_head;
 txn->write_head  = e;
 txn->write_count++;
 return FASTKV_OK;
}

fastkv_err_t fastkv_txn_write_put(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value)
{
 return write_entry_add(txn, key, value);
}

fastkv_err_t fastkv_txn_write_delete(fastkv_txn_t *txn, fastkv_slice_t key)
{
 return write_entry_add(txn, key, FASTKV_SLICE_NULL);
}

fastkv_err_t fastkv_txn_write_lookup(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t *value_out)
{
 for (fastkv_write_entry_t *e = txn->write_head; e; e = e->next) {
 if (e->key.len == key.len && memcmp(e->key.data, key.data, key.len) == 0) {
 if (!e->value.data) return FASTKV_ERR_NOTFOUND;  /* pending delete */
 *value_out = e->value;
 return FASTKV_OK;
 }
 }
 return FASTKV_ERR_NOTFOUND;
}
