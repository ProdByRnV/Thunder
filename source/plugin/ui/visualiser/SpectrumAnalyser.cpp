#include "SpectrumAnalyser.h"

#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>
#include <cmath>

namespace thunder::ui
{

using namespace theme;

namespace
{
// A Hann window's equivalent noise bandwidth, in bins: a sine's power spreads
// over the bins around it and sums to this many times its own.
constexpr double kHannBandwidth = 1.5;

// Analyse again once this fraction of the FFT has been replaced.
constexpr int kHopDivisor = 8;

// Below this change (dB) nothing visible moves.
constexpr float kVisibleChange = 0.05f;
} // namespace

float SpectrumAnalyser::frequencyOf (int point) noexcept
{
    const auto t = static_cast<float> (point) / static_cast<float> (kPoints - 1);
    return visualiser::minHz * std::pow (visualiser::maxHz / visualiser::minHz, t);
}

void SpectrumAnalyser::prepare (double newSampleRate, int newDetail)
{
    if (newSampleRate <= 0.0 || (juce::exactlyEqual (newSampleRate, sampleRate) && newDetail == detail))
        return;

    sampleRate = newSampleRate;
    detail = newDetail;

    // About 85 ms whatever the rate, so the low end resolves the same (half or
    // twice that with `detail`).
    const auto order = (sampleRate <= 50000.0 ? 12 : (sampleRate <= 100000.0 ? 13 : 14)) + std::clamp (detail, -1, 1);
    fftSize = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);

    window.resize (static_cast<std::size_t> (fftSize));
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), window.size(),
                                                              juce::dsp::WindowingFunction<float>::hann, false);
    work.assign (2 * static_cast<std::size_t> (fftSize), 0.0f);
    prefix.assign (static_cast<std::size_t> (fftSize / 2 + 2), 0.0);

    for (auto* c : {&dry, &layer})
    {
        c->ring.assign (static_cast<std::size_t> (fftSize), 0.0f);
        c->shown.fill (kFloorDb);
    }
    writeIndex = 0;
    fresh = 0;
    sinceAnalysis = 0.0;
}

void SpectrumAnalyser::push (const visual::SpectrumSample* samples, int count)
{
    if (fft == nullptr)
        return;

    for (int i = 0; i < count; ++i)
    {
        const auto at = static_cast<std::size_t> (writeIndex);
        dry.ring[at] = samples[i].dry;
        layer.ring[at] = samples[i].layer;
        writeIndex = (writeIndex + 1) % fftSize;
    }
    fresh = std::min (fresh + count, fftSize);
}

void SpectrumAnalyser::bandPowers (std::array<float, kPoints>& out)
{
    // `work` holds the magnitudes. A full-scale sine at a bin's centre reads
    // |X| = N/4 with a Hann window (its coherent gain is 0.5), so its
    // amplitude is |X| · 4 / N.
    const auto norm = 4.0 / fftSize;
    const auto bins = fftSize / 2;
    const auto binHz = sampleRate / fftSize;

    prefix[0] = 0.0;
    for (int k = 0; k <= bins; ++k)
    {
        const auto a = work[static_cast<std::size_t> (k)] * norm;
        prefix[static_cast<std::size_t> (k) + 1] = prefix[static_cast<std::size_t> (k)] + a * a;
    }
    const auto sum = [this] (int from, int to) // bins from … to, inclusive
    { return prefix[static_cast<std::size_t> (to) + 1] - prefix[static_cast<std::size_t> (from)]; };

    const auto halfBand = std::pow (2.0, visualiser::smoothingOctaves * 0.5);
    for (int p = 0; p < kPoints; ++p)
    {
        const auto f = static_cast<double> (frequencyOf (p));
        if (f >= sampleRate * 0.5)
        {
            out[static_cast<std::size_t> (p)] = kFloorDb;
            continue;
        }

        // The bins inside the band: their total power (a sine counts once).
        const auto first = std::clamp (static_cast<int> (std::ceil (f / halfBand / binHz)), 0, bins);
        const auto last = std::clamp (static_cast<int> (std::floor (f * halfBand / binHz)), 0, bins);
        auto value = last >= first ? sum (first, last) / kHannBandwidth : 0.0;

        // Narrow bands (the low end): the spectrum at the point itself.
        const auto position = f / binHz;
        const auto below = std::clamp (static_cast<int> (std::floor (position)), 0, bins);
        const auto above = std::min (below + 1, bins);
        const auto t = position - below;
        value = std::max (value, sum (below, below) * (1.0 - t) + sum (above, above) * t);

        out[static_cast<std::size_t> (p)] =
            std::max (kFloorDb, static_cast<float> (10.0 * std::log10 (value + 1.0e-20)));
    }
}

void SpectrumAnalyser::analyse (const std::vector<float>& ring, std::array<float, kPoints>& out)
{
    // Oldest sample first, windowed; the second half is the FFT's workspace.
    for (int i = 0; i < fftSize; ++i)
    {
        const auto from = static_cast<std::size_t> ((writeIndex + i) % fftSize);
        work[static_cast<std::size_t> (i)] = ring[from] * window[static_cast<std::size_t> (i)];
    }
    std::fill (work.begin() + fftSize, work.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (work.data(), true);
    bandPowers (out);
}

bool SpectrumAnalyser::update (double seconds)
{
    sinceAnalysis += std::max (0.0, seconds);

    // Without new audio nothing moves: there is nothing to fall towards.
    if (fft == nullptr || fresh < fftSize / kHopDivisor)
        return false;

    const auto release = static_cast<float> (1.0 - std::exp (-sinceAnalysis / visualiser::releaseSeconds));
    const auto visible = [] (float db) { return std::max (db, visualiser::minDb); };
    auto changed = false;

    for (auto* c : {&dry, &layer})
    {
        analyse (c->ring, latest);

        for (std::size_t p = 0; p < static_cast<std::size_t> (kPoints); ++p)
        {
            const auto target = latest[p];
            auto& shown = c->shown[p];
            const auto next = target >= shown ? target : shown + (target - shown) * release;
            changed = changed || std::abs (visible (next) - visible (shown)) > kVisibleChange;
            shown = next;
        }
    }

    fresh = 0;
    sinceAnalysis = 0.0;
    return changed;
}

} // namespace thunder::ui
