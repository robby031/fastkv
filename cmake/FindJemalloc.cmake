find_path(JEMALLOC_INCLUDE_DIR jemalloc/jemalloc.h)
find_library(JEMALLOC_LIBRARY NAMES jemalloc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Jemalloc
    REQUIRED_VARS JEMALLOC_LIBRARY JEMALLOC_INCLUDE_DIR
)

if(Jemalloc_FOUND AND NOT TARGET Jemalloc::Jemalloc)
    add_library(Jemalloc::Jemalloc UNKNOWN IMPORTED)
    set_target_properties(Jemalloc::Jemalloc PROPERTIES
        IMPORTED_LOCATION "${JEMALLOC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
    )
endif()
