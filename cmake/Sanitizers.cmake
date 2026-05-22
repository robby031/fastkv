# Configure sanitizers — mutually exclusive: ASan vs TSan

if(FASTKV_ENABLE_ASAN AND FASTKV_ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously")
endif()

add_library(fastkv_sanitizers INTERFACE)

if(FASTKV_ENABLE_ASAN)
    target_compile_options(fastkv_sanitizers INTERFACE -fsanitize=address,leak -fno-omit-frame-pointer)
    target_link_options(fastkv_sanitizers    INTERFACE -fsanitize=address,leak)
endif()

if(FASTKV_ENABLE_TSAN)
    target_compile_options(fastkv_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(fastkv_sanitizers    INTERFACE -fsanitize=thread)
endif()

if(FASTKV_ENABLE_UBSAN)
    target_compile_options(fastkv_sanitizers INTERFACE -fsanitize=undefined)
    target_link_options(fastkv_sanitizers    INTERFACE -fsanitize=undefined)
endif()
