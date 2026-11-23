#pragma once

#include "core/params/Enums.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace thunder::analysis
{

// What the key detector hears (PRD FR-KEY-002/003).
struct KeyEstimate
{
    // Enough tonal audio has been analysed for a key to be shown.
    bool valid = false;

    params::KeyRoot root = params::KeyRoot::C;
    params::Scale scale = params::Scale::Major; // Major or Natural Minor only

    // 0–1: how sure the detector is. UI.md §9.13 shows ≥ 0.7 as confident,
    // 0.4–0.7 as uncertain, below 0.4 as no answer.
    float confidence = 0.0f;

    // Seconds of tonal audio behind the estimate (older audio counts less
    // when the detector forgets).
    double seconds = 0.0;

    bool sameKeyAs (const KeyEstimate& other) const noexcept
    {
        return valid == other.valid && root == other.root && scale == other.scale;
    }
};

// Key and scale from audio, on a background thread (never the audio thread).
//
// Algorithm (ARCHITECTURE §11): a pitch-class profile matched against key
// profiles.
// 1. The input is low-passed and decimated to 11–12 kHz (by 1, 2, 4, 8 or 16
//    depending on the rate), so every rate is analysed the same way.
// 2. Frames of 4096 samples (about 0.34 s) every 2048, Hann-windowed, FFT.
// 3. Chroma from spectral peaks (E. Gómez, "Tonal Description of Music Audio
//    Signals", PhD thesis, UPF 2006, the HPCP): each local maximum between
//    C2 and C7, its frequency refined by parabolic interpolation, adds its
//    amplitude to its pitch class with a cos² weight that falls to zero half
//    a semitone away. Peaks 40 dB or more below the frame's highest are
//    ignored, and so are quiet frames (below −70 dBFS RMS).
// 4. Each frame's chroma is scaled to a maximum of 1 and added to a running
//    total that forgets old frames (a time constant, or never for a whole file).
// 5. The total is correlated (Pearson) with the 24 major and minor key
//    profiles of Krumhansl and Kessler (C. L. Krumhansl, "Cognitive
//    Foundations of Musical Pitch", Oxford University Press 1990, table 2.1).
//    The best match is the key.
// 6. Confidence combines how well the best key fits, how far ahead of the
//    runner-up it is, and how much audio has been heard (see confidenceFor).
//
// Only major and natural minor are told apart: the modes (Dorian …) share
// their notes with a major key and differ mainly in which note feels like
// home, which a pitch-class profile can't settle reliably.
class KeyDetector
{
public:
    // The rate audio is analysed at is the input rate divided by a power of two
    // that brings it closest to this.
    static constexpr double kAnalysisRate = 12000.0;
    static constexpr int kFftOrder = 12; // 4096
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHop = kFftSize / 2;

    // Seconds of tonal audio before an estimate is shown at all.
    static constexpr double kMinSeconds = 2.0;

    KeyDetector();
    ~KeyDetector();

    // [BG] Sets the input rate; clears everything. Allocates.
    void prepare (double inputSampleRate);

    // [BG] Forgets everything heard so far.
    void reset();

    // [BG] How quickly old audio is forgotten: the time constant of the running
    // total in seconds of analysed audio. 0 = never (a whole file).
    void setMemorySeconds (double seconds) noexcept { memorySeconds = seconds; }

    // [BG] Mono audio at the prepared rate.
    void push (const float* samples, int numSamples);

    // [BG] The current estimate.
    KeyEstimate getEstimate() const;

    // The running pitch-class total, C first (for tests).
    const std::array<double, 12>& getChroma() const noexcept { return chroma; }

    // [BG] The key of a whole buffer (all channels mixed), nothing forgotten.
    static KeyEstimate analyse (const juce::AudioBuffer<float>& audio, double sampleRate);

    // The decimation factor for an input rate (1, 2, 4, 8 or 16).
    static int decimationFor (double inputSampleRate) noexcept;

    // Confidence from the best and second-best correlations and the seconds of
    // audio heard. Exposed for tests.
    static float confidenceFor (double best, double runnerUp, double seconds) noexcept;

private:
    void analyseFrame();

    double inputRate = 48000.0;
    int decimation = 4;
    double rate = 12000.0;
    double memorySeconds = 0.0;

    // Two cascaded low-pass sections before decimation.
    std::array<juce::IIRFilter, 2> antiAlias;
    int decimationPhase = 0;

    std::vector<float> frame; // the last kFftSize analysed samples (ring)
    int writePos = 0;
    int sinceLastFrame = 0;
    int filled = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> fftData;

    std::array<double, 12> chroma{};
    double weightedFrames = 0.0; // frames heard, old ones counting less
};

} // namespace thunder::analysis
