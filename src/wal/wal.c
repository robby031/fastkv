#include "wal.h"

#include "mem/allocator.h"
#include "util/crc32.h"
#include "util/log.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define WAL_FILENAME_FMT "%s/wal-%020" PRIu64 ".log"
#define WAL_FILENAME_PFX "wal-"
#define WAL_FILENAME_SFX ".log"
#define WAL_SEG_ID_LEN 20 /* zero-padded decimal digits */

struct fastkv_wal {
    FILE    *file;
    char    *dir;
    bool     sync_writes;
    uint64_t bytes_written;
    uint64_t segment_id;
};

/* Low-level write helpers */

static fastkv_err_t write_u8(FILE *f, uint8_t v) {
    return fwrite(&v, 1, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

static fastkv_err_t write_u32(FILE *f, uint32_t v) {
    return fwrite(&v, 4, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

static fastkv_err_t write_u64(FILE *f, uint64_t v) {
    return fwrite(&v, 8, 1, f) == 1 ? FASTKV_OK : FASTKV_ERR_IO;
}

/* Low-level read helpers — return false on short read / EOF */

static bool read_u8(FILE *f, uint8_t *v) {
    return fread(v, 1, 1, f) == 1;
}

static bool read_u32(FILE *f, uint32_t *v) {
    return fread(v, 4, 1, f) == 1;
}

static bool read_u64(FILE *f, uint64_t *v) {
    return fread(v, 8, 1, f) == 1;
}

/* Lifecycle */

fastkv_err_t fastkv_wal_open(fastkv_wal_t **wal, const char *dir, bool sync_writes) {
    fastkv_wal_t *w = fkv_malloc(sizeof(*w));
    if (!w)
        return FASTKV_ERR_NOMEM;

    w->dir = fkv_malloc(strlen(dir) + 1);
    if (!w->dir) {
        fkv_free(w);
        return FASTKV_ERR_NOMEM;
    }
    strcpy(w->dir, dir);

    w->sync_writes   = sync_writes;
    w->bytes_written = 0;

    /* Find the highest existing segment ID so we append to the right file */
    w->segment_id = 0;
    DIR *d        = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strncmp(ent->d_name, WAL_FILENAME_PFX, strlen(WAL_FILENAME_PFX)) != 0)
                continue;
            const char *sfx = strstr(ent->d_name, WAL_FILENAME_SFX);
            if (!sfx)
                continue;
            uint64_t id = (uint64_t)strtoull(ent->d_name + strlen(WAL_FILENAME_PFX), NULL, 10);
            if (id >= w->segment_id)
                w->segment_id = id;
        }
        closedir(d);
    }

    char path[4096];
    snprintf(path, sizeof path, WAL_FILENAME_FMT, dir, w->segment_id);

    w->file = fopen(path, "ab");
    if (!w->file) {
        LOG_ERROR("WAL: failed to open %s: %s", path, strerror(errno));
        fkv_free(w->dir);
        fkv_free(w);
        return FASTKV_ERR_IO;
    }

    LOG_DEBUG("WAL opened: segment %" PRIu64 " (%s)", w->segment_id, path);
    *wal = w;
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_close(fastkv_wal_t *wal) {
    if (!wal)
        return FASTKV_OK;
    if (wal->file) {
        fflush(wal->file);
        fclose(wal->file);
    }
    fkv_free(wal->dir);
    fkv_free(wal);
    return FASTKV_OK;
}

/* Append */

fastkv_err_t fastkv_wal_append(fastkv_wal_t *wal, fastkv_wal_rec_type_t type, fastkv_ts_t ts,
    fastkv_slice_t key, fastkv_slice_t value) {
    uint32_t klen = (uint32_t)key.len;
    uint32_t vlen = (value.data == NULL) ? WAL_VLEN_TOMBSTONE : (uint32_t)value.len;

    /* CRC covers everything after the CRC field itself */
    uint32_t crc = 0;
    crc          = fastkv_crc32c(crc, &type, 1);
    crc          = fastkv_crc32c(crc, &ts, 8);
    crc          = fastkv_crc32c(crc, &klen, 4);
    crc          = fastkv_crc32c(crc, &vlen, 4);
    if (key.len)
        crc = fastkv_crc32c(crc, key.data, key.len);
    if (value.len)
        crc = fastkv_crc32c(crc, value.data, value.len);

    FILE        *f = wal->file;
    fastkv_err_t rc;

    if ((rc = write_u32(f, crc)) != FASTKV_OK)
        return rc;
    if ((rc = write_u8(f, (uint8_t)type)) != FASTKV_OK)
        return rc;
    if ((rc = write_u64(f, ts)) != FASTKV_OK)
        return rc;
    if ((rc = write_u32(f, klen)) != FASTKV_OK)
        return rc;
    if ((rc = write_u32(f, vlen)) != FASTKV_OK)
        return rc;
    if (key.len && fwrite(key.data, 1, key.len, f) != key.len)
        return FASTKV_ERR_IO;
    if (value.len && fwrite(value.data, 1, value.len, f) != value.len)
        return FASTKV_ERR_IO;

    wal->bytes_written += WAL_HEADER_SIZE + key.len + (value.data ? value.len : 0);

    if (wal->sync_writes)
        return fastkv_wal_sync(wal);
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_sync(fastkv_wal_t *wal) {
    if (!wal || !wal->file)
        return FASTKV_OK;
    if (fflush(wal->file) != 0)
        return FASTKV_ERR_IO;
#ifndef _WIN32
    if (fsync(fileno(wal->file)) != 0)
        return FASTKV_ERR_IO;
#endif
    return FASTKV_OK;
}

/* Replay */

/* Collect and sort segment IDs found in dir */
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static fastkv_err_t collect_segments(const char *dir, uint64_t **ids_out, size_t *count_out) {
    DIR *d = opendir(dir);
    if (!d)
        return FASTKV_ERR_IO;

    size_t    cap = 64, n = 0;
    uint64_t *ids = fkv_malloc(cap * sizeof(*ids));
    if (!ids) {
        closedir(d);
        return FASTKV_ERR_NOMEM;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, WAL_FILENAME_PFX, strlen(WAL_FILENAME_PFX)) != 0)
            continue;
        if (!strstr(ent->d_name, WAL_FILENAME_SFX))
            continue;
        uint64_t id = (uint64_t)strtoull(ent->d_name + strlen(WAL_FILENAME_PFX), NULL, 10);
        if (n >= cap) {
            cap *= 2;
            uint64_t *tmp = fkv_realloc(ids, cap * sizeof(*ids));
            if (!tmp) {
                fkv_free(ids);
                closedir(d);
                return FASTKV_ERR_NOMEM;
            }
            ids = tmp;
        }
        ids[n++] = id;
    }
    closedir(d);

    qsort(ids, n, sizeof(*ids), cmp_u64);
    *ids_out   = ids;
    *count_out = n;
    return FASTKV_OK;
}

/*
 * Replay one segment file.
 * Returns FASTKV_OK even when a torn record is detected at the tail
 * (that is normal crash boundary behaviour — not an error).
 */
static fastkv_err_t replay_segment(const char *path, fastkv_ts_t since_ts, fastkv_wal_replay_fn fn,
    void *udata, fastkv_ts_t *max_ts, uint64_t *rec_count) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_WARN("WAL replay: cannot open %s: %s", path, strerror(errno));
        return FASTKV_OK; /* missing segment is not fatal during recovery */
    }

    uint8_t *key_buf = NULL;
    uint8_t *val_buf = NULL;
    size_t   key_cap = 0;
    size_t   val_cap = 0;

    for (;;) {
        uint32_t stored_crc;
        uint8_t  type_byte;
        uint64_t ts;
        uint32_t klen, vlen;

        /* Any incomplete read at this point = torn record at crash boundary */
        if (!read_u32(f, &stored_crc))
            break;
        if (!read_u8(f, &type_byte))
            break;
        if (!read_u64(f, &ts))
            break;
        if (!read_u32(f, &klen))
            break;
        if (!read_u32(f, &vlen))
            break;

        if (klen > FASTKV_MAX_KEY_LEN) {
            LOG_WARN("WAL replay %s: implausible klen=%u, stopping", path, klen);
            break;
        }
        uint32_t real_vlen = (vlen == WAL_VLEN_TOMBSTONE) ? 0 : vlen;
        if (real_vlen > FASTKV_MAX_VAL_LEN) {
            LOG_WARN("WAL replay %s: implausible vlen=%u, stopping", path, vlen);
            break;
        }

        /* Resize key/value buffers as needed */
        if (klen > key_cap) {
            fkv_free(key_buf);
            key_buf = fkv_malloc(klen + 1);
            if (!key_buf)
                break;
            key_cap = klen;
        }
        if (real_vlen > val_cap) {
            fkv_free(val_buf);
            val_buf = fkv_malloc(real_vlen + 1);
            if (!val_buf)
                break;
            val_cap = real_vlen;
        }

        if (klen && fread(key_buf, 1, klen, f) != klen)
            break;
        if (real_vlen && fread(val_buf, 1, real_vlen, f) != real_vlen)
            break;

        /* Verify CRC */
        uint32_t computed = 0;
        computed          = fastkv_crc32c(computed, &type_byte, 1);
        computed          = fastkv_crc32c(computed, &ts, 8);
        computed          = fastkv_crc32c(computed, &klen, 4);
        computed          = fastkv_crc32c(computed, &vlen, 4);
        if (klen)
            computed = fastkv_crc32c(computed, key_buf, klen);
        if (real_vlen)
            computed = fastkv_crc32c(computed, val_buf, real_vlen);

        if (computed != stored_crc) {
            LOG_WARN(
                "WAL replay %s: CRC mismatch at ts=%" PRIu64 ", stopping (torn write)", path, ts);
            break;
        }

        /* Update high-water mark */
        if (ts > *max_ts)
            *max_ts = ts;

        /* Skip records already covered by the snapshot */
        if (ts <= since_ts)
            continue;

        /* Only replay data records */
        if (type_byte != WAL_REC_PUT && type_byte != WAL_REC_DELETE)
            continue;

        fastkv_wal_record_t rec = {
            .crc  = stored_crc,
            .ts   = (fastkv_ts_t)ts,
            .type = (fastkv_wal_rec_type_t)type_byte,
            .key  = FASTKV_SLICE(key_buf, klen),
            .value =
                (vlen == WAL_VLEN_TOMBSTONE) ? FASTKV_SLICE_NULL : FASTKV_SLICE(val_buf, real_vlen),
        };

        fastkv_err_t rc = fn(&rec, udata);
        if (rc != FASTKV_OK) {
            fkv_free(key_buf);
            fkv_free(val_buf);
            fclose(f);
            return rc;
        }
        (*rec_count)++;
    }

    fkv_free(key_buf);
    fkv_free(val_buf);
    fclose(f);
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_replay(const char *dir, fastkv_ts_t since_ts, fastkv_wal_replay_fn fn,
    void *udata, fastkv_ts_t *max_ts_out) {
    uint64_t *ids   = NULL;
    size_t    count = 0;

    fastkv_err_t rc = collect_segments(dir, &ids, &count);
    if (rc != FASTKV_OK)
        return rc;

    if (count == 0) {
        LOG_INFO("WAL replay: no segments found in %s", dir);
        return FASTKV_OK;
    }

    fastkv_ts_t max_ts = since_ts;
    uint64_t    total  = 0;
    char        path[4096];

    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof path, WAL_FILENAME_FMT, dir, ids[i]);
        rc = replay_segment(path, since_ts, fn, udata, &max_ts, &total);
        if (rc != FASTKV_OK)
            break;
    }

    LOG_INFO("WAL replay: %zu segments, %" PRIu64 " records replayed, max_ts=%" PRIu64, count,
        total, max_ts);

    if (max_ts_out)
        *max_ts_out = max_ts;
    fkv_free(ids);
    return rc;
}

