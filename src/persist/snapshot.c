#include "snapshot.h"

#include "api/kv_api.h"
#include "index/btree/btree.h"
#include "mem/allocator.h"
#include "storage/hashtable/ht.h"
#include "util/crc32.h"
#include "util/log.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SNAP_FILENAME_FMT "snapshot-%020" PRIu64 ".bin"
#define SNAP_FILENAME_PFX "snapshot-"
#define SNAP_FILENAME_SFX ".bin"

/* Low-level helpers   */

static bool fwrite_u16(FILE *f, uint16_t v) {
    return fwrite(&v, 2, 1, f) == 1;
}
static bool fwrite_u32(FILE *f, uint32_t v) {
    return fwrite(&v, 4, 1, f) == 1;
}
static bool fwrite_u64(FILE *f, uint64_t v) {
    return fwrite(&v, 8, 1, f) == 1;
}

static bool fread_u16(FILE *f, uint16_t *v) {
    return fread(v, 2, 1, f) == 1;
}
static bool fread_u32(FILE *f, uint32_t *v) {
    return fread(v, 4, 1, f) == 1;
}
static bool fread_u64(FILE *f, uint64_t *v) {
    return fread(v, 8, 1, f) == 1;
}

/* Write */

fastkv_err_t fastkv_snapshot_write(const char *dir, fastkv_ts_t ts, struct fastkv_db *db) {
    /* Collect all live key-value pairs from the hashtable */
    fastkv_ht_t *ht = db->ht;

    /* First pass: count live keys and compute data CRC */
    uint64_t num_keys = 0;
    uint32_t data_crc = 0;

    for (size_t i = 0; i < ht->capacity; i++) {
        fastkv_version_t *v = atomic_load_explicit(&ht->buckets[i], memory_order_acquire);
        while (v) {
            if (v->end_ts == FASTKV_TS_MAX && v->value.data != NULL) {
                uint32_t klen = (uint32_t)v->key.len;
                uint32_t vlen = (uint32_t)v->value.len;
                data_crc      = fastkv_crc32c(data_crc, &klen, 4);
                data_crc      = fastkv_crc32c(data_crc, &vlen, 4);
                data_crc      = fastkv_crc32c(data_crc, v->key.data, v->key.len);
                data_crc      = fastkv_crc32c(data_crc, v->value.data, v->value.len);
                num_keys++;
                break; /* only one live version per key */
            }
            v = atomic_load_explicit(&v->next, memory_order_relaxed);
        }
    }

    /* Open output file (write to tmp then rename for atomicity) */
    char path[4096], tmp_path[4096];
    snprintf(path, sizeof path, "%s/" SNAP_FILENAME_FMT, dir, ts);
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        LOG_ERROR("snapshot: cannot open %s: %s", tmp_path, strerror(errno));
        return FASTKV_ERR_IO;
    }

    /* Write header */
    uint32_t magic   = SNAPSHOT_MAGIC;
    uint16_t version = SNAPSHOT_VERSION;
    uint16_t pad     = 0;
    uint32_t pad2    = 0;

    if (!fwrite_u32(f, magic) || !fwrite_u16(f, version) || !fwrite_u16(f, pad) ||
        !fwrite_u64(f, ts) || !fwrite_u64(f, num_keys) || !fwrite_u32(f, data_crc) ||
        !fwrite_u32(f, pad2)) {
        fclose(f);
        remove(tmp_path);
        return FASTKV_ERR_IO;
    }

    /* Second pass: write key-value pairs */
    for (size_t i = 0; i < ht->capacity; i++) {
        fastkv_version_t *v = atomic_load_explicit(&ht->buckets[i], memory_order_acquire);
        while (v) {
            if (v->end_ts == FASTKV_TS_MAX && v->value.data != NULL) {
                uint32_t klen = (uint32_t)v->key.len;
                uint32_t vlen = (uint32_t)v->value.len;
                if (!fwrite_u32(f, klen) || !fwrite_u32(f, vlen) ||
                    fwrite(v->key.data, 1, v->key.len, f) != v->key.len ||
                    fwrite(v->value.data, 1, v->value.len, f) != v->value.len) {
                    fclose(f);
                    remove(tmp_path);
                    return FASTKV_ERR_IO;
                }
                break;
            }
            v = atomic_load_explicit(&v->next, memory_order_relaxed);
        }
    }

    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp_path);
        return FASTKV_ERR_IO;
    }
#ifndef _WIN32
    fsync(fileno(f));
#endif
    fclose(f);

    /* Atomic rename */
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return FASTKV_ERR_IO;
    }

    LOG_INFO("snapshot written: ts=%" PRIu64 " keys=%" PRIu64 " path=%s", ts, num_keys, path);
    return FASTKV_OK;
}

/* Load   */

/* Find the snapshot with the highest ts in dir */
static fastkv_ts_t find_latest_snapshot(const char *dir, char *path_out, size_t path_cap) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    fastkv_ts_t    best_ts = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, SNAP_FILENAME_PFX, strlen(SNAP_FILENAME_PFX)) != 0)
            continue;
        if (!strstr(ent->d_name, SNAP_FILENAME_SFX))
            continue;
        fastkv_ts_t ts = (fastkv_ts_t)strtoull(ent->d_name + strlen(SNAP_FILENAME_PFX), NULL, 10);
        if (ts > best_ts) {
            best_ts = ts;
            snprintf(path_out, path_cap, "%s/%s", dir, ent->d_name);
        }
    }
    closedir(d);
    return best_ts;
}

