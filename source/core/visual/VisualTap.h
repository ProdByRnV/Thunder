#pragma once

#include "VisualFrames.h"

#include "core/dsp/gain/LoudnessMeter.h"
#include "core/host/TransportState.h"
#include "core/util/SpscFifo.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{
class GrainModule;
}

namespace thunder::visual
{

// The audio side of the visualiser (PRD UI-VIS-002/004/006/007/008, ARCHITECTURE
// §6.3, §7.1). The engine hands it the aligned dry signal and the layer before
// the output mix, then the final output, every chunk; it reduces them to
// WaveFrames (min/max and meter sums per ~1.3 ms), spectrum samples and grain
// snapshots, and queues them for the editor:
//
//   [RT]  engine chunk ─► tapLayer / tapOutput / tapGrains ─► three SPSC queues
//   [MSG] frame timer  ─► readFrames / readSpectrum / readGrains
//
// The audio thread never waits for the editor: a full queue drops what doesn't
// fit (and counts it). While no editor shows a part, that part costs the audio
// thread nothing, and with no editor open the whole tap is skipped
// (UI-VIS-006). The queues are allocated the first time an editor asks for
// data and never again, so a sample-rate change can't move memory under a
// reader. The tap only reads the audio: the sound is the same with or without it.
class VisualTap
{
public:
    static constexpr double kFramesPerSecond = 750.0; // one frame per 64 samples at 48 kHz
    static constexpr int kFrameCapacity = 2048;       // ~2.7 s of frames
    static constexpr int kSpectrumCapacity = 49152;   // 0.25 s at 192 kHz
    static constexpr double kGrainsPerSecond = 60.0;  // grain snapshots
    static constexpr int kGrainCapacity = 16;

    // What the editor wants (setWanted).
    static constexpr std::uint32_t kFrames = 1;   // waveform and meters
    static constexpr std::uint32_t kSpectrum = 2; // spectrum samples
    static constexpr std::uint32_t kGrains = 4;   // grain snapshots

    // [NRT] `latencySamples` is how far the aligned dry signal and the layer lag
    // the input (the chain's latency): frame positions are moved back by it.
    void prepare (double sampleRate, int maxChunkSize, int latencySamples);

    // [MSG] What the editor shows now (any of kFrames | kSpectrum | kGrains; 0 when
    // closed or hidden). The first call with something wanted allocates the
    // queues. A part that is turned on starts from fresh data: anything still
    // queued from before is thrown away.
    void setWanted (std::uint32_t parts);
    std::uint32_t getWanted() const noexcept { return wanted.load (std::memory_order_relaxed); }

    // [RT] Once per host block, before the chunks: picks up setWanted.
    void beginBlock() noexcept;
    bool isActive() const noexcept { return active != 0; }

    // [RT] The aligned dry signal and the layer (two channels each), just before
    // the output mix.
    void tapLayer (const float* const* layer, const float* const* dry, int numSamples) noexcept;

    // [RT] The final output of the same chunk; `transport` is its position, and
    // `limiterGainDb` the safety limiter's gain (≤ 0) after it.
    void tapOutput (const float* const* output, int numSamples, const TransportState& transport,
                    float limiterGainDb) noexcept;

    // [RT] After the chunk: about 60 times a second, where every sounding grain
    // is. `sounding` is false while the Grains module is switched off.
    void tapGrains (const dsp::GrainModule& grains, bool sounding, int numSamples) noexcept;

    // [MSG] The consumer side, for UiBridge.
    int readFrames (WaveFrame* destination, int maxFrames) noexcept;
    int readSpectrum (SpectrumSample* destination, int maxSamples) noexcept;
    bool readGrains (GrainSnapshot& latest) noexcept; // the newest snapshot, if any came
    double getSampleRate() const noexcept { return sampleRateForReaders.load (std::memory_order_relaxed); }
    double getFrameSeconds() const noexcept { return frameSecondsForReaders.load (std::memory_order_relaxed); }
    std::uint64_t getDroppedFrames() const noexcept { return frames.getDroppedCount(); }

    int getFrameSamples() const noexcept { return frameSamples; }

private:
    void startFrame (const TransportState& transport, int offset) noexcept;
    void finishFrame() noexcept;
    void resetFrame() noexcept;
    float kWeighted (int channel, float x) noexcept;

    double sampleRate = 48000.0;
    int frameSamples = 64;
    int latencySamples = 0;
    int grainInterval = 800;
    std::atomic<double> sampleRateForReaders{48000.0};
    std::atomic<double> frameSecondsForReaders{64.0 / 48000.0};

    std::atomic<std::uint32_t> wanted{0};
    std::uint32_t active = 0; // the audio thread's copy for this block
    bool allocated = false;   // [MSG]

    util::SpscFifo<WaveFrame> frames;
    util::SpscFifo<SpectrumSample> spectrum;
    util::SpscFifo<GrainSnapshot> grainSnapshots;

    // Per chunk, from tapLayer: each sample's lowest and highest channel.
    std::vector<float> layerLow, layerHigh, dryLow, dryHigh;
    std::vector<SpectrumSample> spectrumScratch;

    // The frame being filled.
    WaveFrame current;
    int currentCount = 0;
    std::int64_t freeRunningSamples = 0; // Thunder's own clock when the host has no transport

    // K-weighting for the loudness sums (as dsp::LoudnessMeter).
    std::array<dsp::LoudnessMeter::Biquad, 2> kStages{};
    std::array<std::array<std::array<double, 2>, 2>, 2> kState{};

    int samplesUntilGrains = 0;
    GrainSnapshot grainScratch;
};

} // namespace thunder::visual
