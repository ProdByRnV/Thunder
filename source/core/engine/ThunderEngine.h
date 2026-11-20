#pragma once

#include "ModuleSlot.h"
#include "OutputStage.h"

#include "core/dsp/grains/GrainModule.h"
#include "core/dsp/pitch/PitchModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/tone/ToneModule.h"
#include "core/dsp/width/WidthModule.h"
#include "core/host/TransportState.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/LatencyDelay.h"
#include "core/util/Smoother.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace thunder
{

// Thunder's whole audio path, independent of any host or plugin format
// (ARCHITECTURE §4–5). The plugin, the tests and (later) the offline renderer
// all drive this same class.
//
// Per chunk:
//   input gain, sanitise
//     ├─ dry path: delayed by the engine latency ──────────────────────┐
//     └─ layer:    Pitch → Grains → Space → Tone → Width ──────────────┴─ Output Mode / Mix → output gain → sanitise
//
// The chain follows PRD DEC-005 (Pitch → Time → Grains → Space → Tone → Width →
// Shape → Duck). Time (v0.8) will go between Pitch and Grains, Shape and Duck
// after Width. Every module sits in a ModuleSlot with its own click-free on/off
// switch (PRD FR-CORE-009); a switched-off module costs nothing and the latency
// never changes.
//
// TODO(PRD): Auto Gain, Layer Level and the safety limiter (T-0.4-04/05, 2F).
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

    // Total latency of the layer path. The dry path is delayed by the same
    // amount, so the whole output is this far behind the input. Constant after
    // prepare() (ARCHITECTURE §5.7).
    int getLatencySamples() const noexcept { return latencySamples; }

    // Grains keep replaying the input for a while, and the reverb then rings
    // out after the last grain.
    double getTailSeconds() const noexcept { return grains.getTailSeconds() + space.getTailSeconds(); }

    // The grain module, for tests and diagnostics.
    const dsp::GrainModule& getGrains() const noexcept { return grains.get(); }
    bool isPrepared() const noexcept { return prepared; }

    // The transport position of the most recent chunk.
    const TransportState& getTransport() const noexcept { return transport; }

    // NaN/Inf samples replaced since prepare() at the input and output. For tests
    // and diagnostics; any thread may read it.
    std::uint64_t getNonFiniteSamplesReplaced() const noexcept
    {
        return nonFiniteReplaced.load (std::memory_order_relaxed);
    }

    // Times a module produced NaN/Inf and was reset (ARCHITECTURE §14).
    std::uint64_t getModuleResets() const noexcept { return moduleResets.load (std::memory_order_relaxed); }

private:
    // The layer chain in processing order (PRD DEC-005).
    std::array<Module*, 5> chain() noexcept { return {&pitch, &grains, &space, &tone, &width}; }

    void processChunk (float* const* channels, int numSamples, const ProcessContext& context) noexcept;
    void runModule (Module& module, juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept;
    void sanitise (float* const* channels, int numSamples) noexcept;

    double sampleRate = 44100.0;
    bool prepared = false;
    bool firstBlock = true; // the first block after prepare/reset snaps instead of ramping
    int latencySamples = 0;

    TransportState transport;
    util::GainSmoother inputGain;
    util::GainSmoother outputGain;

    ModuleSlot<dsp::PitchModule> pitch{params::ParamId::pitchOn};
    ModuleSlot<dsp::GrainModule> grains{params::ParamId::grainOn};
    ModuleSlot<dsp::SpaceModule> space{params::ParamId::spaceOn};
    ModuleSlot<dsp::ToneModule> tone{params::ParamId::toneOn};
    ModuleSlot<dsp::WidthModule> width{params::ParamId::widthOn};
    OutputStage output;

    util::LatencyDelay dryDelay;
    std::array<std::vector<float>, kNumChannels> dry;

    std::atomic<std::uint64_t> nonFiniteReplaced{0};
    std::atomic<std::uint64_t> moduleResets{0};
};

} // namespace thunder
