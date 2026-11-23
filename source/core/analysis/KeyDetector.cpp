#include "KeyDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace thunder::analysis
{

namespace
{
// Krumhansl and Kessler's probe-tone ratings, tonic first (Krumhansl 1990, table 2.1).
constexpr std::array<double, 12> kMajorProfile{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinorProfile{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

// The range chroma is taken from: C2 to C7.
constexpr double kLowestHz = 65.41;
constexpr double kHighestHz = 2093.0;

// Peaks this far below the frame's highest don't count.
constexpr float kPeakFloor = 0.01f; // −40 dB

// Frames quieter than this (RMS) are skipped, so silence neither adds nor
// makes the detector forget.
constexpr float kSilenceRms = 3.16e-4f; // −70 dBFS

double pearson (const std::array<double, 12>& a, const std::array<double, 12>& profile, int rotation) noexcept
{
    double meanA = 0.0, meanB = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        meanA += a[static_cast<std::size_t> (i)];
        meanB += profile[static_cast<std::size_t> (i)];
    }
    meanA /= 12.0;
    meanB /= 12.0;

    double num = 0.0, varA = 0.0, varB = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        // The profile's degree for pitch class pc in the key whose tonic is `rotation`.
        const auto x = a[static_cast<std::size_t> (pc)] - meanA;
        const auto y = profile[static_cast<std::size_t> ((pc - rotation + 12) % 12)] - meanB;
        num += x * y;
        varA += x * x;
        varB += y * y;
    }

    return varA > 0.0 && varB > 0.0 ? num / std::sqrt (varA * varB) : 0.0;
}
} // namespace

KeyDetector::KeyDetector()
{
    prepare (48000.0);
}

KeyDetector::~KeyDetector() = default;

int KeyDetector::decimationFor (double inputSampleRate) noexcept
{
    int factor = 1;
    while (factor < 16 && inputSampleRate / (factor * 2) >= kAnalysisRate * 0.75)
        factor *= 2;
    return factor;
}

void KeyDetector::prepare (double inputSampleRate)
{
    jassert (inputSampleRate > 0.0);
    inputRate = inputSampleRate;
    decimation = decimationFor (inputRate);
    rate = inputRate / decimation;

    // Fourth-order Butterworth low-pass at 40% of the new Nyquist rate: what
    // folds back lies far above the range the chroma uses.
    const auto cutoff = 0.4 * rate;
    antiAlias[0].setCoefficients (juce::IIRCoefficients::makeLowPass (inputRate, cutoff, 0.5412));
    antiAlias[1].setCoefficients (juce::IIRCoefficients::makeLowPass (inputRate, cutoff, 1.3066));

    if (fft == nullptr)
    {
        fft = std::make_unique<juce::dsp::FFT> (kFftOrder);
        window.resize (kFftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), kFftSize,
                                                                  juce::dsp::WindowingFunction<float>::hann, false);
        frame.assign (kFftSize, 0.0f);
        fftData.assign (2 * kFftSize, 0.0f);
    }

    reset();
}

void KeyDetector::reset()
{
    for (auto& f : antiAlias)
        f.reset();
    decimationPhase = 0;
    std::fill (frame.begin(), frame.end(), 0.0f);
    writePos = 0;
    sinceLastFrame = 0;
    filled = 0;
    chroma.fill (0.0);
    weightedFrames = 0.0;
}

void KeyDetector::push (const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        auto x = std::isfinite (samples[i]) ? samples[i] : 0.0f;
        if (decimation > 1)
        {
            x = antiAlias[1].processSingleSampleRaw (antiAlias[0].processSingleSampleRaw (x));
            if (++decimationPhase < decimation)
                continue;
            decimationPhase = 0;
        }

        frame[static_cast<std::size_t> (writePos)] = x;
        writePos = (writePos + 1) % kFftSize;
        filled = std::min (filled + 1, kFftSize);

        if (++sinceLastFrame >= kHop && filled == kFftSize)
        {
            sinceLastFrame = 0;
            analyseFrame();
        }
    }
}

