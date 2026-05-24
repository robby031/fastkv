#define _POSIX_C_SOURCE 200809L

#include "api/kv_api.h"
#include "index/btree/btree.h"
#include "mem/allocator.h"
#include "repl.h"
#include "storage/hashtable/ht.h"
#include "util/crc32.h"
#include "util/log.h"
#include "wal/wal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* baca tepat len bytes dari fd */
static bool recv_all(int fd, void *buf, size_t len) {
    uint8_t *ptr = buf;
    while (len > 0) {
        ssize_t n = recv(fd, ptr, len, MSG_WAITALL);
        if (n <= 0)
            return false;
        ptr += n;
        len -= (size_t)n;
    }
    return true;
}

/* terapkan satu WAL record ke DB replica */
static void apply_wal_record(struct fastkv_db *db, const uint8_t *buf, uint32_t len) {
    if (len < 17) /* minimal: crc(4) + type(1) + ts(8) + klen(4) + vlen(4) */
        return;

    const uint8_t *p = buf;

    uint32_t stored_crc;
    memcpy(&stored_crc, p, 4);
    p += 4;

    uint8_t type_byte = *p++;

    uint64_t ts;
    memcpy(&ts, p, 8);
    p += 8;

    uint32_t klen, vlen;
    memcpy(&klen, p, 4);
    p += 4;
    memcpy(&vlen, p, 4);
    p += 4;

#define WAL_VLEN_TOMBSTONE UINT32_MAX
    uint32_t real_vlen = (vlen == WAL_VLEN_TOMBSTONE) ? 0 : vlen;

    if ((size_t)(p - buf) + klen + real_vlen > len)
        return;

    /* verifikasi CRC */
    uint32_t computed = 0;
    computed          = fastkv_crc32c(computed, &type_byte, 1);
    computed          = fastkv_crc32c(computed, &ts, 8);
    computed          = fastkv_crc32c(computed, &klen, 4);
    computed          = fastkv_crc32c(computed, &vlen, 4);
    if (klen)
        computed = fastkv_crc32c(computed, p, klen);
    if (real_vlen)
        computed = fastkv_crc32c(computed, p + klen, real_vlen);

    if (computed != stored_crc) {
        LOG_WARN("repl: CRC tidak cocok, record diabaikan");
        return;
    }

    fastkv_slice_t key = FASTKV_SLICE(p, klen);
    fastkv_slice_t value =
        (vlen == WAL_VLEN_TOMBSTONE) ? FASTKV_SLICE_NULL : FASTKV_SLICE(p + klen, real_vlen);

    if (type_byte == WAL_REC_PUT) {
        fastkv_ht_put(db->ht, (fastkv_ts_t)ts, key, value);
        fastkv_btree_insert(db->btree, key, value);
    } else if (type_byte == WAL_REC_DELETE) {
        fastkv_ht_delete(db->ht, (fastkv_ts_t)ts, key);
        fastkv_btree_delete(db->btree, key);
    }
    /* advance oracle supaya snapshot ts tetap konsisten */
    fastkv_oracle_advance(&db->txn_mgr.oracle, (fastkv_ts_t)ts + 1);
}

static int connect_to_primary(const char *host, uint16_t port) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family       = AF_INET;
    hints.ai_socktype     = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof port_str, "%u", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

static void *receiver_thread(void *arg) {
    struct fastkv_repl_client *cli = arg;

    while (!atomic_load_explicit(&cli->stop, memory_order_acquire)) {
        int fd = connect_to_primary(cli->host, cli->port);
        if (fd < 0) {
            if (atomic_load_explicit(&cli->stop, memory_order_acquire))
                break;
            LOG_WARN("repl: gagal konek ke %s:%u, coba ulang dalam 2 detik", cli->host, cli->port);
            atomic_store_explicit(&cli->connected, false, memory_order_release);
            struct timespec ts = {.tv_sec = 2};
            nanosleep(&ts, NULL);
            continue;
        }

        cli->fd = fd;
        atomic_store_explicit(&cli->connected, true, memory_order_release);
        LOG_INFO("repl: terhubung ke primary %s:%u", cli->host, cli->port);

        /* terima greeting dari primary */
        repl_frame_hdr_t hello;
        if (!recv_all(fd, &hello, sizeof hello) || hello.magic != REPL_MAGIC) {
            LOG_WARN("repl: greeting tidak valid");
            goto reconnect;
        }
        atomic_store_explicit(&cli->primary_offset, hello.offset, memory_order_release);

        /* loop terima frame */
        while (!atomic_load_explicit(&cli->stop, memory_order_acquire)) {
            repl_frame_hdr_t hdr;
            if (!recv_all(fd, &hdr, sizeof hdr))
                break;
            if (hdr.magic != REPL_MAGIC) {
                LOG_WARN("repl: magic frame tidak valid");
                break;
            }
            if (hdr.payload_len == 0) {
                /* keepalive / sync frame */
                atomic_store_explicit(&cli->primary_offset, hdr.offset, memory_order_release);
                continue;
            }
            if (hdr.payload_len > FASTKV_MAX_KEY_LEN + FASTKV_MAX_VAL_LEN + 32) {
                LOG_WARN("repl: payload terlalu besar (%u), abort", hdr.payload_len);
                break;
            }

            uint8_t *payload = fkv_malloc(hdr.payload_len);
            if (!payload)
                break;
            if (!recv_all(fd, payload, hdr.payload_len)) {
                fkv_free(payload);
                break;
            }

            apply_wal_record(cli->db, payload, hdr.payload_len);
            fkv_free(payload);

            atomic_fetch_add_explicit(&cli->bytes_received, hdr.payload_len, memory_order_relaxed);
            atomic_store_explicit(&cli->primary_offset, hdr.offset, memory_order_release);
        }

    reconnect:
        close(fd);
        cli->fd = -1;
        atomic_store_explicit(&cli->connected, false, memory_order_release);

        if (!atomic_load_explicit(&cli->stop, memory_order_acquire)) {
            LOG_WARN("repl: koneksi terputus, reconnect dalam 2 detik");
            struct timespec ts = {.tv_sec = 2};
            nanosleep(&ts, NULL);
        }
    }

    return NULL;
}

fastkv_err_t fastkv_repl_client_init(
    struct fastkv_repl_client *cli, struct fastkv_db *db, const char *host, uint16_t port) {
    memset(cli, 0, sizeof(*cli));
    cli->db   = db;
    cli->port = port;
    cli->fd   = -1;
    strncpy(cli->host, host, sizeof cli->host - 1);
    atomic_init(&cli->connected, false);
    atomic_init(&cli->stop, false);
    atomic_init(&cli->bytes_received, 0);
    atomic_init(&cli->primary_offset, 0);

    if (pthread_create(&cli->thr, NULL, receiver_thread, cli) != 0)
        return FASTKV_ERR_IO;

    LOG_INFO("repl: client dimulai, menghubungi %s:%u", host, port);
    return FASTKV_OK;
}

void fastkv_repl_client_destroy(struct fastkv_repl_client *cli) {
    atomic_store_explicit(&cli->stop, true, memory_order_release);
    if (cli->fd >= 0) {
        shutdown(cli->fd, SHUT_RDWR);
        close(cli->fd);
        cli->fd = -1;
    }
    pthread_join(cli->thr, NULL);
    LOG_INFO("repl: client berhenti");
}
