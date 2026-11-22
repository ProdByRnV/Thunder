#pragma once

#include "core/visual/VisualFrames.h"

#include <array>
#include <optional>
#include <vector>

namespace thunder::ui
{

// What the output meters show (PRD UI-VIS-007, FR-WIDTH-003; UI.md §8.5, §9.9),
// worked out from the frames' exact peaks and sums:
// - Level per channel: rises at once, falls at 20 dB/s.
// - Peak hold: the highest level, held 1.5 s, then falling like the level.
// - Clip light: lit for 2 s after a sample reaches 0 dBFS (a click clears it).
// - Short-term loudness: ITU-R BS.1770 over the last 3 s (a rectangular
//   window over the frames' K-weighted sums), in LUFS.
// - Correlation of the two channels over the last 0.3 s: +1 mono, 0 unrelated,
//   −1 out of phase (cancels in mono).
// - The limiter light: on while the safety limiter takes more than 1 dB, and
//   0.5 s after.
// Time only moves through advance(), so tests control it. Message thread only.
class MeterModel
{
public:
    static constexpr float kSilenceDb = -100.0f;

    // The frames' length; a different one (new sample rate) clears the windows.
    void prepare (double frameSeconds);

    // [MSG] A frame from the audio thread (also while the transport is stopped).
    void add (const visual::WaveFrame& frame);

    // [MSG] `seconds` have passed: the ballistics move on. Returns true if
    // anything shown changed.
    bool advance (double seconds);

    float getLevelDb (int channel) const noexcept { return channels[index (channel)].level; }
    float getPeakHoldDb (int channel) const noexcept { return channels[index (channel)].hold; }
    bool isClipped (int channel) const noexcept;
    void resetClip() noexcept;

    // Below the loudness floor (−70 LUFS) or with no audio yet: nothing.
    std::optional<float> getShortTermLufs() const noexcept;
    // With no signal on either channel: nothing.
    std::optional<float> getCorrelation() const noexcept;
    bool isLimiting() const noexcept;

private:
    struct Channel
    {
        float level = kSilenceDb;
        float hold = kSilenceDb;
        float pending = 0.0f; // the highest |sample| since the last advance
        double heldAt = -1.0e9;
        double clippedAt = -1.0e9;
    };

    // A running sum over the last `capacity` frames.
    template<std::size_t N> struct Window
    {
        std::vector<std::array<double, N>> ring;
        std::array<double, N> sum{};
        int next = 0, count = 0;

        void prepare (int capacity);
        void add (const std::array<double, N>& values);
    };

    static std::size_t index (int channel) noexcept { return channel <= 0 ? 0u : 1u; }

    std::array<Channel, 2> channels;
    Window<2> loudness;    // K-weighted power, samples
    Window<4> correlation; // LL, RR, LR, samples
    double frameSeconds = 0.0;
    double now = 0.0;
    double limitedAt = -1.0e9;
    std::optional<float> shownLufs, shownCorrelation;
    bool shownLimiting = false;
    std::array<bool, 2> shownClip{};
};

} // namespace thunder::ui
