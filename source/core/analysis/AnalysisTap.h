#pragma once

#include "core/util/SpscFifo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace thunder::analysis
{

// The audio thread's feed for live key detection (PRD FR-KEY-002, ARCHITECTURE
// §7.4): Thunder's input, mixed to mono, into a lock-free queue that the
// analysis thread drains. Off until a reader enables it; while the queue is
// full (nobody reading) audio is simply dropped.
//
// Threads: prepare() [NRT]; allocate() and setEnabled() [MSG]; push() [RT];
// read() and the getters [BG] (one reader).
class AnalysisTap
{
public:
    static constexpr int kMaxChunk = 256;

    // One second at 192 kHz (768 KB); the reader drains it every 100 ms.
    static constexpr int kCapacity = 192000;

    // [NRT] The rate of what follows; the reader starts over when it changes.
    void prepare (double rate) noexcept
    {
        sampleRate.store (rate, std::memory_order_relaxed);
        generation.fetch_add (1, std::memory_order_release);
    }

    // [MSG] Allocates the queue (once). Call before enabling.
    void allocate()
    {
        if (allocated.load (std::memory_order_acquire))
            return;
        fifo.setCapacity (kCapacity);
        allocated.store (true, std::memory_order_release);
    }

    void setEnabled (bool on) noexcept { enabled.store (on && allocated.load (std::memory_order_acquire)); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    // [RT] Stereo input before anything else touches it.
    void push (const float* const* channels, int numSamples) noexcept
    {
        if (!enabled.load (std::memory_order_relaxed))
            return;

        for (int offset = 0; offset < numSamples; offset += kMaxChunk)
        {
            const auto n = std::min (kMaxChunk, numSamples - offset);
            if (fifo.getFreeSpace() < n)
                return;

            for (int i = 0; i < n; ++i)
            {
                const auto m = 0.5f * (channels[0][offset + i] + channels[1][offset + i]);
                mono[static_cast<std::size_t> (i)] = std::isfinite (m) ? m : 0.0f;
            }
            fifo.push (mono.data(), n);
        }
    }

    // [BG] Up to `max` samples into `destination`; returns how many.
    int read (float* destination, int max) noexcept
    {
        return allocated.load (std::memory_order_acquire) ? fifo.pop (destination, max) : 0;
    }

    double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
    std::uint32_t getGeneration() const noexcept { return generation.load (std::memory_order_acquire); }

private:
    util::SpscFifo<float> fifo;
    std::array<float, kMaxChunk> mono{};
    std::atomic<bool> allocated{false};
    std::atomic<bool> enabled{false};
    std::atomic<double> sampleRate{48000.0};
    std::atomic<std::uint32_t> generation{0};
};

} // namespace thunder::analysis
