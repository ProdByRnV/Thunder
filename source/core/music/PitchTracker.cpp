#include "PitchTracker.h"

#include "Scale.h"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace thunder::music
{

namespace
{
// A key maximum must reach this fraction of the highest one to be chosen (the
// "k" constant of the McLeod paper).
constexpr float kPeakThreshold = 0.9f;
} // namespace

struct PitchTracker::Fft
{
    signalsmith::linear::RealFFT<float> transform;
    std::vector<std::complex<float>> spectrum; // fftSize / 2 bins; bin 0 holds DC and Nyquist
};

PitchTracker::PitchTracker() = default;
PitchTracker::~PitchTracker() = default;

void PitchTracker::prepare (double sampleRate)
{
    jassert (sampleRate > 0.0);

    // Average 88.2/96 kHz down by 2 and 176.4/192 kHz by 4: pitch needs no more
    // than 44.1–48 kHz, and the cost stays the same at every rate.
    decimation = std::max (1, static_cast<int> (sampleRate / 44100.0 + 1.0e-6));
    analysisRate = sampleRate / decimation;

    windowSize = static_cast<int> (std::lround (analysisRate * kWindowSeconds));

    // The NSDF is reliable up to lags of half the window.
    maxLag = std::min (static_cast<int> (std::ceil (analysisRate / kMinFrequencyHz)), windowSize / 2);
    minLag = std::max (2, static_cast<int> (std::floor (analysisRate / kMaxFrequencyHz)));

    // Zero-padded to at least twice the window, so the FFT autocorrelation
    // doesn't wrap around.
    const auto size = signalsmith::linear::RealFFT<float>::fastSizeAbove (static_cast<std::size_t> (2 * windowSize));
    fftSize = static_cast<int> (size);
    fft = std::make_unique<Fft>();
    fft->transform.resize (size);
    fft->spectrum.assign (size / 2, {});

    history.assign (static_cast<std::size_t> (windowSize), 0.0f);
    frame.assign (static_cast<std::size_t> (windowSize), 0.0f);
    timeData.assign (size, 0.0f); // the zero padding stays zero
    correlation.assign (size, 0.0f);
    power.assign (size / 2 + 1, 0.0f);
    nsdf.assign (static_cast<std::size_t> (maxLag + 2), 0.0f);

    reset();
}

void PitchTracker::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    writePosition = 0;
    samplesSinceAnalysis = 0;
    decimationSum = 0.0f;
    decimationCount = 0;

    estimate = {};
    pendingNote = -1;
    pendingCount = 0;
    confident = false;
}

int PitchTracker::getDecisionDelaySamples() const noexcept
{
    return (windowSize + kConfirmAnalyses * kHopSize) * decimation;
}

void PitchTracker::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (windowSize == 0 || numChannels <= 0)
        return;

    const auto channelScale = 1.0f / static_cast<float> (numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += channels[ch][i];

        decimationSum += mono * channelScale;
        if (++decimationCount < decimation)
            continue;

        history[static_cast<std::size_t> (writePosition)] = decimationSum / static_cast<float> (decimation);
        decimationSum = 0.0f;
        decimationCount = 0;

        if (++writePosition == windowSize)
            writePosition = 0;

        if (++samplesSinceAnalysis == kHopSize)
        {
            samplesSinceAnalysis = 0;
            analyse();
        }
    }
}

