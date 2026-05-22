find_package(PkgConfig QUIET)

# ── Unity test framework (vendored, single-file) ──────────────────────────
# Download unity.c/.h if not present — CI fetches them automatically.
set(UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/unity)
if(NOT EXISTS ${UNITY_DIR}/unity.c)
    message(STATUS "Unity not found; fetching...")
    file(DOWNLOAD
        https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity.c
        ${UNITY_DIR}/unity.c SHOW_PROGRESS)
    file(DOWNLOAD
        https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity.h
        ${UNITY_DIR}/unity.h SHOW_PROGRESS)
    file(DOWNLOAD
        https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.5.2/src/unity_internals.h
        ${UNITY_DIR}/unity_internals.h SHOW_PROGRESS)
endif()

add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})

# ── Helper macro ──────────────────────────────────────────────────────────
function(add_fastkv_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE fastkv unity)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ── Unit tests ────────────────────────────────────────────────────────────
add_fastkv_test(test_arena      unit/test_arena.c)
add_fastkv_test(test_hashtable  unit/test_hashtable.c)
add_fastkv_test(test_wal        unit/test_wal.c)
add_fastkv_test(test_txn        unit/test_txn.c)
add_fastkv_test(test_crc32      unit/test_crc32.c)

# ── Integration tests ─────────────────────────────────────────────────────
add_fastkv_test(test_crud       integration/test_crud.c)

# ── Fuzz targets (clang only) ─────────────────────────────────────────────
if(FASTKV_BUILD_FUZZ)
    add_executable(fuzz_api fuzz/fuzz_api.c)
    target_link_libraries(fuzz_api PRIVATE fastkv)
    target_compile_options(fuzz_api PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_api    PRIVATE -fsanitize=fuzzer,address)
endif()
