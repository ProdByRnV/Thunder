#pragma once

#include "core/host/TransportState.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/Smoother.h"

#include <atomic>
#include <cstdint>

namespace thunder
{

// Thunder's whole audio path, independent of any host or plugin format
// (ARCHITECTURE §4–5). The plugin, the tests and (later) the offline renderer
// all drive this same class.
//
// Current stages: input sanitising (NaN/Inf → 0), input gain, output gain and a
// final sanitise. The module chain (pitch, grains, space, ...) is inserted
// between the gains in later milestones.
class ThunderEngine
{
public:
    // The engine works in chunks of at most this many samples, whatever block
    // size the host uses (ARCHITECTURE §5.6). Internal buffers are sized for it
    // once in prepare(), so no host block size can cause an allocation.
    static constexpr int kMaxChunkSize = 256;
    static constexpr int kNumChannels = 2;

    // [NRT] May allocate. Must be called before process(), and again whenever
    // the sample rate changes.
    void prepare (double sampleRate);

    // [RT] Clears all internal state (host reset or transport jump; PRD
    // FR-CORE-012). Smoothed values jump to their current targets.
    void reset() noexcept;

    // [RT] Processes stereo audio in place. Any length, including 0 and lengths
    // larger than the host announced.
    void process (float* const* channels, int numSamples, const TransportState& transport,
                  const params::ParamSnapshot& snapshot) noexcept;

    int getLatencySamples() const noexcept { return 0; }
    double getTailSeconds() const noexcept { return 0.0; }
    bool isPrepared() const noexcept { return prepared; }

    // The transport position of the most recent chunk.
    const TransportState& getTransport() const noexcept { return transport; }

    // NaN/Inf samples replaced since prepare() (input and output). For tests and
    // diagnostics; any thread may read it.
    std::uint64_t getNonFiniteSamplesReplaced() const noexcept
    {
        return nonFiniteReplaced.load (std::memory_order_relaxed);
    }

private:
    void processChunk (float* const* channels, int numSamples) noexcept;
    void sanitise (float* const* channels, int numSamples) noexcept;

    double sampleRate = 44100.0;
    bool prepared = false;
    bool firstBlock = true; // the first block after prepare/reset snaps instead of ramping

    TransportState transport;
    util::GainSmoother inputGain;
    util::GainSmoother outputGain;

    std::atomic<std::uint64_t> nonFiniteReplaced{0};
};

} // namespace thunder
