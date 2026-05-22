find_package(Threads REQUIRED)

add_library(fastkv STATIC
    # util
    util/log.c
    util/crc32.c
    util/hash.c

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
    persist/snapshot.c
    persist/compaction.c

    # index
    index/btree/btree.c
    index/rtree/rtree.c

    # api
    api/kv_api.c
)

target_include_directories(fastkv
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

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