void KeyDetector::analyseFrame()
{
    // Oldest sample first, windowed.
    double energy = 0.0;
    for (int i = 0; i < kFftSize; ++i)
    {
        const auto x = frame[static_cast<std::size_t> ((writePos + i) % kFftSize)];
        energy += static_cast<double> (x) * x;
        fftData[static_cast<std::size_t> (i)] = x * window[static_cast<std::size_t> (i)];
    }
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

    if (std::sqrt (energy / kFftSize) < kSilenceRms)
        return;

    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

    const auto binHz = rate / kFftSize;
    const auto first = std::max (2, static_cast<int> (std::floor (kLowestHz / binHz)));
    const auto last = std::min (kFftSize / 2 - 2, static_cast<int> (std::ceil (kHighestHz / binHz)));

    float highest = 0.0f;
    for (int k = first; k <= last; ++k)
        highest = std::max (highest, fftData[static_cast<std::size_t> (k)]);
    if (highest <= 0.0f)
        return;

    std::array<double, 12> frameChroma{};
    for (int k = first; k <= last; ++k)
    {
        const auto m = fftData[static_cast<std::size_t> (k)];
        if (m < highest * kPeakFloor || m <= fftData[static_cast<std::size_t> (k - 1)] ||
            m < fftData[static_cast<std::size_t> (k + 1)])
            continue;

        // Parabolic interpolation on log magnitudes refines the peak's frequency.
        const auto a = std::log (fftData[static_cast<std::size_t> (k - 1)] + 1.0e-12);
        const auto b = std::log (static_cast<double> (m) + 1.0e-12);
        const auto c = std::log (fftData[static_cast<std::size_t> (k + 1)] + 1.0e-12);
        const auto denom = a - 2.0 * b + c;
        const auto offset = std::abs (denom) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
        const auto hz = (k + offset) * binHz;
        if (hz < kLowestHz || hz > kHighestHz)
            continue;

        const auto note = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto nearest = std::round (note);
        const auto weight = std::pow (std::cos (juce::MathConstants<double>::pi * (note - nearest)), 2.0);
        const auto pc = (static_cast<int> (nearest) % 12 + 12) % 12;
        frameChroma[static_cast<std::size_t> (pc)] += weight * m;
    }

    const auto top = *std::max_element (frameChroma.begin(), frameChroma.end());
    if (top <= 0.0)
        return;

    // Each frame counts the same however loud it is.
    const auto keep = memorySeconds > 0.0 ? std::exp (-(kHop / rate) / memorySeconds) : 1.0;
    for (std::size_t pc = 0; pc < 12; ++pc)
        chroma[pc] = chroma[pc] * keep + frameChroma[pc] / top;
    weightedFrames = weightedFrames * keep + 1.0;
}

float KeyDetector::confidenceFor (double best, double runnerUp, double seconds) noexcept
{
    // How well the best key fits: a diatonic melody correlates at 0.75–0.95,
    // chromatic material or noise far lower.
    const auto fit = juce::jlimit (0.0, 1.0, (best - 0.45) / 0.35);
    // How far ahead of the runner-up it is. The relative major or minor (the
    // same notes) is often close behind; that halves the confidence at most.
    const auto margin = juce::jlimit (0.0, 1.0, (best - runnerUp) / 0.1);
    // How much has been heard: full after 8 s.
    const auto heard = juce::jlimit (0.0, 1.0, seconds / 8.0);
    return static_cast<float> (fit * (0.5 + 0.5 * margin) * heard);
}

KeyEstimate KeyDetector::getEstimate() const
{
    KeyEstimate e;
    e.seconds = weightedFrames * kHop / rate;
    if (e.seconds < kMinSeconds)
        return e;

    double best = -2.0, runnerUp = -2.0;
    int bestKey = 0;
    for (int key = 0; key < 24; ++key)
    {
        const auto r = pearson (chroma, key < 12 ? kMajorProfile : kMinorProfile, key % 12);
        if (r > best)
        {
            runnerUp = best;
            best = r;
            bestKey = key;
        }
        else if (r > runnerUp)
        {
            runnerUp = r;
        }
    }

    e.valid = true;
    e.root = static_cast<params::KeyRoot> (bestKey % 12);
    e.scale = bestKey < 12 ? params::Scale::Major : params::Scale::NaturalMinor;
    e.confidence = confidenceFor (best, runnerUp, e.seconds);
    return e;
}

KeyEstimate KeyDetector::analyse (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    KeyDetector detector;
    detector.prepare (sampleRate);
    detector.setMemorySeconds (0.0);

    constexpr int kBlock = 4096;
    std::vector<float> mono (kBlock);
    const auto channels = std::max (1, audio.getNumChannels());
    for (int start = 0; start < audio.getNumSamples(); start += kBlock)
    {
        const auto n = std::min (kBlock, audio.getNumSamples() - start);
        std::fill (mono.begin(), mono.begin() + n, 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono.data(), audio.getReadPointer (ch, start),
                                                          1.0f / static_cast<float> (channels), n);
        detector.push (mono.data(), n);
    }

    return detector.getEstimate();
}

} // namespace thunder::analysis
