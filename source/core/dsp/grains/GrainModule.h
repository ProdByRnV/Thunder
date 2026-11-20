#pragma once

#include "GrainBuffer.h"
#include "GrainWindow.h"

#include "core/engine/Module.h"

#include <array>
#include <cstdint>

namespace thunder::dsp
{

// Thunder's granular engine (PRD §7.4, ARCHITECTURE §5.8).
//
// Design (ROADMAP T-0.3-01), written after studying GRNLR and InstaGrain for
// ideas only (no code taken; GRNLR is GPL-3.0):
//
// - Buffer. Grains play back the layer's own recent input, which a circular
//   buffer (GrainBuffer) holds: at least 4 s plus the longest grain, sized in
//   prepare() (FR-GRAIN-001). The references play a loaded sample instead; live
//   input adds one rule: a grain may only read audio that has already been
//   written. Each grain therefore starts reading one grain length behind "now",
//   so at rate 1 it ends where "now" was when it started. (Grains played faster
//   than rate 1 will need to start further back. That comes with Pitch Spread.)
//
// - Voice pool. 64 Grain slots, allocated with the module (FR-GRAIN-007). A new
//   grain takes a free slot. If none is free it is skipped and counted, never
//   taken over: cutting off a sounding grain would click. Within the parameter
//   ranges at most 50 grains overlap (100 grains/s × 0.5 s), so skipping only
//   happens beyond them.
//
// - Scheduler. A fractional accumulator advanced by density / sample rate every
//   sample; a grain starts each time it passes 1. The average density is exact,
//   and each grain starts on its own sample inside a chunk, so the result does
//   not depend on the host's block size. (A whole-sample countdown, as in
//   InstaGrain, rounds the spacing and drifts.)
//
// - Window. A Hann window from a table (GrainWindow, FR-GRAIN-003): it starts and
//   ends at exactly zero, so grains never click, and evenly spaced overlapping
//   grains sum to a constant.
//
// - Level. With N grains overlapping on average (density × size) and no spread,
//   every grain reads the same delayed input, so they add up exactly: the cloud
//   is the input times the sum of the windows, about 0.5·N (the Hann window's
//   mean). Each grain is scaled by min(1, 2/N) so dense clouds aren't louder
//   than the input. Sparse clouds aren't boosted; they are meant to have gaps.
//   Position and pitch spread (2D) make grains unrelated, and unrelated grains
//   add in power (√(0.375·N), 0.375 being the window's mean square), so 2D
//   must blend towards that scaling as the spread grows (ROADMAP T-0.3-04).
//
// - Cost. Per sample, one window lookup and one buffer read per channel for
//   each sounding grain; nothing allocates, locks or waits (`[RT]`).
//
// Current scope (subphase 2C): Size and Density drive the grains. Amount, the
// spreads (position, pitch, pan), reverse playback and cubic interpolation for
// pitched grains follow in 2D (ROADMAP T-0.3-04/05), as does placing the module
// in the chain (T-0.3-06). Until then the module outputs the grain cloud alone.
class GrainModule final : public Module
{
public:
    static constexpr int kMaxGrains = 64;
    static constexpr double kBufferSeconds = 4.0;
    static constexpr double kMaxGrainSeconds = 0.5; // grainSize's maximum (500 ms)

    // What drives the grains. Values outside the parameter ranges are accepted
    // (tests use them); sizes are clamped to 1 sample … kMaxGrainSeconds.
    struct Settings
    {
        double sizeSeconds = 0.08;
        double density = 20.0; // grains per second
    };

    struct Grain
    {
        bool active = false;
        int startOffset = 0;        // sample in the current chunk where it starts
        std::int64_t readStart = 0; // absolute buffer index of its first sample
        int length = 0;             // samples
        int elapsed = 0;            // samples played
        float windowStep = 0.0f;    // window table segments per sample
        float gain = 1.0f;
    };

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;

    // Reads Size and Density from the parameters.
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // [RT] Writes the block into the buffer and replaces it with the grain cloud.
    void process (juce::dsp::AudioBlock<float>& block, const Settings& settings) noexcept;

    // Grains play past input: no latency.
    int getLatencySamples() const noexcept override { return 0; }

    // A grain started just before the input stops still has to finish.
    double getTailSeconds() const noexcept override { return kMaxGrainSeconds; }

    int getActiveGrains() const noexcept;
    std::uint64_t getGrainsStarted() const noexcept { return grainsStarted; }
    std::uint64_t getGrainsSkipped() const noexcept { return grainsSkipped; }
    const GrainBuffer& getBuffer() const noexcept { return buffer; }

    // The per-grain level for a given overlap (see "Level" above).
    static float gainForOverlap (double overlap) noexcept;

private:
    void startGrain (int offset, std::int64_t now, int length, float gain) noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;
    int maxGrainSamples = 1;

    GrainBuffer buffer;
    GrainWindow window;
    std::array<Grain, kMaxGrains> grains{};

    double schedulerPhase = 0.0;
    std::uint64_t grainsStarted = 0;
    std::uint64_t grainsSkipped = 0;
};

} // namespace thunder::dsp
