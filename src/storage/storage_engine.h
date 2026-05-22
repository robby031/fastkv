#ifndef FASTKV_STORAGE_ENGINE_H
#define FASTKV_STORAGE_ENGINE_H

#include "fastkv/types.h"
#include "fastkv/error.h"

/*
 * Abstract storage engine vtable.
 *
 * A concrete engine (e.g. the lock-free hashtable or a future LSM-tree)
 * fills in this struct and registers itself.  The transaction manager and
 * public API layer call through this interface so the backend is swappable.
 */

typedef struct fastkv_engine fastkv_engine_t;

typedef struct {
    /* lifecycle */
    fastkv_err_t (*open)(fastkv_engine_t **engine, const fastkv_opts_t *opts);
    fastkv_err_t (*close)(fastkv_engine_t *engine);

    /* point operations — called from within a committed transaction */
    fastkv_err_t (*get)(fastkv_engine_t *engine,
                         fastkv_ts_t       snapshot_ts,
                         fastkv_slice_t    key,
                         fastkv_slice_t   *value_out);

    fastkv_err_t (*put)(fastkv_engine_t *engine,
                         fastkv_ts_t       commit_ts,
                         fastkv_slice_t    key,
                         fastkv_slice_t    value);

    fastkv_err_t (*del)(fastkv_engine_t *engine,
                         fastkv_ts_t       commit_ts,
                         fastkv_slice_t    key);

    /* ordered iteration (for cursor + range-scan support) */
    fastkv_err_t (*iter_create)(fastkv_engine_t  *engine,
                                 fastkv_ts_t       snapshot_ts,
                                 void            **iter_out);
    fastkv_err_t (*iter_seek)(void *iter, fastkv_slice_t key);
    fastkv_err_t (*iter_next)(void *iter);
    fastkv_err_t (*iter_key)(void *iter, fastkv_slice_t *key_out);
    fastkv_err_t (*iter_value)(void *iter, fastkv_slice_t *value_out);
    void         (*iter_destroy)(void *iter);

    /* stats / maintenance */
    uint64_t     (*num_keys)(fastkv_engine_t *engine);
    fastkv_err_t (*compact)(fastkv_engine_t *engine);  /* remove deleted/expired versions */
} fastkv_engine_ops_t;

struct fastkv_engine {
    const fastkv_engine_ops_t *ops;
    void                      *impl;   /* engine-private state */
};

/* Built-in engine descriptors */
extern const fastkv_engine_ops_t fastkv_engine_hashtable;  /* lock-free concurrent hashtable */

#endif /* FASTKV_STORAGE_ENGINE_H */
