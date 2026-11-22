#pragma once

#include "core/visual/VisualFrames.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace thunder::ui
{

// The spectrum view's analysis (PRD UI-VIS-003, ARCHITECTURE §12.3), on the
// message thread: the newest stretch of the dry signal and the layer, Hann
// windowed, through an FFT of about 85 ms (4096 points at 44.1/48 kHz, more
// at higher rates), read at kPoints log-spaced frequencies from 20 Hz to 20 kHz.
//
// Each point is the power in a 1/6-octave band around it, like a real-time
// analyser: a sine reads its own level (in dBFS) wherever it falls, and pink
// noise reads flat. Where a band is narrower than the FFT's bins (the low end)
// the nearest bins are interpolated instead. Levels rise at once and fall with
// a 300 ms release (UI.md §8.2).
class SpectrumAnalyser
{
public:
    static constexpr int kPoints = 384;
    static constexpr float kFloorDb = -120.0f;

    // [MSG] A different sample rate starts afresh.
    // `detail` moves the FFT size a power of two up or down from the usual (Quality:
    // Eco −1, High +1).
    void prepare (double sampleRate, int detail = 0);

    // [MSG] New samples from the audio thread.
    void push (const visual::SpectrumSample* samples, int count);

    // [MSG] Analyses the newest samples if enough have come, and lets the
    // levels fall for `seconds`. Returns true if anything shown changed.
    bool update (double seconds);

    const std::array<float, kPoints>& getDryDb() const noexcept { return dry.shown; }
    const std::array<float, kPoints>& getLayerDb() const noexcept { return layer.shown; }
    static float frequencyOf (int point) noexcept; // Hz
    double getSampleRate() const noexcept { return sampleRate; }
    int getFftSize() const noexcept { return fftSize; }

private:
    struct Channel
    {
        std::vector<float> ring; // the last fftSize samples
        std::array<float, kPoints> shown{};
    };

    void analyse (const std::vector<float>& ring, std::array<float, kPoints>& out);
    void bandPowers (std::array<float, kPoints>& out);

    double sampleRate = 0.0;
    int detail = 0;
    int fftSize = 4096;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> work;
    std::vector<double> prefix; // running sums of bin power, for the bands
    Channel dry, layer;
    int writeIndex = 0;
    int fresh = 0;              // samples since the last analysis
    double sinceAnalysis = 0.0; // seconds since the last analysis
    std::array<float, kPoints> latest{};
};

} // namespace thunder::ui
