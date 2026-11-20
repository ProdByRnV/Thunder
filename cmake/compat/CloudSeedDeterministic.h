#pragma once

// Part of Thunder's patch to CloudSeedCore (see cmake/CloudSeed.cmake).
//
// Upstream, each modulated delay and allpass picks its starting modulation
// phase with std::rand(). That global generator is shared by every reverb in
// the process, so each new reverb started differently: two instances (or a
// reopened session) didn't sound identical. These replace std::rand() with a
// small generator that Thunder reseeds, on the constructing thread, before it
// builds each reverb, so every reverb starts from the same phases.

#include <cstdint>

namespace Cloudseed
{
inline std::uint32_t& ThunderPhaseSeed()
{
    static thread_local std::uint32_t state = 1;
    return state;
}

// While set, ClearBuffers() leaves the large delay memory as it is and only
// resets the small state (modulation phases). Thunder sets it while resetting a
// reverb that is already silent, where zeroing 2 s of 192 kHz memory per line
// would only cost time (PRD DEC-043).
inline bool& ThunderSkipDelayZeroing()
{
    static thread_local bool skip = false;
    return skip;
}

// A value in [0, 1), like std::rand() / RAND_MAX.
inline float ThunderPhaseRandom()
{
    auto& state = ThunderPhaseSeed();
    state = state * 1664525u + 1013904223u;
    return static_cast<float> (state >> 8) / 16777216.0f;
}
} // namespace Cloudseed
