add_executable(fastkv_bench
 bench_main.c
 bench_read.c
 bench_write.c
)

target_link_libraries(fastkv_bench PRIVATE fastkv)
target_include_directories(fastkv_bench PRIVATE
 ${PROJECT_SOURCE_DIR}/include
 ${PROJECT_SOURCE_DIR}/src
)
