#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

namespace thunder::music
{

// What the pitch tracker currently hears.
struct PitchEstimate
{
    // A single, clear pitch is present (with hysteresis, so it doesn't flicker).
    bool voiced = false;

    // The note being played, as a whole MIDI note, held with hysteresis so
    // vibrato and small pitch drift don't change it. -1 when not voiced.
    int note = -1;

    // The latest raw estimate, whether voiced or not (0 when there is none).
    float frequencyHz = 0.0f;
    float noteValue = 0.0f; // fractional MIDI note of frequencyHz

    // 0–1: how sure the tracker is that the input is one clear pitch.
    float confidence = 0.0f;
};

// Monophonic pitch tracker for Interval mode (PRD FR-PITCH-010, DEC-014;
// ARCHITECTURE §5.8, §11).
//
// Algorithm: the McLeod Pitch Method (P. McLeod and G. Wyvill, "A Smarter Way
// to Find Pitch", ICMC 2005). Every hop it computes the normalised square
// difference function (NSDF) of the last 40 ms of input, using an FFT (from
// Signalsmith Linear, already used by the pitch shifter) for the
// autocorrelation, and takes the first peak that comes within 90% of the
// highest. The peak's height ("clarity") is the confidence; parabolic
// interpolation refines the period.
//
// Thunder-specific additions:
// - Chords. A chord is close to periodic at a low common period (C–E–G repeats
//   at the C two octaves down), so the NSDF alone would call it a confident low
//   note. Real single notes carry much of their energy in their first two
//   harmonics; a chord "note" has none there. Confidence is scaled down when
//   less than 15% of the energy lies between 0.5× and 2.5× the estimated
//   frequency.
// - Octave errors are harmless for Interval mode (the in-key shift depends only
//   on the pitch class), so no octave correction is attempted.
// - Hysteresis: confident from confidence 0.8, no longer below 0.6. A note is
//   only reported once two analyses in a row with confidence 0.8 or more agree
//   on it, and the held note changes only when the estimate is more than 0.8
//   semitones away from it for two such analyses in a row. During a legato change the window briefly holds
//   both notes, which looks like a chord: the tracker may report no note for
//   those few milliseconds rather than a wrong one.
// - High sample rates are averaged down to 44.1–48 kHz first, so the cost and
//   the window length in time are the same at every rate.
//
// Real-time safe: every buffer is allocated in prepare(); process() never
// allocates or locks.
class PitchTracker
{
public:
    static constexpr double kMinFrequencyHz = 60.0;   // below B1
    static constexpr double kMaxFrequencyHz = 2000.0; // above B6
    static constexpr double kWindowSeconds = 0.04;
    static constexpr int kHopSize = 512; // analysis-rate samples (about 11 ms)

    static constexpr float kVoicedOnConfidence = 0.8f;
    static constexpr float kVoicedOffConfidence = 0.6f;
    static constexpr float kNoteHysteresisSemitones = 0.3f; // beyond the half-way point between notes
    static constexpr int kConfirmAnalyses = 2;
    static constexpr float kMinLowHarmonicShare = 0.15f;
    static constexpr float kSilenceRms = 0.001f; // -60 dBFS

    PitchTracker();
    ~PitchTracker();
    PitchTracker (const PitchTracker&) = delete;
    PitchTracker& operator= (const PitchTracker&) = delete;

    // [NRT] Allocates. Call before process() and whenever the sample rate changes.
    void prepare (double sampleRate);

    // [RT] Forgets all input and the current estimate.
    void reset() noexcept;

    // [RT] Feeds input; channels are averaged to mono. Any number of samples.
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    const PitchEstimate& getEstimate() const noexcept { return estimate; }

    // The longest time, in input samples, from a note starting to the tracker
    // reporting it: one window (by then it holds only the new note), plus one
    // hop for the analysis to happen and one to confirm it. Interval mode must
    // fit this inside the pitch shifter's latency (FR-PITCH-010).
    int getDecisionDelaySamples() const noexcept;

    int getDecimation() const noexcept { return decimation; }
    int getWindowSize() const noexcept { return windowSize; }
    double getAnalysisRate() const noexcept { return analysisRate; }

private:
    void analyse() noexcept;
    void updateEstimate (float frequencyHz, float confidence) noexcept;

    double analysisRate = 44100.0;
    int decimation = 1;
    int windowSize = 0;
    int fftSize = 0;
    int minLag = 2, maxLag = 2;

    struct Fft; // Signalsmith Linear real FFT and its spectrum buffer
    std::unique_ptr<Fft> fft;
    std::vector<float> history; // ring buffer of the last windowSize analysis-rate samples
    std::vector<float> frame;
    std::vector<float> timeData;    // the frame, zero-padded to fftSize
    std::vector<float> correlation; // autocorrelation from the inverse FFT
    std::vector<float> power;
    std::vector<float> nsdf;

    static constexpr int kMaxKeyMaxima = 64;
    std::array<int, kMaxKeyMaxima> keyMaxima{};

    int writePosition = 0;
    int samplesSinceAnalysis = 0;
    float decimationSum = 0.0f;
    int decimationCount = 0;

    PitchEstimate estimate;
    int pendingNote = -1;
    int pendingCount = 0;
    bool confident = false; // the confidence hysteresis state
};

} // namespace thunder::music
