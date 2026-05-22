/*
 * LibFuzzer entry point for the public API.
 * Build: cmake -DFASTKV_BUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang ..
 */

#include "fastkv.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Persistent db across fuzzer iterations for stateful fuzzing */
static fastkv_db_t *db;

__attribute__((constructor)) static void fuzz_init(void) {
    system("mkdir -p /tmp/fuzz_fastkv");
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = "/tmp/fuzz_fastkv";
    opts.sync_writes   = false;
    fastkv_open(&db, &opts);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return 0;

    uint8_t op   = data[0] % 3; /* 0=put, 1=get, 2=delete */
    size_t  klen = (data[1] % 64) + 1;
    if (klen + 2 > size)
        klen = size - 2;

    fastkv_slice_t key   = FASTKV_SLICE(data + 2, klen);
    fastkv_slice_t value = FASTKV_SLICE(data + 2 + klen, size - 2 - klen);

    switch (op) {
    case 0:
        fastkv_put(db, key, value);
        break;
    case 1: {
        fastkv_slice_t v;
        fastkv_get(db, key, &v);
    } break;
    case 2:
        fastkv_delete(db, key);
        break;
    }
    return 0;
}
