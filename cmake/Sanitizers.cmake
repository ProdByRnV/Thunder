# Sanitizer builds (Clang/GCC on Linux and macOS). Used by CI:
#   THUNDER_ENABLE_ASAN + THUNDER_ENABLE_UBSAN — memory errors and undefined behaviour
#   THUNDER_ENABLE_RTSAN                       — RealtimeSanitizer (Clang 20+): any allocation,
#                                                lock or blocking call inside a real-time section
#
# The flags are applied to everything built after this file is included,
# dependencies included, so instrumented and uninstrumented code never mix.
# RTSan cannot be combined with ASan in one binary.

option(THUNDER_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(THUNDER_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(THUNDER_ENABLE_RTSAN "Build with RealtimeSanitizer (Clang 20+)" OFF)

if(THUNDER_ENABLE_RTSAN AND THUNDER_ENABLE_ASAN)
    message(FATAL_ERROR "RealtimeSanitizer cannot be combined with AddressSanitizer")
endif()

set(_thunder_sanitizers "")
if(THUNDER_ENABLE_ASAN)
    list(APPEND _thunder_sanitizers address)
endif()
if(THUNDER_ENABLE_UBSAN)
    list(APPEND _thunder_sanitizers undefined)
endif()
if(THUNDER_ENABLE_RTSAN)
    list(APPEND _thunder_sanitizers realtime)
endif()

if(_thunder_sanitizers)
    if(MSVC)
        message(FATAL_ERROR "Thunder's sanitizer options are for Clang/GCC builds")
    endif()

    list(JOIN _thunder_sanitizers "," _thunder_sanitizer_list)
    add_compile_options(-fsanitize=${_thunder_sanitizer_list} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${_thunder_sanitizer_list})

    if(THUNDER_ENABLE_UBSAN)
        add_compile_options(-fno-sanitize-recover=undefined)
    endif()

    # Tells the tests which checking strategy to use (see tests/support/Realtime.h).
    add_compile_definitions(THUNDER_SANITIZER_BUILD=1)
    message(STATUS "Thunder: sanitizers enabled: ${_thunder_sanitizer_list}")
endif()