void PitchTracker::analyse() noexcept
{
    const auto w = static_cast<std::size_t> (windowSize);

    // Oldest sample first.
    const auto split = static_cast<std::size_t> (writePosition);
    std::copy (history.begin() + static_cast<std::ptrdiff_t> (split), history.end(), frame.begin());
    std::copy (history.begin(), history.begin() + static_cast<std::ptrdiff_t> (split),
               frame.begin() + static_cast<std::ptrdiff_t> (w - split));

    double energy = 0.0;
    for (const auto x : frame)
        energy += static_cast<double> (x) * static_cast<double> (x);

    if (!std::isfinite (energy))
    {
        // Only reachable if unsanitised input is fed in directly.
        reset();
        return;
    }

    if (std::sqrt (energy / static_cast<double> (w)) < kSilenceRms)
    {
        updateEstimate (0.0f, 0.0f);
        return;
    }

    // Autocorrelation r(τ) by FFT: inverse transform of the power spectrum.
    std::copy (frame.begin(), frame.end(), timeData.begin());
    auto& spectrum = fft->spectrum;
    fft->transform.fft (timeData.data(), spectrum.data());

    // Bin 0 packs the (real) DC and Nyquist values together.
    const auto half = spectrum.size();
    power[0] = spectrum[0].real() * spectrum[0].real();
    power[half] = spectrum[0].imag() * spectrum[0].imag();
    spectrum[0] = {power[0], power[half]};
    double totalPower = power[half];
    for (std::size_t k = 1; k < half; ++k)
    {
        power[k] = std::norm (spectrum[k]);
        totalPower += power[k];
        spectrum[k] = {power[k], 0.0f};
    }

    fft->transform.ifft (spectrum.data(), correlation.data());

    // The inverse is unscaled; r(0) must equal the energy.
    if (!(correlation[0] > 0.0f))
    {
        updateEstimate (0.0f, 0.0f);
        return;
    }
    const auto scale = energy / static_cast<double> (correlation[0]);

    // NSDF n(τ) = 2 r(τ) / m(τ), with m(τ) the energy of both overlapping parts.
    double m = 2.0 * energy;
    nsdf[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const auto t = static_cast<std::size_t> (tau);
        const auto a = static_cast<double> (frame[t - 1]);
        const auto b = static_cast<double> (frame[w - t]);
        m -= a * a + b * b;
        const auto r = static_cast<double> (correlation[t]) * scale;
        nsdf[t] = m > 1.0e-12 ? static_cast<float> (2.0 * r / m) : 0.0f;
    }
    nsdf[static_cast<std::size_t> (maxLag) + 1] = 0.0f;

    // Key maxima: the highest point of each positive region after the first
    // negative-going zero crossing.
    int numMaxima = 0;
    float highest = 0.0f;
    int tau = 1;
    while (tau <= maxLag && nsdf[static_cast<std::size_t> (tau)] > 0.0f)
        ++tau;

    while (tau <= maxLag && numMaxima < kMaxKeyMaxima)
    {
        while (tau <= maxLag && nsdf[static_cast<std::size_t> (tau)] <= 0.0f)
            ++tau;

        int best = -1;
        while (tau <= maxLag && nsdf[static_cast<std::size_t> (tau)] > 0.0f)
        {
            if (best < 0 || nsdf[static_cast<std::size_t> (tau)] > nsdf[static_cast<std::size_t> (best)])
                best = tau;
            ++tau;
        }

        // A region still rising at maxLag has no real maximum inside the range.
        if (best >= minLag && best < maxLag)
        {
            keyMaxima[static_cast<std::size_t> (numMaxima++)] = best;
            highest = std::max (highest, nsdf[static_cast<std::size_t> (best)]);
        }
    }

    if (numMaxima == 0 || highest <= 0.0f)
    {
        updateEstimate (0.0f, std::max (highest, 0.0f));
        return;
    }

    int chosen = keyMaxima[0];
    for (int i = 0; i < numMaxima; ++i)
    {
        const auto lag = keyMaxima[static_cast<std::size_t> (i)];
        if (nsdf[static_cast<std::size_t> (lag)] >= kPeakThreshold * highest)
        {
            chosen = lag;
            break;
        }
    }

    // Parabolic interpolation of the peak.
    const auto y0 = nsdf[static_cast<std::size_t> (chosen - 1)];
    const auto y1 = nsdf[static_cast<std::size_t> (chosen)];
    const auto y2 = nsdf[static_cast<std::size_t> (chosen + 1)];
    const auto curvature = y0 - 2.0f * y1 + y2;
    float offset = 0.0f, clarity = y1;
    if (curvature < 0.0f)
    {
        offset = std::clamp (0.5f * (y0 - y2) / curvature, -0.5f, 0.5f);
        clarity = y1 - 0.25f * (y0 - y2) * offset;
    }
    clarity = std::clamp (clarity, 0.0f, 1.0f);

    const auto frequency = static_cast<float> (analysisRate / (static_cast<double> (chosen) + offset));

    // Chord check: share of the energy around the first two harmonics.
    const auto binHz = analysisRate / static_cast<double> (fftSize);
    const auto lowBin = static_cast<std::size_t> (std::max (1.0, std::floor (0.5 * frequency / binHz)));
    const auto highBin = std::min (power.size() - 1, static_cast<std::size_t> (std::ceil (2.5 * frequency / binHz)));
    double lowPower = 0.0;
    for (auto k = lowBin; k <= highBin; ++k)
        lowPower += power[k];

    const auto share = totalPower > 0.0 ? static_cast<float> (lowPower / totalPower) : 0.0f;
    const auto confidence = clarity * std::min (1.0f, share / kMinLowHarmonicShare);

    updateEstimate (frequency, confidence);
}

void PitchTracker::updateEstimate (float frequencyHz, float confidence) noexcept
{
    estimate.frequencyHz = frequencyHz;
    estimate.noteValue = frequencyHz > 0.0f ? static_cast<float> (frequencyToNote (frequencyHz)) : 0.0f;
    estimate.confidence = confidence;

    if (frequencyHz <= 0.0f)
        confident = false;
    else
        confident = confidence >= (confident ? kVoicedOffConfidence : kVoicedOnConfidence);

    if (!confident)
    {
        estimate.voiced = false;
        estimate.note = -1;
        pendingNote = -1;
        pendingCount = 0;
        return;
    }

    const auto nearest = static_cast<int> (std::lround (estimate.noteValue));

    // Still the held note (within the hysteresis band): nothing to change.
    if (estimate.note >= 0 &&
        std::abs (estimate.noteValue - static_cast<float> (estimate.note)) <= 0.5f + kNoteHysteresisSemitones)
    {
        pendingNote = -1;
        pendingCount = 0;
        return;
    }

    // A new note, after silence or from the held one: it must be confirmed, and
    // only analyses as confident as a note start count towards that. (Frames
    // straddling a legato change are just confident enough to keep the old note
    // but can point anywhere.)
    if (confidence < kVoicedOnConfidence)
    {
        pendingNote = -1;
        pendingCount = 0;
        estimate.voiced = estimate.note >= 0;
        return;
    }

    if (nearest == pendingNote)
        ++pendingCount;
    else
    {
        pendingNote = nearest;
        pendingCount = 1;
    }

    if (pendingCount >= kConfirmAnalyses)
    {
        estimate.note = nearest;
        pendingNote = -1;
        pendingCount = 0;
    }

    estimate.voiced = estimate.note >= 0;
}

} // namespace thunder::music
