#include "io.h"

#include "mem/allocator.h"
#include "util/log.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux__) && defined(FASTKV_USE_IOURING)
#include <liburing.h>

#define IOURING_QUEUE_DEPTH 256

struct fastkv_io_ctx {
    int             fd;
    struct io_uring ring;
    bool            use_uring;
    int             pending_ops;
};

fastkv_err_t fastkv_io_open(fastkv_io_ctx_t **ctx, const char *path, int flags) {
    fastkv_io_ctx_t *c = fkv_malloc(sizeof(*c));
    if (!c)
        return FASTKV_ERR_NOMEM;

    c->fd          = open(path, flags, 0644);
    if (c->fd < 0) {
        fkv_free(c);
        return FASTKV_ERR_IO;
    }

    c->use_uring   = false;
    c->pending_ops = 0;

    /* Try to initialize io_uring with polling mode */
    int rc = io_uring_queue_init(IOURING_QUEUE_DEPTH, &c->ring, IORING_SETUP_SQPOLL);
    if (rc == 0) {
        c->use_uring = true;
    } else {
        /* Fallback to regular io_uring if SQPOLL fails (e.g. no root/privilege in some kernels) */
        rc = io_uring_queue_init(IOURING_QUEUE_DEPTH, &c->ring, 0);
        if (rc == 0) {
            c->use_uring = true;
        } else {
            LOG_WARN("io_uring_queue_init failed (%d), falling back to POSIX", rc);
        }
    }

    *ctx = c;
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_close(fastkv_io_ctx_t *ctx) {
    if (!ctx)
        return FASTKV_OK;
    if (ctx->use_uring) {
        io_uring_queue_exit(&ctx->ring);
    }
    if (ctx->fd >= 0)
        close(ctx->fd);
    fkv_free(ctx);
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_size(fastkv_io_ctx_t *ctx, uint64_t *size_out) {
    if (!ctx || !size_out) return FASTKV_ERR_INVAL;
    struct stat st;
    if (fstat(ctx->fd, &st) != 0) return FASTKV_ERR_IO;
    *size_out = (uint64_t)st.st_size;
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_pwrite(fastkv_io_ctx_t *ctx, const void *buf, size_t len, uint64_t offset) {
    if (!ctx)
        return FASTKV_ERR_INVAL;
    if (ctx->use_uring) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
        if (!sqe)
            return FASTKV_ERR_IO;
        io_uring_prep_write(sqe, ctx->fd, buf, len, offset);
        io_uring_submit(&ctx->ring);

        struct io_uring_cqe *cqe;
        int                  rc = io_uring_wait_cqe(&ctx->ring, &cqe);
        if (rc < 0 || cqe->res < 0) {
            if (cqe)
                io_uring_cqe_seen(&ctx->ring, cqe);
            return FASTKV_ERR_IO;
        }
        io_uring_cqe_seen(&ctx->ring, cqe);
        return FASTKV_OK;
    } else {
        ssize_t written = pwrite(ctx->fd, buf, len, offset);
        return written == (ssize_t)len ? FASTKV_OK : FASTKV_ERR_IO;
    }
}

fastkv_err_t fastkv_io_pwrite_async(
    fastkv_io_ctx_t *ctx, const void *buf, size_t len, uint64_t offset) {
    if (!ctx)
        return FASTKV_ERR_INVAL;
    if (ctx->use_uring) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
        if (!sqe) {
            /* Queue full, submit what we have and try again */
            io_uring_submit(&ctx->ring);
            sqe = io_uring_get_sqe(&ctx->ring);
            if (!sqe)
                return FASTKV_ERR_IO;
        }
        io_uring_prep_write(sqe, ctx->fd, buf, len, offset);
        ctx->pending_ops++;
        return FASTKV_OK;
    } else {
        /* Fallback: just do it synchronously */
        ssize_t written = pwrite(ctx->fd, buf, len, offset);
        return written == (ssize_t)len ? FASTKV_OK : FASTKV_ERR_IO;
    }
}

fastkv_err_t fastkv_io_sync(fastkv_io_ctx_t *ctx) {
    if (!ctx)
        return FASTKV_ERR_INVAL;
    if (ctx->use_uring) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
        if (sqe) {
            io_uring_prep_fsync(sqe, ctx->fd, IORING_FSYNC_DATASYNC);
            ctx->pending_ops++;
        }

        io_uring_submit(&ctx->ring);

        /* Wait for all pending completions */
        for (int i = 0; i < ctx->pending_ops; i++) {
            struct io_uring_cqe *cqe;
            int                  rc = io_uring_wait_cqe(&ctx->ring, &cqe);
            if (rc < 0)
                continue;
            io_uring_cqe_seen(&ctx->ring, cqe);
        }
        ctx->pending_ops = 0;
        return FASTKV_OK;
    } else {
        return fsync(ctx->fd) == 0 ? FASTKV_OK : FASTKV_ERR_IO;
    }
}

#else
/* POSIX Fallback */

struct fastkv_io_ctx {
    int fd;
};

fastkv_err_t fastkv_io_open(fastkv_io_ctx_t **ctx, const char *path, int flags) {
    fastkv_io_ctx_t *c = fkv_malloc(sizeof(*c));
    if (!c)
        return FASTKV_ERR_NOMEM;

    c->fd          = open(path, flags, 0644);
    if (c->fd < 0) {
        fkv_free(c);
        return FASTKV_ERR_IO;
    }
    *ctx = c;
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_close(fastkv_io_ctx_t *ctx) {
    if (!ctx)
        return FASTKV_OK;
    if (ctx->fd >= 0)
        close(ctx->fd);
    fkv_free(ctx);
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_size(fastkv_io_ctx_t *ctx, uint64_t *size_out) {
    if (!ctx || !size_out) return FASTKV_ERR_INVAL;
    struct stat st;
    if (fstat(ctx->fd, &st) != 0) return FASTKV_ERR_IO;
    *size_out = (uint64_t)st.st_size;
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_pwrite(fastkv_io_ctx_t *ctx, const void *buf, size_t len, uint64_t offset) {
    if (!ctx)
        return FASTKV_ERR_INVAL;
    ssize_t written = pwrite(ctx->fd, buf, len, offset);
    return written == (ssize_t)len ? FASTKV_OK : FASTKV_ERR_IO;
}

fastkv_err_t fastkv_io_pwrite_async(
    fastkv_io_ctx_t *ctx, const void *buf, size_t len, uint64_t offset) {
    /* POSIX fallback is synchronous */
    return fastkv_io_pwrite(ctx, buf, len, offset);
}

fastkv_err_t fastkv_io_sync(fastkv_io_ctx_t *ctx) {
    if (!ctx)
        return FASTKV_ERR_INVAL;
    return fsync(ctx->fd) == 0 ? FASTKV_OK : FASTKV_ERR_IO;
}

#endif

fastkv_err_t fastkv_io_mmap(fastkv_io_ctx_t *ctx, uint64_t offset, size_t length, void **addr_out) {
    if (!ctx || !addr_out)
        return FASTKV_ERR_INVAL;
    void *addr = mmap(NULL, length, PROT_READ, MAP_SHARED, ctx->fd, offset);
    if (addr == MAP_FAILED)
        return FASTKV_ERR_IO;
    *addr_out = addr;
    return FASTKV_OK;
}

fastkv_err_t fastkv_io_munmap(void *addr, size_t length) {
    if (!addr)
        return FASTKV_ERR_INVAL;
    return munmap(addr, length) == 0 ? FASTKV_OK : FASTKV_ERR_IO;
}
