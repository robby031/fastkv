#include "wal.h"
#include "mem/allocator.h"
#include "util/crc32.h"
#include "util/log.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#ifndef _WIN32
#  include <unistd.h>
#endif

#define WAL_FILENAME_FMT  "%s/wal-%020" PRIu64 ".log"
#define WAL_MAGIC         0x464B5741UL   /* "FKWA" */
#define WAL_VERSION       1

struct fastkv_wal {
    FILE    *file;
    char    *dir;
    bool     sync_writes;
    uint64_t bytes_written;
    uint64_t segment_id;
};

/* ── Internal helpers ────────────────────────────────────────────────────── */

static fastkv_err_t write_u8(FILE *f, uint8_t v)
{
    return fwrite(&v, 1, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

static fastkv_err_t write_u32(FILE *f, uint32_t v)
{
    return fwrite(&v, 4, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

static fastkv_err_t write_u64(FILE *f, uint64_t v)
{
    return fwrite(&v, 8, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

fastkv_err_t fastkv_wal_open(fastkv_wal_t **wal, const char *dir, bool sync_writes)
{
    fastkv_wal_t *w = fkv_malloc(sizeof(*w));
    if (!w) return FASTKV_ERR_NOMEM;

    w->dir          = fkv_malloc(strlen(dir) + 1);
    if (!w->dir) { fkv_free(w); return FASTKV_ERR_NOMEM; }
    strcpy(w->dir, dir);

    w->sync_writes  = sync_writes;
    w->bytes_written = 0;
    w->segment_id    = 0;

    char path[4096];
    snprintf(path, sizeof path, WAL_FILENAME_FMT, dir, w->segment_id);

    w->file = fopen(path, "ab");
    if (!w->file) {
        LOG_ERROR("WAL: failed to open %s", path);
        fkv_free(w->dir);
        fkv_free(w);
        return FASTKV_ERR_IO;
    }

    *wal = w;
    LOG_DEBUG("WAL opened: %s", path);
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_close(fastkv_wal_t *wal)
{
    if (!wal) return FASTKV_OK;
    if (wal->file) fclose(wal->file);
    fkv_free(wal->dir);
    fkv_free(wal);
    return FASTKV_OK;
}

/* ── Writing ─────────────────────────────────────────────────────────────── */

fastkv_err_t fastkv_wal_append(fastkv_wal_t         *wal,
                                fastkv_wal_rec_type_t type,
                                fastkv_ts_t           ts,
                                fastkv_slice_t        key,
                                fastkv_slice_t        value)
{
    uint32_t klen = (uint32_t)key.len;
    uint32_t vlen = (value.data == NULL) ? WAL_VLEN_TOMBSTONE : (uint32_t)value.len;

    /* Compute CRC over [type, ts, klen, vlen, key, value] */
    uint32_t crc = 0;
    crc = fastkv_crc32c(crc, &type, 1);
    crc = fastkv_crc32c(crc, &ts,   8);
    crc = fastkv_crc32c(crc, &klen, 4);
    crc = fastkv_crc32c(crc, &vlen, 4);
    if (key.len)   crc = fastkv_crc32c(crc, key.data,   key.len);
    if (value.len) crc = fastkv_crc32c(crc, value.data, value.len);

    FILE *f = wal->file;
    fastkv_err_t rc;

    if ((rc = write_u32(f, crc))         != FASTKV_OK) return rc;
    if ((rc = write_u8(f,  (uint8_t)type)) != FASTKV_OK) return rc;
    if ((rc = write_u64(f, ts))           != FASTKV_OK) return rc;
    if ((rc = write_u32(f, klen))         != FASTKV_OK) return rc;
    if ((rc = write_u32(f, vlen))         != FASTKV_OK) return rc;

    if (key.len   && fwrite(key.data,   1, key.len,   f) != key.len)   return FASTKV_ERR_IO;
    if (value.len && fwrite(value.data, 1, value.len, f) != value.len) return FASTKV_ERR_IO;

    size_t rec_size = 4 + 1 + 8 + 4 + 4 + key.len + value.len;
    wal->bytes_written += rec_size;

    if (wal->sync_writes) return fastkv_wal_sync(wal);
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_sync(fastkv_wal_t *wal)
{
    if (fflush(wal->file) != 0) return FASTKV_ERR_IO;
#ifndef _WIN32
    if (fsync(fileno(wal->file)) != 0) return FASTKV_ERR_IO;
#endif
    return FASTKV_OK;
}

/* ── Recovery ────────────────────────────────────────────────────────────── */

fastkv_err_t fastkv_wal_replay(const char         *dir,
                                fastkv_wal_replay_fn fn,
                                void               *udata)
{
    /* TODO: enumerate segment files in order and replay each record */
    (void)dir; (void)fn; (void)udata;
    LOG_WARN("WAL replay not yet implemented");
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_rotate(fastkv_wal_t *wal)
{
    fclose(wal->file);
    wal->segment_id++;

    char path[4096];
    snprintf(path, sizeof path, WAL_FILENAME_FMT, wal->dir, wal->segment_id);
    wal->file = fopen(path, "ab");
    if (!wal->file) return FASTKV_ERR_IO;
    LOG_INFO("WAL rotated to segment %" PRIu64, wal->segment_id);
    return FASTKV_OK;
}

uint64_t fastkv_wal_bytes_written(fastkv_wal_t *wal)
{
    return wal ? wal->bytes_written : 0;
}
