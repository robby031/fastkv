#define _POSIX_C_SOURCE 200809L

#include "api/kv_api.h"
#include "mem/allocator.h"
#include "repl.h"
#include "util/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* kirim semua bytes, handle partial write */
static bool send_all(int fd, const void *buf, size_t len) {
    const uint8_t *ptr = buf;
    while (len > 0) {
        ssize_t n = send(fd, ptr, len, MSG_NOSIGNAL);
        if (n <= 0)
            return false;
        ptr += n;
        len -= (size_t)n;
    }
    return true;
}

static void sender_drain_queue(struct repl_sender *s) {
    while (!atomic_load_explicit(&s->stop, memory_order_acquire)) {
        pthread_mutex_lock(&s->queue_lock);
        while (!s->queue_head && !atomic_load_explicit(&s->stop, memory_order_relaxed))
            pthread_cond_wait(&s->queue_cond, &s->queue_lock);

        struct repl_queued_frame *f = s->queue_head;
        if (f) {
            s->queue_head = f->next;
            if (!s->queue_head)
                s->queue_tail = NULL;
            s->queue_bytes -= f->len;
        }
        pthread_mutex_unlock(&s->queue_lock);

        if (!f)
            continue;

        /* kirim frame header + payload */
        repl_frame_hdr_t hdr = {
            .magic       = REPL_MAGIC,
            .payload_len = f->len,
            .offset      = f->offset,
        };
        bool ok = send_all(s->fd, &hdr, sizeof hdr) && send_all(s->fd, f->data, f->len);

        fkv_free(f->data);
        fkv_free(f);

        if (!ok) {
            LOG_WARN("repl: sender ke %s putus saat kirim", s->addr);
            atomic_store_explicit(&s->connected, false, memory_order_release);
            atomic_store_explicit(&s->stop, true, memory_order_release);
        }
    }
}

static void *sender_thread(void *arg) {
    struct repl_sender *s = arg;
    sender_drain_queue(s);
    atomic_store_explicit(&s->connected, false, memory_order_release);
    LOG_INFO("repl: sender thread untuk %s berhenti", s->addr);
    return NULL;
}

/* tambah frame ke antrian sender */
static void sender_enqueue(struct repl_sender *s, const void *data, uint32_t len, uint64_t offset) {
    if (!atomic_load_explicit(&s->connected, memory_order_acquire))
        return;

    pthread_mutex_lock(&s->queue_lock);
    if (s->queue_bytes + len > REPL_MAX_QUEUE_BYTES) {
        /* replica terlalu lambat, putus */
        pthread_mutex_unlock(&s->queue_lock);
        LOG_WARN("repl: %s terlalu lambat (antrian penuh), memutus koneksi", s->addr);
        atomic_store_explicit(&s->stop, true, memory_order_release);
        atomic_store_explicit(&s->connected, false, memory_order_release);
        pthread_cond_signal(&s->queue_cond);
        return;
    }

    struct repl_queued_frame *f = fkv_malloc(sizeof(*f));
    if (!f) {
        pthread_mutex_unlock(&s->queue_lock);
        return;
    }
    f->data = fkv_malloc(len);
    if (!f->data) {
        fkv_free(f);
        pthread_mutex_unlock(&s->queue_lock);
        return;
    }
    memcpy(f->data, data, len);
    f->len    = len;
    f->offset = offset;
    f->next   = NULL;

    if (s->queue_tail)
        s->queue_tail->next = f;
    else
        s->queue_head = f;
    s->queue_tail = f;
    s->queue_bytes += len;

    pthread_cond_signal(&s->queue_cond);
    pthread_mutex_unlock(&s->queue_lock);
}

static struct repl_sender *sender_create(
    struct fastkv_repl_server *srv, int fd, struct sockaddr_in *peer) {
    struct repl_sender *s = fkv_malloc(sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));

    s->fd  = fd;
    s->srv = srv;
    snprintf(s->addr, sizeof s->addr, "%s:%u", inet_ntoa(peer->sin_addr), ntohs(peer->sin_port));

    pthread_mutex_init(&s->queue_lock, NULL);
    pthread_cond_init(&s->queue_cond, NULL);
    atomic_init(&s->connected, true);
    atomic_init(&s->stop, false);

    if (pthread_create(&s->thr, NULL, sender_thread, s) != 0) {
        pthread_mutex_destroy(&s->queue_lock);
        pthread_cond_destroy(&s->queue_cond);
        fkv_free(s);
        return NULL;
    }

    LOG_INFO("repl: replica baru terhubung dari %s", s->addr);
    return s;
}

