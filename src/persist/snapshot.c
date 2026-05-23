#include "snapshot.h"

#include "api/kv_api.h"
#include "index/btree/btree.h"
#include "mem/allocator.h"
#include "persist/io.h"
#include "storage/hashtable/ht.h"
#include "util/crc32.h"
#include "util/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNAP_FILENAME_FMT "snapshot-%020" PRIu64 ".bin"
#define SNAP_FILENAME_PFX "snapshot-"
#define SNAP_FILENAME_SFX ".bin"

#define SNAP_BUF_CAP 65536

struct snap_writer {
    fastkv_io_ctx_t *io;
    uint8_t          buf[SNAP_BUF_CAP];
    size_t           buf_len;
    uint64_t         offset;
    fastkv_err_t     err;
};

static void snap_flush(struct snap_writer *sw) {
    if (sw->err != FASTKV_OK || sw->buf_len == 0)
        return;
    sw->err = fastkv_io_pwrite(sw->io, sw->buf, sw->buf_len, sw->offset);
    if (sw->err == FASTKV_OK) {
        sw->offset += sw->buf_len;
        sw->buf_len = 0;
    }
}

static void snap_write(struct snap_writer *sw, const void *data, size_t len) {
    if (sw->err != FASTKV_OK)
        return;
    const uint8_t *ptr = data;
    while (len > 0) {
        size_t space = SNAP_BUF_CAP - sw->buf_len;
        if (space == 0) {
            snap_flush(sw);
            if (sw->err != FASTKV_OK)
                return;
            space = SNAP_BUF_CAP;
        }
        size_t to_copy = len < space ? len : space;
        memcpy(sw->buf + sw->buf_len, ptr, to_copy);
        sw->buf_len += to_copy;
        ptr += to_copy;
        len -= to_copy;
    }
}

static void snap_write_u16(struct snap_writer *sw, uint16_t v) {
    snap_write(sw, &v, 2);
}
static void snap_write_u32(struct snap_writer *sw, uint32_t v) {
    snap_write(sw, &v, 4);
}
static void snap_write_u64(struct snap_writer *sw, uint64_t v) {
    snap_write(sw, &v, 8);
}

/* Write */

fastkv_err_t fastkv_snapshot_write(const char *dir, fastkv_ts_t ts, struct fastkv_db *db) {
    fastkv_ht_t *ht = db->ht;

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
                break;
            }
            v = atomic_load_explicit(&v->next, memory_order_relaxed);
        }
    }

    char path[4096], tmp_path[4100];
    snprintf(path, sizeof path, "%s/" SNAP_FILENAME_FMT, dir, ts);
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", path);

    fastkv_io_ctx_t *io = NULL;
    fastkv_err_t     rc = fastkv_io_open(&io, tmp_path, O_CREAT | O_RDWR);
    if (rc != FASTKV_OK) {
        LOG_ERROR("snapshot: gagal membuat %s", tmp_path);
        return rc;
    }

    struct snap_writer sw = {.io = io, .buf_len = 0, .offset = 0, .err = FASTKV_OK};

    uint32_t magic   = SNAPSHOT_MAGIC;
    uint16_t version = SNAPSHOT_VERSION;
    uint16_t pad     = 0;
    uint32_t pad2    = 0;

    snap_write_u32(&sw, magic);
    snap_write_u16(&sw, version);
    snap_write_u16(&sw, pad);
    snap_write_u64(&sw, ts);
    snap_write_u64(&sw, num_keys);
    snap_write_u32(&sw, data_crc);
    snap_write_u32(&sw, pad2);

    for (size_t i = 0; i < ht->capacity; i++) {
        fastkv_version_t *v = atomic_load_explicit(&ht->buckets[i], memory_order_acquire);
        while (v) {
            if (v->end_ts == FASTKV_TS_MAX && v->value.data != NULL) {
                uint32_t klen = (uint32_t)v->key.len;
                uint32_t vlen = (uint32_t)v->value.len;
                snap_write_u32(&sw, klen);
                snap_write_u32(&sw, vlen);
                snap_write(&sw, v->key.data, v->key.len);
                snap_write(&sw, v->value.data, v->value.len);
                break;
            }
            v = atomic_load_explicit(&v->next, memory_order_relaxed);
        }
    }

    snap_flush(&sw);
    if (sw.err == FASTKV_OK) {
        sw.err = fastkv_io_sync(io);
    }
    fastkv_io_close(io);

    if (sw.err != FASTKV_OK) {
        remove(tmp_path);
        return sw.err;
    }

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return FASTKV_ERR_IO;
    }

    LOG_INFO(
        "snapshot berhasil ditulis: ts=%" PRIu64 " keys=%" PRIu64 " path=%s", ts, num_keys, path);
    return FASTKV_OK;
}

