cmake_minimum_required(VERSION 3.18)
project(fastkv VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Options 
option(FASTKV_BUILD_TESTS "Build unit and integration tests" ON)
option(FASTKV_BUILD_BENCH "Build benchmark suite" ON)
option(FASTKV_BUILD_FUZZ  "Build fuzz targets (requires clang + libFuzzer)" OFF)
option(FASTKV_USE_JEMALLOC "Link against jemalloc" OFF)
option(FASTKV_ENABLE_ASAN "Enable AddressSanitizer"  OFF)
option(FASTKV_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(FASTKV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(FASTKV_USE_IOURING "Enable io_uring persistence backend (Linux only)" OFF)

# CMake modules 
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerFlags)
include(Sanitizers)

if(FASTKV_USE_JEMALLOC)
 find_package(Jemalloc REQUIRED)
endif()

# Core library 
add_subdirectory(src)

# Tests 
if(FASTKV_BUILD_TESTS)
 enable_testing()
 add_subdirectory(tests)
endif()

# Benchmarks 
if(FASTKV_BUILD_BENCH)
 add_subdirectory(bench)
endif()

# Fuzz Targets
if(FASTKV_BUILD_FUZZ)
 add_subdirectory(fuzz)
endif()

# Install rules 
include(GNUInstallDirs)
install(TARGETS fastkv
 ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/fastkv DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES include/fastkv.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Summary  
message(STATUS "
##########################################
               FastKV ${PROJECT_VERSION} 
##########################################")
message(STATUS "  C standard  : C${CMAKE_C_STANDARD}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests       : ${FASTKV_BUILD_TESTS}")
message(STATUS "  Benchmarks  : ${FASTKV_BUILD_BENCH}")
message(STATUS "  Fuzz        : ${FASTKV_BUILD_FUZZ}")
message(STATUS "  jemalloc    : ${FASTKV_USE_JEMALLOC}")
message(STATUS "  io_uring    : ${FASTKV_USE_IOURING}")
message(STATUS "  ASan        : ${FASTKV_ENABLE_ASAN}")
message(STATUS "  TSan        : ${FASTKV_ENABLE_TSAN}")
message(STATUS "
##########################################")