static void sender_destroy(struct repl_sender *s) {
    atomic_store_explicit(&s->stop, true, memory_order_release);
    pthread_mutex_lock(&s->queue_lock);
    pthread_cond_signal(&s->queue_cond);
    pthread_mutex_unlock(&s->queue_lock);
    pthread_join(s->thr, NULL);
    close(s->fd);

    /* bersihkan antrian yang tersisa */
    struct repl_queued_frame *f = s->queue_head;
    while (f) {
        struct repl_queued_frame *nx = f->next;
        fkv_free(f->data);
        fkv_free(f);
        f = nx;
    }

    pthread_mutex_destroy(&s->queue_lock);
    pthread_cond_destroy(&s->queue_cond);
    fkv_free(s);
}

/* hapus sender yang sudah terputus dari daftar */
static void prune_disconnected(struct fastkv_repl_server *srv) {
    pthread_mutex_lock(&srv->senders_lock);
    struct repl_sender **pp = &srv->senders;
    while (*pp) {
        struct repl_sender *s = *pp;
        if (!atomic_load_explicit(&s->connected, memory_order_acquire)) {
            *pp = s->next;
            sender_destroy(s);
        } else {
            pp = &s->next;
        }
    }
    pthread_mutex_unlock(&srv->senders_lock);
}

static void *accept_thread(void *arg) {
    struct fastkv_repl_server *srv = arg;

    while (!atomic_load_explicit(&srv->stop, memory_order_acquire)) {
        struct sockaddr_in peer;
        socklen_t          plen = sizeof peer;
        int                cfd  = accept(srv->listen_fd, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) {
            if (!atomic_load_explicit(&srv->stop, memory_order_acquire))
                LOG_WARN("repl: accept error: %s", strerror(errno));
            break;
        }

        /* aktifkan TCP_NODELAY untuk latensi rendah */
        int one = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

        struct repl_sender *s = sender_create(srv, cfd, &peer);
        if (!s) {
            close(cfd);
            continue;
        }

        /* kirim greeting: offset saat ini */
        repl_frame_hdr_t hello = {
            .magic       = REPL_MAGIC,
            .payload_len = 0,
            .offset      = srv->primary_offset,
        };
        send_all(cfd, &hello, sizeof hello);

        pthread_mutex_lock(&srv->senders_lock);
        s->next      = srv->senders;
        srv->senders = s;
        pthread_mutex_unlock(&srv->senders_lock);

        prune_disconnected(srv);
    }

    return NULL;
}

fastkv_err_t fastkv_repl_server_init(
    struct fastkv_repl_server *srv, struct fastkv_db *db, uint16_t port) {
    memset(srv, 0, sizeof(*srv));
    srv->db   = db;
    srv->port = port;
    atomic_init(&srv->stop, false);
    pthread_mutex_init(&srv->senders_lock, NULL);

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0)
        return FASTKV_ERR_IO;

    int one = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(port),
    };
    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(srv->listen_fd, 8) < 0) {
        close(srv->listen_fd);
        return FASTKV_ERR_IO;
    }

    if (pthread_create(&srv->accept_thr, NULL, accept_thread, srv) != 0) {
        close(srv->listen_fd);
        return FASTKV_ERR_IO;
    }

    LOG_INFO("repl: server aktif di port %u", port);
    return FASTKV_OK;
}

void fastkv_repl_server_destroy(struct fastkv_repl_server *srv) {
    if (srv->listen_fd <= 0)
        return;

    atomic_store_explicit(&srv->stop, true, memory_order_release);
    shutdown(srv->listen_fd, SHUT_RDWR);
    close(srv->listen_fd);
    srv->listen_fd = 0;
    pthread_join(srv->accept_thr, NULL);

    pthread_mutex_lock(&srv->senders_lock);
    struct repl_sender *s = srv->senders;
    srv->senders          = NULL;
    pthread_mutex_unlock(&srv->senders_lock);

    while (s) {
        struct repl_sender *nx = s->next;
        sender_destroy(s);
        s = nx;
    }

    pthread_mutex_destroy(&srv->senders_lock);
    LOG_INFO("repl: server berhenti");
}

void fastkv_repl_broadcast(struct fastkv_repl_server *srv, const void *data, size_t len) {
    if (!srv || srv->listen_fd <= 0)
        return;

    srv->primary_offset += len;
    uint64_t offset = srv->primary_offset;

    pthread_mutex_lock(&srv->senders_lock);
    for (struct repl_sender *s = srv->senders; s; s = s->next)
        sender_enqueue(s, data, (uint32_t)len, offset);
    pthread_mutex_unlock(&srv->senders_lock);
}
