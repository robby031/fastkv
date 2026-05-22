# Compiler flags shared across all targets

add_library(fastkv_compiler_flags INTERFACE)

target_compile_options(fastkv_compiler_flags INTERFACE
 -Wall
 -Wextra
 -Wpedantic
 -Wshadow
 -Wconversion
 -Wno-unused-parameter
 $<$<CONFIG:Debug>: -O0 -g3 -DDEBUG>
 $<$<CONFIG:Release>: -O3 -DNDEBUG -march=native>
 $<$<CONFIG:RelWithDebInfo>: -O2 -g -DNDEBUG>
)

target_compile_definitions(fastkv_compiler_flags INTERFACE
 FASTKV_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
 FASTKV_VERSION_MINOR=${PROJECT_VERSION_MINOR}
 FASTKV_VERSION_PATCH=${PROJECT_VERSION_PATCH}
 $<$<BOOL:${FASTKV_USE_IOURING}>:FASTKV_USE_IOURING>
)
