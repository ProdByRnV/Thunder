#pragma once

// Portability shims for CloudSeedCore, force-included (non-MSVC compilers only)
// into code that uses CloudSeed — see cmake/CloudSeed.cmake. They let the
// upstream sources build unmodified on macOS and Linux.

#if defined(__cplusplus)

// 1. strcpy_s: CloudSeed's parameter-to-text code calls it, and only MSVC's C
//    library provides it. Not used on the audio path.
    #if ! defined(_MSC_VER) && ! defined(__STDC_LIB_EXT1__)
        #include <cstddef>
        #include <cstdio>

inline int strcpy_s (char* destination, std::size_t size, const char* source)
{
    if (destination == nullptr || size == 0)
        return 1;

    std::snprintf (destination, size, "%s", source != nullptr ? source : "");
    return 0;
}
    #endif

// 2. std::sinf / std::sqrtf / std::expf: required by C++17 <cmath>, but GCC's
//    standard library only declares them from GCC 14 (libstdc++ bug 79700).
//    GCC 13 is still the default compiler on Ubuntu 24.04.
    #include <cmath>
    #if defined(__GLIBCXX__) && defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 14
        #include <math.h>

namespace std
{
using ::expf;
using ::sinf;
using ::sqrtf;
} // namespace std
    #endif

#endif
