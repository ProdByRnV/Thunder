#include "WaveformHistory.h"

#include "plugin/ui/theme/ThunderTheme.h"

#include <algorithm>
#include <cmath>

namespace thunder::ui
{

void WaveformHistory::prepare (double newFrameSeconds)
{
    jassert (newFrameSeconds > 0.0);
    if (juce::exactlyEqual (newFrameSeconds, frameSeconds) && !ring.empty())
        return;

    frameSeconds = newFrameSeconds;
    const auto capacity = static_cast<int> (std::ceil (theme::visualiser::maxSeconds / frameSeconds)) + 1;
    ring.assign (static_cast<std::size_t> (capacity), Entry{});
    clear();
}

void WaveformHistory::clear()
{
    next = 0;
    count = 0;
    total = 0;
    ++generation;
    hasPrevious = false;
}

const WaveformHistory::Entry& WaveformHistory::fromNewest (int index) const noexcept
{
    jassert (index >= 0 && index < count);
    const auto size = static_cast<int> (ring.size());
    const auto at = ((next - 1 - index) % size + size) % size;
    return ring[static_cast<std::size_t> (at)];
}

const WaveformHistory::Entry* WaveformHistory::atAbsolute (std::int64_t k) const noexcept
{
    if (k < total - count || k >= total)
        return nullptr;
    return &fromNewest (static_cast<int> (total - 1 - k));
}

double WaveformHistory::windowSeconds (int bars) const noexcept
{
    return std::min (theme::visualiser::maxSeconds, std::max (1, bars) * secondsPerBar());
}

void WaveformHistory::add (const visual::WaveFrame& frame)
{
    if (ring.empty())
        return;

    Entry entry;
    entry.dryMin = frame.dryMin;
    entry.dryMax = frame.dryMax;
    entry.layerMin = frame.layerMin;
    entry.layerMax = frame.layerMax;

    bpm = std::max (1.0, static_cast<double> (frame.bpm));
    numerator = std::max (1, static_cast<int> (frame.timeSigNumerator));
    denominator = std::max (1, static_cast<int> (frame.timeSigDenominator));

    // A line belongs to the frame its bar or beat starts in. A frame lasts this
    // many quarter notes; a little slack covers rounding.
    const auto frameQuarters = frameSeconds * bpm / 60.0;
    const auto slack = 1.5 * frameQuarters;
    const auto barLength = frame.quarterNotesPerBar();
    const auto beatLength = 4.0 / denominator;
    const auto intoBar = frame.ppq - frame.barStartPpq;
    const auto beatIndex = std::floor (intoBar / beatLength + 1.0e-9);
    // (A position a hair before the next bar can round to its downbeat; that
    // line is drawn by the next frame, which reports the new bar.)

    const auto newBar = !hasPrevious || std::abs (previous.barStartPpq - frame.barStartPpq) > 1.0e-6;
    if (newBar && intoBar < slack)
    {
        entry.line = kBarLine;
        entry.bar = static_cast<std::int32_t> (std::lround (frame.barStartPpq / barLength)) + 1;
    }
    else if (beatIndex >= 1.0 && beatIndex < numerator && intoBar - beatIndex * beatLength < slack)
    {
        const auto previousBeat =
            hasPrevious && !newBar ? std::floor ((previous.ppq - previous.barStartPpq) / beatLength + 1.0e-9) : -1.0;
        if (!juce::exactlyEqual (beatIndex, previousBeat))
            entry.line = kBeatLine;
    }

    ring[static_cast<std::size_t> (next)] = entry;
    next = (next + 1) % static_cast<int> (ring.size());
    count = std::min (count + 1, static_cast<int> (ring.size()));
    ++total;
    previous = frame;
    hasPrevious = true;
}

} // namespace thunder::ui