/* Maintenance */

fastkv_err_t fastkv_wal_rotate(fastkv_wal_t *wal) {
    if (wal->file) {
        fflush(wal->file);
        fclose(wal->file);
    }
    wal->segment_id++;

    char path[4096];
    snprintf(path, sizeof path, WAL_FILENAME_FMT, wal->dir, wal->segment_id);
    wal->file = fopen(path, "ab");
    if (!wal->file)
        return FASTKV_ERR_IO;

    LOG_INFO("WAL rotated to segment %" PRIu64, wal->segment_id);
    return FASTKV_OK;
}

fastkv_err_t fastkv_wal_trim(const char *dir, uint64_t keep_from_id) {
    uint64_t    *ids   = NULL;
    size_t       count = 0;
    fastkv_err_t rc    = collect_segments(dir, &ids, &count);
    if (rc != FASTKV_OK)
        return rc;

    char     path[4096];
    uint64_t removed = 0;
    for (size_t i = 0; i < count; i++) {
        if (ids[i] >= keep_from_id)
            continue;
        snprintf(path, sizeof path, WAL_FILENAME_FMT, dir, ids[i]);
        if (remove(path) == 0) {
            removed++;
            LOG_DEBUG("WAL trim: removed segment %" PRIu64, ids[i]);
        }
    }
    LOG_INFO("WAL trim: removed %" PRIu64 " old segment(s)", removed);
    fkv_free(ids);
    return FASTKV_OK;
}

uint64_t fastkv_wal_current_segment(fastkv_wal_t *wal) {
    return wal ? wal->segment_id : 0;
}

uint64_t fastkv_wal_bytes_written(fastkv_wal_t *wal) {
    return wal ? wal->bytes_written : 0;
}
