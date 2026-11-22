#pragma once

#include "core/visual/VisualFrames.h"

#include <cstdint>
#include <vector>

namespace thunder::ui
{

// The waveform view's memory (PRD UI-VIS-001/004/005): the last 32 s of frames
// while the song moves, newest last, each with the bar or beat line that
// starts in it. Lines come from each frame's own song position, so loops,
// jumps and tempo changes draw where they happen. Frames that arrive while the
// transport is stopped are not added: the view freezes (UI.md §8.2).
// Message thread only.
class WaveformHistory
{
public:
    static constexpr std::uint8_t kNoLine = 0;
    static constexpr std::uint8_t kBeatLine = 1;
    static constexpr std::uint8_t kBarLine = 2;

    struct Entry
    {
        float dryMin = 0.0f, dryMax = 0.0f;
        float layerMin = 0.0f, layerMax = 0.0f;
        std::int32_t bar = 0; // the bar number (from 1) where `line` is a bar line
        std::uint8_t line = kNoLine;
    };

    // Room for the longest window at `frameSeconds` per frame. A different
    // frame length (a new sample rate) starts empty.
    void prepare (double frameSeconds);
    void clear();

    // Adds a frame (the caller passes only frames whose position moves).
    void add (const visual::WaveFrame& frame);

    int size() const noexcept { return count; }
    const Entry& fromNewest (int index) const noexcept; // 0 = the newest

    // Frames counted since the history last started (prepare/clear): the k-th
    // frame ever added, or nullptr if it is no longer (or not yet) held.
    std::int64_t getTotalAdded() const noexcept { return total; }
    const Entry* atAbsolute (std::int64_t k) const noexcept;
    // Changes whenever the history starts again.
    int getGeneration() const noexcept { return generation; }
    double getFrameSeconds() const noexcept { return frameSeconds; }

    // The newest frame's tempo and meter (120 BPM in 4/4 before any).
    double getBpm() const noexcept { return bpm; }
    double getQuarterNotesPerBar() const noexcept { return numerator * 4.0 / denominator; }
    int getBeatsPerBar() const noexcept { return numerator; }
    double secondsPerBar() const noexcept { return getQuarterNotesPerBar() * 60.0 / bpm; }

    // How many seconds `bars` bars last at the current tempo (at most the
    // history's length).
    double windowSeconds (int bars) const noexcept;

private:
    std::vector<Entry> ring;
    int next = 0;
    int count = 0;
    std::int64_t total = 0;
    int generation = 0;
    double frameSeconds = 0.0;

    visual::WaveFrame previous;
    bool hasPrevious = false;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
};

} // namespace thunder::ui
