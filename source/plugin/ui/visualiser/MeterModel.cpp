#include "MeterModel.h"

#include "plugin/ui/theme/ThunderTheme.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace thunder::ui
{

using namespace theme;

namespace
{
float toDb (float gain) noexcept
{
    return gain > 1.0e-5f ? 20.0f * std::log10 (gain) : MeterModel::kSilenceDb;
}

// Shown values move in these steps, so repainting follows what can be seen.
constexpr float kLufsStep = 0.1f;
constexpr float kCorrelationStep = 0.01f;

bool same (std::optional<float> a, std::optional<float> b) noexcept
{
    return a.has_value() == b.has_value() && (!a || juce::exactlyEqual (*a, *b));
}
} // namespace

template<std::size_t N> void MeterModel::Window<N>::prepare (int capacity)
{
    ring.assign (static_cast<std::size_t> (std::max (1, capacity)), std::array<double, N>{});
    sum = {};
    next = 0;
    count = 0;
}

template<std::size_t N> void MeterModel::Window<N>::add (const std::array<double, N>& values)
{
    auto& slot = ring[static_cast<std::size_t> (next)];
    for (std::size_t i = 0; i < N; ++i)
    {
        sum[i] += values[i] - slot[i];
        slot[i] = values[i];
    }
    next = (next + 1) % static_cast<int> (ring.size());
    count = std::min (count + 1, static_cast<int> (ring.size()));

    // Running sums drift by rounding; rebuild them once per lap.
    if (next == 0)
    {
        sum = {};
        for (const auto& v : ring)
            for (std::size_t i = 0; i < N; ++i)
                sum[i] += v[i];
    }
}

void MeterModel::prepare (double newFrameSeconds)
{
    jassert (newFrameSeconds > 0.0);
    if (juce::exactlyEqual (newFrameSeconds, frameSeconds))
        return;

    frameSeconds = newFrameSeconds;
    loudness.prepare (static_cast<int> (std::lround (visualiser::loudnessSeconds / frameSeconds)));
    correlation.prepare (static_cast<int> (std::lround (visualiser::correlationSeconds / frameSeconds)));
}

void MeterModel::add (const visual::WaveFrame& frame)
{
    if (frameSeconds <= 0.0)
        return;

    channels[0].pending = std::max (channels[0].pending, frame.peakLeft);
    channels[1].pending = std::max (channels[1].pending, frame.peakRight);

    loudness.add ({static_cast<double> (frame.loudnessPower), static_cast<double> (frame.numSamples)});
    correlation.add ({static_cast<double> (frame.sumLL), static_cast<double> (frame.sumRR),
                      static_cast<double> (frame.sumLR), static_cast<double> (frame.numSamples)});

    if (frame.limiterReductionDb > visualiser::limiterWarnDb)
        limitedAt = now;
}

bool MeterModel::advance (double seconds)
{
    now += std::max (0.0, seconds);
    auto changed = false;
    const auto fall = visualiser::meterReleaseDbPerSecond * static_cast<float> (seconds);

    for (auto& c : channels)
    {
        const auto peakDb = toDb (c.pending);
        if (c.pending >= 1.0f)
            c.clippedAt = now;
        c.pending = 0.0f;

        // Instant attack, 20 dB/s release.
        const auto level = std::max (peakDb, std::max (kSilenceDb, c.level - fall));

        // Peak hold: held for 1.5 s, then falls like the level.
        auto hold = c.hold;
        if (peakDb >= hold)
        {
            hold = peakDb;
            c.heldAt = now;
        }
        else if (now - c.heldAt > visualiser::peakHoldSeconds)
        {
            hold = std::max (level, std::max (kSilenceDb, hold - fall));
        }

        // Below the meter's floor nothing shows; don't repaint for it.
        const auto visible = [] (float db) { return std::max (db, visualiser::meterMinDb); };
        changed = changed || !juce::exactlyEqual (visible (level), visible (c.level)) ||
                  !juce::exactlyEqual (visible (hold), visible (c.hold));
        c.level = level;
        c.hold = hold;
    }

    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        const auto clipped = isClipped (static_cast<int> (ch));
        changed = changed || clipped != shownClip[ch];
        shownClip[ch] = clipped;
    }

    const auto quantise = [] (std::optional<float> v, float step) -> std::optional<float>
    {
        if (!v)
            return std::nullopt;
        return std::round (*v / step) * step;
    };

    const auto lufs = quantise (getShortTermLufs(), kLufsStep);
    const auto corr = quantise (getCorrelation(), kCorrelationStep);
    changed = changed || !same (lufs, shownLufs) || !same (corr, shownCorrelation) || isLimiting() != shownLimiting;
    shownLufs = lufs;
    shownCorrelation = corr;
    shownLimiting = isLimiting();

    return changed;
}

bool MeterModel::isClipped (int channel) const noexcept
{
    return now - channels[index (channel)].clippedAt < visualiser::clipHoldSeconds;
}

void MeterModel::resetClip() noexcept
{
    for (auto& c : channels)
        c.clippedAt = -1.0e9;
}

std::optional<float> MeterModel::getShortTermLufs() const noexcept
{
    if (loudness.count == 0 || loudness.sum[1] <= 0.0)
        return std::nullopt;

    // BS.1770: −0.691 + 10·log10 of the K-weighted mean squares summed over the channels.
    const auto meanSquare = std::max (0.0, loudness.sum[0]) / loudness.sum[1];
    if (meanSquare <= 0.0)
        return std::nullopt;

    const auto lufs = static_cast<float> (-0.691 + 10.0 * std::log10 (meanSquare));
    return lufs < visualiser::loudnessFloor ? std::nullopt : std::optional<float> (lufs);
}

std::optional<float> MeterModel::getCorrelation() const noexcept
{
    const auto ll = correlation.sum[0], rr = correlation.sum[1], lr = correlation.sum[2];

    // Below −80 dB on average a channel counts as silent.
    const auto floor = 1.0e-8 * correlation.sum[3];
    if (correlation.count == 0 || ll <= floor || rr <= floor)
        return std::nullopt;

    return static_cast<float> (std::clamp (lr / std::sqrt (ll * rr), -1.0, 1.0));
}

bool MeterModel::isLimiting() const noexcept
{
    return now - limitedAt < visualiser::limiterHoldSeconds;
}

} // namespace thunder::ui
