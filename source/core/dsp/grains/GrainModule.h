#pragma once

#include "GrainBuffer.h"
#include "GrainWindow.h"

#include "core/engine/Module.h"
#include "core/util/Rng.h"
#include "core/util/Smoother.h"

#include <array>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{

// Thunder's granular engine (PRD §7.4, ARCHITECTURE §5.8).
//
// Design (ROADMAP T-0.3-01…05), written after studying GRNLR and InstaGrain for
// ideas only (no code taken; GRNLR is GPL-3.0):
//
// - Buffer. Grains play back the layer's own recent input, which a circular
//   buffer (GrainBuffer) holds: 4 s of history for Position Spread plus room for
//   the longest grain read at the highest rate, sized in prepare()
//   (FR-GRAIN-001). The references play a loaded sample instead; live input adds
//   one rule: a grain may only read audio that has already been written, and
//   that won't be overwritten before the grain ends. So each grain starts
//   reading at least as far behind "now" as the stretch of input it covers
//   (one grain length at rate 1; up to two at +12 semitones), and Position
//   Spread moves it further back.
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
// - Spreads (FR-GRAIN-002, PRD DEC-040). Each grain draws its own random values
//   from a seeded Rng when it starts, so settings changes affect only new grains
//   and never click:
//   - Position Spread moves the grain's start back by up to spread² × 4 s
//     (25% → 0.25 s, 50% → 1 s, 100% → 4 s). The square gives fine control over
//     the short smears that keep a melody recognisable.
//   - Pitch Spread transposes it by a random amount within ± the spread, in
//     semitones, by reading the buffer faster or slower.
//   - Pan Spread places it at a random position within ± the spread
//     (equal-power, unity at the centre).
//   - Reverse Probability is the chance it plays backwards.
//
// - Interpolation. A grain at rate 1 reads whole samples. Pitched grains read
//   between samples with 4-point cubic Hermite interpolation (FR-GRAIN-008).
//
// - Level (PRD DEC-040). With N grains overlapping on average (density × size)
//   and no spread, every grain reads the same delayed input, so they add up in
//   amplitude: the cloud is the input times the sum of the windows, about 0.5·N
//   (the Hann window's mean). Unrelated grains add in power instead: 0.375·N,
//   the window's mean square. With pairs correlating by ρ on average, the
//   cloud's power is (1 − ρ)·0.375·N + ρ·0.25·N², and the cloud is scaled by
//   one over its square root (gainForOverlap).
//   How related the grains are depends on the material as much as on the
//   spreads: noise decorrelates after a sample of offset, a low melody only
//   after a period of its note. So ρ is measured, not guessed: per sample, the
//   cloud's power against the power of its separate grains, smoothed over
//   100 ms (until there is something to measure, ρ = 1, the quietest
//   assumption). The gain itself glides over 50 ms, so changing Size or Density
//   never steps the level. Sparse clouds aren't boosted; their gaps are part of
//   the sound.
//
// - Amount blends the input with the cloud, equal-power, smoothed (FR-GRAIN-002).
//
// - Cost. Per sample, one window lookup and one buffer read (cubic when pitched)
//   per channel for each sounding grain, plus two sums for the level; nothing
//   allocates, locks or waits (`[RT]`).
//
// On/off (grainOn) is handled by the engine's ModuleSlot.
class GrainModule final : public Module
{
public:
    static constexpr int kMaxGrains = 64;
    static constexpr double kBufferSeconds = 4.0;          // how far back Position Spread reaches
    static constexpr double kMaxGrainSeconds = 0.5;        // grainSize's maximum (500 ms)
    static constexpr double kMaxPitchSpreadSemitones = 12; // grainPitchSpread's maximum
    static constexpr double kMaxRate = 2.0;                // a grain transposed +12 semitones

    // What drives the grains. Values outside the parameter ranges are accepted
    // (tests use them); they are clamped where it matters for safety. Leading
    // values can be given alone ({size, density}); the rest keep their defaults
    // (no spread, grains only).
    struct Settings
    {
        constexpr Settings (double size = 0.08, double grainsPerSecond = 20.0, float amountOfGrains = 1.0f,
                            float position = 0.0f, float pitch = 0.0f, float pan = 0.0f, float reverse = 0.0f) noexcept
            : sizeSeconds (size)
            , density (grainsPerSecond)
            , amount (amountOfGrains)
            , positionSpread (position)
            , pitchSpread (pitch)
            , panSpread (pan)
            , reverseProbability (reverse)
        {
        }

