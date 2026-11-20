#pragma once

#include <cstdint>

// Thunder's random numbers (ARCHITECTURE §13.3, AD-05).
//
// PCG32 (O'Neill's pcg32: XSH-RR output on a 64-bit LCG): small, fast, with good
// statistics and, above all, a fixed algorithm. The same seed gives the same
// sequence on every compiler and OS, which `std::rand` and the standard
// distributions don't promise. That keeps renders reproducible: a bounce sounds
// like real-time playback, and a test that fails on one platform fails on all.

namespace thunder::util
{

class Rng
{
public:
    static constexpr std::uint64_t kDefaultSeed = 0x853c49e6748fea9bULL;
    static constexpr std::uint64_t kDefaultStream = 0xda3e39cb94b95bdbULL;

    explicit Rng (std::uint64_t seedValue = kDefaultSeed, std::uint64_t stream = kDefaultStream) noexcept
    {
        seed (seedValue, stream);
    }

    // [RT] Restarts the sequence (pcg32_srandom_r).
    void seed (std::uint64_t seedValue, std::uint64_t stream = kDefaultStream) noexcept
    {
        state = 0;
        increment = (stream << 1u) | 1u;
        nextUInt32();
        state += seedValue;
        nextUInt32();
    }

    // [RT] 32 random bits (pcg32_random_r).
    std::uint32_t nextUInt32() noexcept
    {
        const auto old = state;
        state = old * 6364136223846793005ULL + increment;
        const auto xorshifted = static_cast<std::uint32_t> (((old >> 18u) ^ old) >> 27u);
        const auto rotation = static_cast<std::uint32_t> (old >> 59u);
        return (xorshifted >> rotation) | (xorshifted << ((32u - rotation) & 31u));
    }

    // [RT] Uniform in [0, 1): the top 24 bits, so every value is exact in float.
    float nextFloat() noexcept { return static_cast<float> (nextUInt32() >> 8u) * (1.0f / 16777216.0f); }

    // [RT] Uniform in [-1, 1).
    float nextBipolar() noexcept { return 2.0f * nextFloat() - 1.0f; }

private:
    std::uint64_t state = 0;
    std::uint64_t increment = 1;
};

} // namespace thunder::util
