#ifndef FASTKV_ERROR_H
#define FASTKV_ERROR_H

typedef enum {
    FASTKV_OK = 0,

    /* General */
    FASTKV_ERR_NOMEM   = -1, /* memory allocation failed */
    FASTKV_ERR_INVAL   = -2, /* invalid argument */
    FASTKV_ERR_IO      = -3, /* I/O failure */
    FASTKV_ERR_CORRUPT = -4, /* data corruption detected (CRC mismatch) */
    FASTKV_ERR_FULL    = -5, /* capacity limit reached */

    /* Key/Value */
    FASTKV_ERR_NOTFOUND = -10, /* key does not exist */
    FASTKV_ERR_KEYSIZE  = -11, /* key exceeds maximum length */
    FASTKV_ERR_VALSIZE  = -12, /* value exceeds maximum length */

    /* Transaction */
    FASTKV_ERR_TXN_RO       = -20, /* write attempted on read-only transaction */
    FASTKV_ERR_TXN_CONFLICT = -21, /* write-write conflict detected at commit */
    FASTKV_ERR_TXN_CLOSED   = -22, /* transaction already committed or aborted */

    /* Cursor */
    FASTKV_ERR_CURSOR_EOF = -30, /* cursor exhausted */
} fastkv_err_t;

const char *fastkv_strerror(fastkv_err_t err);

#endif /* FASTKV_ERROR_H */