        double sizeSeconds = 0.08;
        double density = 20.0;           // grains per second
        float amount = 1.0f;             // 0 = the input only, 1 = the grains only
        float positionSpread = 0.0f;     // 0 … 1
        float pitchSpread = 0.0f;        // semitones, each grain within ± this
        float panSpread = 0.0f;          // 0 … 1
        float reverseProbability = 0.0f; // 0 … 1
    };

    struct Grain
    {
        bool active = false;
        bool interpolate = false;                             // pitched: reads between samples
        int startOffset = 0;                                  // sample in the current chunk where it starts
        int length = 0;                                       // output samples
        int elapsed = 0;                                      // output samples played
        std::int64_t base = 0;                                // absolute buffer index its reads are measured from
        double firstPosition = 0.0;                           // samples after `base` of its first read
        double step = 1.0;                                    // read speed; negative when reversed
        float windowStep = 0.0f;                              // window table segments per sample
        std::array<float, GrainBuffer::kMaxChannels> gains{}; // its pan, per channel
    };

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;

    // Reads the grain parameters.
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // [RT] Writes the block into the buffer and blends the grain cloud into it.
    void process (juce::dsp::AudioBlock<float>& block, const Settings& settings) noexcept;

    // Grains play past input: no latency.
    int getLatencySamples() const noexcept override { return 0; }

    // After the input stops, grains keep finding it in the buffer for as long as
    // Position Spread reaches back, plus the longest stretch a grain reads and
    // plays.
    double getTailSeconds() const noexcept override { return kBufferSeconds + 3.0 * kMaxGrainSeconds; }

    int getActiveGrains() const noexcept;
    std::uint64_t getGrainsStarted() const noexcept { return grainsStarted; }
    std::uint64_t getGrainsSkipped() const noexcept { return grainsSkipped; }
    std::uint64_t getGrainsReversed() const noexcept { return grainsReversed; }
    const GrainBuffer& getBuffer() const noexcept { return buffer; }
    const std::array<Grain, kMaxGrains>& getGrains() const noexcept { return grains; }

    // The measured correlation between grains, 0 (unrelated) … 1 (identical),
    // and the gain the cloud currently gets (see "Level" above).
    double getCorrelation() const noexcept { return correlation; }
    double getCloudGain() const noexcept { return cloudGain; }

    // How far back Position Spread (0 … 1) may move a grain's start.
    static double positionSpreadSeconds (float spread) noexcept;

    // The cloud's gain for an overlap N whose grains correlate by `correlation`
    // on average: identical copies (1) by default.
    static float gainForOverlap (double overlap, double correlation = 1.0) noexcept;

private:
    void processChunk (float* const* channels, int numSamples, const Settings& settings) noexcept;
    void startGrain (int offset, std::int64_t now, int length, double maxOffsetSamples,
                     const Settings& settings) noexcept;
    void render (int numSamples) noexcept;
    void normalise (int numSamples, double overlap) noexcept;
    void blend (float* const* channels, int numSamples) noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;
    int maxChunk = 256;
    int maxGrainSamples = 1;

    GrainBuffer buffer;
    GrainWindow window;
    std::array<Grain, kMaxGrains> grains{};
    std::array<std::vector<float>, GrainBuffer::kMaxChannels> cloud;

    // For measuring the correlation, per sample of the first channel: the sum
    // of the grains' squares and of their magnitudes.
    std::vector<float> grainSquares, grainMagnitudes;
    double crossPower = 0.0;    // smoothed (Σc)² − Σc²: what the pairs add
    double maxCrossPower = 0.0; // smoothed (Σ|c|)² − Σc²: what identical pairs would add
    double correlation = 1.0;
    double cloudGain = 1.0;
    double correlationCoefficient = 0.0; // one-pole smoothing per sample
    double gainCoefficient = 0.0;
    bool snapGain = true;

    util::Rng rng;
    util::LinearSmoother amount; // 0 … 1
    bool firstChunk = true;

    double schedulerPhase = 0.0;
    std::uint64_t grainsStarted = 0;
    std::uint64_t grainsSkipped = 0;
    std::uint64_t grainsReversed = 0;
};

} // namespace thunder::dsp
