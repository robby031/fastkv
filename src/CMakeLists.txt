find_package(Threads REQUIRED)

add_library(fastkv STATIC
    # util
    util/log.c
    util/log_public.c
    util/crc32.c
    util/hash.c
    util/uuid7/uuid7.c
    util/uuid7/clock.c
    util/uuid7/prng.c

    # mem
    mem/allocator.c
    mem/arena.c

    # storage
    storage/hashtable/ht.c

    # wal
    wal/wal.c

    # txn
    txn/mvcc.c
    txn/txn_manager.c

    # persist
    persist/io.c
    persist/snapshot.c
    persist/compaction.c

    # index
    index/btree/btree.c
    index/rtree/rtree.c
    index/secondary.c
    index/json_index.c

    # ttl
    persist/ttl.c

    # replication
    repl/repl_server.c
    repl/repl_client.c

    # api
    api/kv_api.c
)

target_include_directories(fastkv
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(fastkv PRIVATE _POSIX_C_SOURCE=200809L)
set_target_properties(fastkv PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(fastkv
    PUBLIC  fastkv_compiler_flags
            fastkv_sanitizers
    PRIVATE Threads::Threads m
)

if(FASTKV_USE_JEMALLOC)
    target_link_libraries(fastkv PRIVATE Jemalloc::Jemalloc)
endif()

if(FASTKV_USE_IOURING)
    target_link_libraries(fastkv PRIVATE uring)
endif()