fastkv_err_t fastkv_snapshot_load(const char *dir, struct fastkv_db *db, fastkv_ts_t *ts_out) {
    char        path[4096] = {0};
    fastkv_ts_t snap_ts    = find_latest_snapshot(dir, path, sizeof path);

    if (snap_ts == 0) {
        LOG_INFO("snapshot: no snapshot found in %s, starting fresh", dir);
        if (ts_out)
            *ts_out = 0;
        return FASTKV_OK;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("snapshot: cannot open %s: %s", path, strerror(errno));
        return FASTKV_ERR_IO;
    }

    /* Read and validate header */
    uint32_t magic;
    if (!fread_u32(f, &magic))
        goto bad;
    uint16_t version;
    if (!fread_u16(f, &version))
        goto bad;
    uint16_t pad;
    if (!fread_u16(f, &pad))
        goto bad;
    uint64_t ts;
    if (!fread_u64(f, &ts))
        goto bad;
    uint64_t num_keys;
    if (!fread_u64(f, &num_keys))
        goto bad;
    uint32_t stored_crc;
    if (!fread_u32(f, &stored_crc))
        goto bad;
    uint32_t pad2;
    if (!fread_u32(f, &pad2))
        goto bad;

    if (magic != SNAPSHOT_MAGIC) {
        LOG_ERROR("snapshot: bad magic 0x%08X in %s", magic, path);
        goto bad;
    }
    if (version != SNAPSHOT_VERSION) {
        LOG_ERROR("snapshot: unsupported version %u in %s", version, path);
        goto bad;
    }

    /* Read key-value pairs */
    uint8_t *key_buf = NULL, *val_buf = NULL;
    size_t   key_cap = 0, val_cap = 0;
    uint32_t data_crc = 0;
    uint64_t loaded   = 0;

    for (uint64_t k = 0; k < num_keys; k++) {
        uint32_t klen, vlen;
        if (!fread_u32(f, &klen) || !fread_u32(f, &vlen))
            goto bad_buf;

        if (klen > key_cap) {
            fkv_free(key_buf);
            key_buf = fkv_malloc(klen);
            key_cap = klen;
        }
        if (vlen > val_cap) {
            fkv_free(val_buf);
            val_buf = fkv_malloc(vlen);
            val_cap = vlen;
        }
        if (!key_buf || !val_buf)
            goto bad_buf;

        if (fread(key_buf, 1, klen, f) != klen)
            goto bad_buf;
        if (fread(val_buf, 1, vlen, f) != vlen)
            goto bad_buf;

        /* Accumulate CRC for validation */
        data_crc = fastkv_crc32c(data_crc, &klen, 4);
        data_crc = fastkv_crc32c(data_crc, &vlen, 4);
        data_crc = fastkv_crc32c(data_crc, key_buf, klen);
        data_crc = fastkv_crc32c(data_crc, val_buf, vlen);

        fastkv_slice_t sk = FASTKV_SLICE(key_buf, klen);
        fastkv_slice_t sv = FASTKV_SLICE(val_buf, vlen);

        fastkv_err_t rc = fastkv_ht_put(db->ht, (fastkv_ts_t)ts, sk, sv);
        if (rc != FASTKV_OK) goto bad_buf;
        if (db->btree) fastkv_btree_insert(db->btree, sk, sv);
        loaded++;
        continue;

    bad_buf:
        fkv_free(key_buf);
        fkv_free(val_buf);
        fclose(f);
        LOG_ERROR("snapshot: read error at key %" PRIu64 " in %s", k, path);
        return FASTKV_ERR_IO;
    }

    fkv_free(key_buf);
    fkv_free(val_buf);
    fclose(f);

    if (data_crc != stored_crc) {
        LOG_ERROR(
            "snapshot: CRC mismatch in %s (stored=%08X computed=%08X)", path, stored_crc, data_crc);
        return FASTKV_ERR_CORRUPT;
    }

    LOG_INFO("snapshot loaded: ts=%" PRIu64 " keys=%" PRIu64 " path=%s", ts, loaded, path);
    if (ts_out)
        *ts_out = (fastkv_ts_t)ts;
    return FASTKV_OK;

bad:
    fclose(f);
    LOG_ERROR("snapshot: header read error in %s", path);
    return FASTKV_ERR_CORRUPT;
}

/* Trim old snapshots   */

fastkv_err_t fastkv_snapshot_trim(const char *dir, fastkv_ts_t keep_ts) {
    DIR *d = opendir(dir);
    if (!d)
        return FASTKV_ERR_IO;

    uint64_t       removed = 0;
    char           path[4096];
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, SNAP_FILENAME_PFX, strlen(SNAP_FILENAME_PFX)) != 0)
            continue;
        if (!strstr(ent->d_name, SNAP_FILENAME_SFX))
            continue;
        fastkv_ts_t ts = (fastkv_ts_t)strtoull(ent->d_name + strlen(SNAP_FILENAME_PFX), NULL, 10);
        if (ts >= keep_ts)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        if (remove(path) == 0) {
            removed++;
            LOG_DEBUG("snapshot trim: removed %s", path);
        }
    }
    closedir(d);
    LOG_INFO("snapshot trim: removed %" PRIu64 " old snapshot(s)", removed);
    return FASTKV_OK;
}
