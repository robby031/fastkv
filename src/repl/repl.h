#ifndef FASTKV_REPL_H
#define FASTKV_REPL_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* magic bytes untuk validasi frame */
#define REPL_MAGIC 0x464B5254UL /* "FKRT" */
#define REPL_VERSION 1

/* ukuran antrian frame per sender sebelum replica dianggap terlalu lambat */
#define REPL_MAX_QUEUE_BYTES (8u * 1024u * 1024u) /* 8 MB */

/* header frame replikasi di atas TCP */
typedef struct {
    uint32_t magic;
    uint32_t payload_len;
    uint64_t offset; /* monotonic byte offset untuk hitung lag */
} repl_frame_hdr_t;

/* satu frame WAL record yang antri untuk dikirim */
struct repl_queued_frame {
    uint8_t                  *data; /* payload bytes (raw WAL record) */
    uint32_t                  len;
    uint64_t                  offset; /* monotonic offset dari primary */
    struct repl_queued_frame *next;
};

/* satu koneksi replica (sisi primary) */
struct repl_sender {
    int          fd;
    char         addr[64]; /* "host:port" */
    uint64_t     queue_bytes;
    _Atomic bool connected;
    _Atomic bool stop;

    struct repl_queued_frame *queue_head;
    struct repl_queued_frame *queue_tail;
    pthread_mutex_t           queue_lock;
    pthread_cond_t            queue_cond;

    pthread_t thr;

    struct fastkv_repl_server *srv;
    struct repl_sender        *next;
};

/* server replikasi (sisi primary) */
struct fastkv_repl_server {
    int          listen_fd;
    uint16_t     port;
    pthread_t    accept_thr;
    _Atomic bool stop;

    struct repl_sender *senders; /* linked list, dilindungi senders_lock */
    pthread_mutex_t     senders_lock;

    uint64_t primary_offset; /* monotonic counter byte WAL */

    struct fastkv_db *db;
};

/* klien replikasi (sisi replica) */
struct fastkv_repl_client {
    int      fd;
    char     host[256];
    uint16_t port;

    _Atomic bool     connected;
    _Atomic bool     stop;
    _Atomic uint64_t bytes_received;
    _Atomic uint64_t primary_offset; /* offset terakhir dari primary */

    pthread_t thr;

    struct fastkv_db *db;
};

struct fastkv_db;

fastkv_err_t fastkv_repl_server_init(
    struct fastkv_repl_server *srv, struct fastkv_db *db, uint16_t port);
void fastkv_repl_server_destroy(struct fastkv_repl_server *srv);

/* dipanggil oleh WAL setelah setiap record ditulis */
void fastkv_repl_broadcast(struct fastkv_repl_server *srv, const void *data, size_t len);

fastkv_err_t fastkv_repl_client_init(
    struct fastkv_repl_client *cli, struct fastkv_db *db, const char *host, uint16_t port);
void fastkv_repl_client_destroy(struct fastkv_repl_client *cli);

#endif /* FASTKV_REPL_H */
