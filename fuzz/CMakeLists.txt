if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzzing membutuhkan compiler Clang. Target fuzz_db dilewati.")
    return()
endif()

add_executable(fuzz_db fuzz_db.c)
target_link_libraries(fuzz_db PRIVATE fastkv pthread)
target_include_directories(fuzz_db PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
)

# Aktifkan AddressSanitizer + LibFuzzer
target_compile_options(fuzz_db PRIVATE
    -fsanitize=fuzzer,address
    -fno-omit-frame-pointer
    -g
    -O1
)
target_link_options(fuzz_db PRIVATE -fsanitize=fuzzer,address)

message(STATUS "Target fuzzing 'fuzz_db' siap dikompilasi dengan Clang + LibFuzzer + ASan")
