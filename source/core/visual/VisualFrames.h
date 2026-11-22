#pragma once

#include <array>
#include <cstdint>

// What the audio thread tells the visualiser (PRD UI-VIS-001…008, ARCHITECTURE
// §6.3, §7.1). All plain data: copied through lock-free queues, never shared.

namespace thunder::visual
{

// What the modules with live displays are doing (UI.md §11.4, §11.8, §11.9),
// read once per chunk from the engine.
struct ModuleActivity
{
    // How far Static moves each target now (StaticModulator::Frame, in its
    // order: Position, Pitch, Filter, Pan, Level).
    std::array<float, 5> staticValues{};
    float duckReductionDb = 0.0f; // Duck's reduction (≥ 0)
    float shapePhase = 0.0f;      // where Shape is in its cycle, 0 … 1
    float shapeGain = 1.0f;       // the gain Shape applies
};

// About 1.3 ms of audio (64 samples at 48 kHz): the waveform's min/max and the
// meters' sums over that stretch. The audio before the output mix (aligned dry
// and the layer) and the final output are both described, so one frame feeds
// the waveform view and the meters.
struct WaveFrame
{
    // Where the frame's first sample sits in the song, in quarter notes, as the
    // listener hears it: the dry signal and the layer are already delayed by
    // the chain's latency, so the position is moved back by it too. Without a
    // host transport Thunder counts its own time at 120 BPM.
    double ppq = 0.0;
    double barStartPpq = 0.0; // start of the bar containing `ppq`
    float bpm = 120.0f;

    // Aligned dry signal and layer (after Auto Gain and Layer Level), both
    // channels: the lowest and highest sample.
    float dryMin = 0.0f, dryMax = 0.0f;
    float layerMin = 0.0f, layerMax = 0.0f;

    // The output (after the limiter).
    float peakLeft = 0.0f, peakRight = 0.0f; // highest |sample|
    float loudnessPower = 0.0f;              // Σ of K-weighted (L² + R²) over the frame (ITU-R BS.1770)
    float sumLL = 0.0f, sumRR = 0.0f, sumLR = 0.0f;
    float limiterReductionDb = 0.0f; // the safety limiter's largest reduction (≥ 0)

    // For the modules' live displays: Duck's largest reduction over the frame,
    // the rest as in the frame's last chunk.
    ModuleActivity activity;

    std::uint16_t numSamples = 0;
    std::uint8_t timeSigNumerator = 4;
    std::uint8_t timeSigDenominator = 4;
    std::uint8_t flags = 0;

    static constexpr std::uint8_t kMoving = 1;    // the song position is advancing (playing, or no transport)
    static constexpr std::uint8_t kHostTempo = 2; // the tempo and meter came from the host

    bool isMoving() const noexcept { return (flags & kMoving) != 0; }
    bool hasHostTempo() const noexcept { return (flags & kHostTempo) != 0; }
    double quarterNotesPerBar() const noexcept { return timeSigNumerator * 4.0 / timeSigDenominator; }
};

// One sample of the aligned dry signal and the layer, each as (L + R) / 2, for
// the spectrum view's FFT (which runs on the message thread, ARCHITECTURE §12.3).
struct SpectrumSample
{
    float dry = 0.0f;
    float layer = 0.0f;
};

// One sounding grain (UI-VIS-008): where it reads, at what pitch, where it sits
// in the stereo field and how far through its window it is.
struct GrainSpark
{
    float ageSeconds = 0.0f;      // how far behind the newest input it reads now
    float startAgeSeconds = 0.0f; // … and where it started reading
    float semitones = 0.0f;       // its own transposition
    float pan = 0.0f;             // −1 (left) … +1 (right)
    float window = 0.0f;          // its window's current level, 0 … 1
    float sizeSeconds = 0.0f;     // its length
};

// Every sounding grain at one moment (published about 60 times a second).
struct GrainSnapshot
{
    static constexpr int kMaxSparks = 64;

    std::array<GrainSpark, kMaxSparks> sparks{};
    int count = 0;
};

} // namespace thunder::visual
