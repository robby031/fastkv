#ifndef FASTKV_TYPES_H
#define FASTKV_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Opaque handles ──────────────────────────────────────────────────────── */
typedef struct fastkv_db     fastkv_db_t;
typedef struct fastkv_txn    fastkv_txn_t;
typedef struct fastkv_cursor fastkv_cursor_t;
typedef struct fastkv_index  fastkv_index_t;

/* ── Slice — non-owning view over a byte buffer ──────────────────────────── */
typedef struct {
    const uint8_t *data;
    size_t         len;
} fastkv_slice_t;

#define FASTKV_SLICE(ptr, n)  ((fastkv_slice_t){ .data = (const uint8_t *)(ptr), .len = (n) })
#define FASTKV_STR(s)         FASTKV_SLICE((s), strlen(s))
#define FASTKV_SLICE_NULL     ((fastkv_slice_t){ .data = NULL, .len = 0 })

/* ── Database configuration ──────────────────────────────────────────────── */
typedef struct {
    const char *path;          /* directory for WAL and snapshot files */
    size_t      map_size;      /* initial hash-map capacity (number of slots) */
    size_t      arena_size;    /* memory arena size per transaction (bytes) */
    bool        sync_writes;   /* fsync WAL on every commit (safe, slower) */
    bool        read_only;     /* open in read-only mode */

    /* Allocator override — NULL to use system malloc/jemalloc */
    void *(*malloc_fn)(size_t);
    void  (*free_fn)(void *);
} fastkv_opts_t;

/* Sensible defaults — override individual fields as needed */
#define FASTKV_OPTS_DEFAULT \
    ((fastkv_opts_t){       \
        .path         = ".",\
        .map_size     = 1024 * 1024, \
        .arena_size   = 64  * 1024,  \
        .sync_writes  = true,        \
        .read_only    = false,       \
        .malloc_fn    = NULL,        \
        .free_fn      = NULL,        \
    })

/* ── Cursor direction ────────────────────────────────────────────────────── */
typedef enum {
    FASTKV_CURSOR_FORWARD  = 0,
    FASTKV_CURSOR_BACKWARD = 1,
} fastkv_cursor_dir_t;

/* ── Timestamp (MVCC) ────────────────────────────────────────────────────── */
typedef uint64_t fastkv_ts_t;

#define FASTKV_TS_INVALID UINT64_MAX
#define FASTKV_TS_MAX     (UINT64_MAX - 1)

/* ── Key/value size limits ───────────────────────────────────────────────── */
#define FASTKV_MAX_KEY_LEN   (4u * 1024u)         /* 4 KiB */
#define FASTKV_MAX_VAL_LEN   (64u * 1024u * 1024u) /* 64 MiB */

#endif /* FASTKV_TYPES_H */
