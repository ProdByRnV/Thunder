#pragma once

#include "ModuleSlot.h"
#include "OutputStage.h"

#include "core/analysis/AnalysisTap.h"
#include "core/capture/CaptureController.h"
#include "core/dsp/duck/DuckModule.h"
#include "core/dsp/gain/AutoGain.h"
#include "core/dsp/gain/SafetyLimiter.h"
#include "core/dsp/grains/GrainModule.h"
#include "core/dsp/pitch/PitchModule.h"
#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/static/StaticModulator.h"
#include "core/dsp/time/TimeModule.h"
#include "core/dsp/tone/ToneModule.h"
#include "core/dsp/width/WidthModule.h"
#include "core/host/TransportState.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/LatencyDelay.h"
#include "core/util/Smoother.h"
#include "core/visual/VisualTap.h"

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
//     ├─ dry path: delayed by the chain's latency ──────────────────────────────────────────────────┐
//     └─ layer:    Pitch → Time → Grains → Space → Tone → Width → Shape → Auto Gain → Layer Level ───┤
//                  → Static's pan and level → Duck ─────────────────────────────────────────────────┴─ Output Mode /
//                  Mix
//                                                       → Output Gain → sanitise → safety limiter (−0.3 dBFS)
//
// The chain follows PRD DEC-005 (Pitch → Time → Grains → Space → Tone → Width →
// Shape), with Duck after Auto Gain so the slow loudness correction doesn't
// undo the ducking (PRD DEC-051). Every module sits
// in a ModuleSlot with its own click-free on/off switch (PRD FR-CORE-009); a
// switched-off module costs nothing and the latency never changes.
//
// Static (dsp::StaticModulator) isn't a stage: each chunk it moves the values
// some modules read (grain spreads, filter cutoffs) in a copy of the snapshot,
// and pans and dips the layer after Auto Gain (dsp::StaticStage).
//
// Tempo-synced modules get the song position of the audio they process: the
// host's position minus the latency of the modules before them, so their grid
// lines up with what is heard.
//
// The limiter looks ahead 1.5 ms; that is part of the reported latency.
//
// Capture (PRD FR-CAPT-*) taps the end of every chunk: the output for Full Mix
// captures, or, while a Layer capture is armed or recording, the layer as
// Output Mode "Layer Only" would play it (after Output Gain and its own safety
// limiter), so both are on the output's timeline.
//
// Input captures (PRD FR-INPUT-003) and live key detection (FR-KEY-002) read
// Thunder's input as the host sent it, before Input Gain.
//
// The visualiser's tap (visual::VisualTap) reads the aligned dry signal and the
// layer before the mix, the output after the limiter and where the grains
// read. It only reads, and only while an editor shows it.
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
    void prepare (double sampleRate, params::Quality quality = params::Quality::Standard);

    params::Quality getQuality() const noexcept { return quality; }

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
    // Halftime can play the rest of a bar from its history first.
    double getTailSeconds() const noexcept
    {
        return time.getTailSeconds() + grains.getTailSeconds() + space.getTailSeconds();
    }

    // Capture control and its queue (arm/cancel from the message thread; the
    // writer drains the queue).
    capture::CaptureController& getCapture() noexcept { return capture; }
    const capture::CaptureController& getCapture() const noexcept { return capture; }

    // The visualiser's feed (PRD UI-VIS-002): the editor reads it through UiBridge.
    visual::VisualTap& getVisualTap() noexcept { return visual; }

    // Live key detection's feed (PRD FR-KEY-002): analysis::AnalysisWorker reads it.
    analysis::AnalysisTap& getAnalysisTap() noexcept { return analysisTap; }

    // The grain module, for tests and diagnostics.
    const dsp::GrainModule& getGrains() const noexcept { return grains.get(); }
    const dsp::TimeModule& getTime() const noexcept { return time.get(); }

    // Where Static moved its targets in the most recent chunk.
    const dsp::StaticModulator::Frame& getStaticFrame() const noexcept { return staticModulator.getFrame(); }
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
    static constexpr std::size_t kChainLength = 7;
    std::array<Module*, kChainLength> chain() noexcept
    {
        return {&pitch, &time, &grains, &space, &tone, &width, &shape};
    }

    void processChunk (float* const* channels, int numSamples, const ProcessContext& context) noexcept;
    void runModule (Module& module, juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept;
    void sanitise (float* const* channels, int numSamples) noexcept;

    double sampleRate = 44100.0;
    params::Quality quality = params::Quality::Standard;
    bool prepared = false;
    bool firstBlock = true;            // the first block after prepare/reset snaps instead of ramping
    std::int64_t samplesProcessed = 0; // since prepare/reset: Static's control grid counts from here
    int latencySamples = 0;

    TransportState transport;
    util::GainSmoother inputGain;
    util::GainSmoother layerLevel;
    util::GainSmoother outputGain;
    dsp::AutoGain autoGain;
    dsp::SafetyLimiter limiter;

    ModuleSlot<dsp::PitchModule> pitch{params::ParamId::pitchOn};
    ModuleSlot<dsp::TimeModule> time{params::ParamId::timeOn};
    ModuleSlot<dsp::GrainModule> grains{params::ParamId::grainOn};
    ModuleSlot<dsp::SpaceModule> space{params::ParamId::spaceOn};
    ModuleSlot<dsp::ToneModule> tone{params::ParamId::toneOn};
    ModuleSlot<dsp::WidthModule> width{params::ParamId::widthOn};
    ModuleSlot<dsp::ShapeModule> shape{params::ParamId::shapeOn};
    OutputStage output;

    // The latency of the modules before each one in chain(), and of the whole chain.
    std::array<int, kChainLength> upstreamLatency{};
    int chainLatency = 0;

    dsp::StaticModulator staticModulator;
    dsp::StaticStage staticStage;
    dsp::DuckModule duck;            // after Auto Gain; switches itself (duckOn)
    params::ParamSnapshot effective; // the host's values with Static's movement

    util::LatencyDelay dryDelay;
    std::array<std::vector<float>, kNumChannels> dry;

    capture::CaptureController capture;
    std::array<std::vector<float>, kNumChannels> layerTap; // the layer for Layer captures
    std::array<std::vector<float>, kNumChannels> inputTap; // the input for Input captures
    dsp::SafetyLimiter layerTapLimiter;
    bool layerTapActive = false;

    visual::VisualTap visual;
    analysis::AnalysisTap analysisTap;

    std::atomic<std::uint64_t> nonFiniteReplaced{0};
    std::atomic<std::uint64_t> moduleResets{0};
};

} // namespace thunder