/* Load   */

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
        LOG_INFO("snapshot: tidak ada snapshot di %s, memulai fresh", dir);
        if (ts_out)
            *ts_out = 0;
        return FASTKV_OK;
    }

    fastkv_io_ctx_t *io = NULL;
    if (fastkv_io_open(&io, path, O_RDONLY) != FASTKV_OK) {
        LOG_ERROR("snapshot: gagal membuka %s: %s", path, strerror(errno));
        return FASTKV_ERR_IO;
    }

    uint64_t file_size = 0;
    if (fastkv_io_size(io, &file_size) != FASTKV_OK) {
        fastkv_io_close(io);
        return FASTKV_ERR_IO;
    }

    void *map_addr = NULL;
    if (fastkv_io_mmap(io, 0, file_size, &map_addr) != FASTKV_OK) {
        fastkv_io_close(io);
        return FASTKV_ERR_IO;
    }

    const uint8_t *ptr = map_addr;
    const uint8_t *end = ptr + file_size;

#define READ_BUF(dest, len)                                                                        \
    do {                                                                                           \
        if (ptr + (len) > end)                                                                     \
            goto bad;                                                                              \
        memcpy((dest), ptr, (len));                                                                \
        ptr += (len);                                                                              \
    } while (0)

    uint32_t magic;
    READ_BUF(&magic, 4);
    uint16_t version;
    READ_BUF(&version, 2);
    uint16_t pad;
    READ_BUF(&pad, 2);
    uint64_t ts;
    READ_BUF(&ts, 8);
    uint64_t num_keys;
    READ_BUF(&num_keys, 8);
    uint32_t stored_crc;
    READ_BUF(&stored_crc, 4);
    uint32_t pad2;
    READ_BUF(&pad2, 4);

    if (magic != SNAPSHOT_MAGIC) {
        LOG_ERROR("snapshot: magic number salah 0x%08X di %s", magic, path);
        goto bad;
    }
    if (version != SNAPSHOT_VERSION) {
        LOG_ERROR("snapshot: versi tidak didukung %u di %s", version, path);
        goto bad;
    }

    uint32_t data_crc = 0;
    uint64_t loaded   = 0;

    for (uint64_t k = 0; k < num_keys; k++) {
        uint32_t klen, vlen;
        READ_BUF(&klen, 4);
        READ_BUF(&vlen, 4);

        if (ptr + klen + vlen > end)
            goto bad;

        const uint8_t *kptr = ptr;
        ptr += klen;
        const uint8_t *vptr = ptr;
        ptr += vlen;

        data_crc = fastkv_crc32c(data_crc, &klen, 4);
        data_crc = fastkv_crc32c(data_crc, &vlen, 4);
        data_crc = fastkv_crc32c(data_crc, kptr, klen);
        data_crc = fastkv_crc32c(data_crc, vptr, vlen);

        fastkv_slice_t sk = FASTKV_SLICE(kptr, klen);
        fastkv_slice_t sv = FASTKV_SLICE(vptr, vlen);

        fastkv_err_t rc = fastkv_ht_put(db->ht, (fastkv_ts_t)ts, sk, sv);
        if (rc != FASTKV_OK)
            goto bad;
        if (db->btree)
            fastkv_btree_insert(db->btree, sk, sv);
        loaded++;
    }

    if (data_crc != stored_crc) {
        LOG_ERROR("snapshot: CRC tidak cocok di %s (tersimpan=%08X dihitung=%08X)", path,
            stored_crc, data_crc);
        fastkv_io_munmap(map_addr, file_size);
        fastkv_io_close(io);
        return FASTKV_ERR_CORRUPT;
    }

    LOG_INFO("snapshot berhasil dimuat (mmap): ts=%" PRIu64 " keys=%" PRIu64 " path=%s", ts, loaded,
        path);
    atomic_store_explicit(&db->stat_num_keys, loaded, memory_order_relaxed);
    if (ts_out)
        *ts_out = (fastkv_ts_t)ts;

    fastkv_io_munmap(map_addr, file_size);
    fastkv_io_close(io);
    return FASTKV_OK;

bad:
    fastkv_io_munmap(map_addr, file_size);
    fastkv_io_close(io);
    LOG_ERROR("snapshot: error saat membaca data dari file %s", path);
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
            LOG_DEBUG("snapshot trim: menghapus %s", path);
        }
    }
    closedir(d);
    LOG_INFO("snapshot trim: berhasil menghapus %" PRIu64 " snapshot lama", removed);
    return FASTKV_OK;
}
