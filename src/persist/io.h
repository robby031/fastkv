#ifndef FASTKV_PERSIST_IO_H
#define FASTKV_PERSIST_IO_H

#include "fastkv/error.h"
#include <stddef.h>
#include <stdint.h>

/*
 * I/O backend abstraction.
 *
 * On Linux with io_uring support compiled in, persistent writes go through
 * the ring for async submission and batching.  On other platforms (or when
 * io_uring is disabled at build time) we fall back to POSIX pwrite(2) + fsync.
 */

typedef struct fastkv_io_ctx fastkv_io_ctx_t;

fastkv_err_t fastkv_io_open(fastkv_io_ctx_t **ctx, const char *path, int flags);
fastkv_err_t fastkv_io_close(fastkv_io_ctx_t *ctx);

/* Synchronous write — returns after data is durably persisted */
fastkv_err_t fastkv_io_pwrite(fastkv_io_ctx_t *ctx,
                               const void      *buf,
                               size_t           len,
                               uint64_t         offset);

/* Flush all pending writes to durable storage */
fastkv_err_t fastkv_io_sync(fastkv_io_ctx_t *ctx);

/* Memory-map a range of the file for read access */
fastkv_err_t fastkv_io_mmap(fastkv_io_ctx_t *ctx,
                             uint64_t         offset,
                             size_t           length,
                             void           **addr_out);

fastkv_err_t fastkv_io_munmap(void *addr, size_t length);

#endif /* FASTKV_PERSIST_IO_H */
