#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{

// The safety limiter at the very end of the output (PRD FR-GAIN-003): nothing
// leaves Thunder above −0.3 dBFS, however modules, gains and stacked layers add
// up. It is a look-ahead peak limiter (PRD DEC-043):
//
// - The audio is delayed by 1.5 ms. For each sample entering the delay, the
//   gain it needs (ceiling / peak, both channels linked so the stereo image
//   doesn't shift) is known 1.5 ms before that sample leaves.
// - A running minimum over the look-ahead window, then a running average over
//   the same window, turns those needs into a gain that starts falling 1.5 ms
//   early and reaches the needed value exactly when the peak leaves: no
//   clipping and no click.
// - The gain recovers over 100 ms.
// - A final clamp to the ceiling guards against rounding.
//
// Off, nothing is clamped and the gain recovers to 1 the same way. The audio stays delayed, so the
// latency never changes (ARCHITECTURE AD-02).
class SafetyLimiter
{
public:
    static constexpr float kCeiling = 0.96605088f; // −0.3 dBFS
    static constexpr double kLookaheadSeconds = 0.0015;
    static constexpr double kReleaseSeconds = 0.100;
    static constexpr int kMaxChannels = 2;

    // [NRT] Allocates the delay and the window.
    void prepare (double sampleRate)
    {
        lookahead = std::max (1, static_cast<int> (std::lround (kLookaheadSeconds * sampleRate)));
        window = lookahead + 1;

        for (auto& line : delay)
            line.assign (static_cast<std::size_t> (lookahead), 0.0f);
        // One more than the window: a new value goes in before the oldest leaves.
        minQueueValue.assign (static_cast<std::size_t> (window + 1), 1.0f);
        minQueueTime.assign (static_cast<std::size_t> (window + 1), 0);
        heldHistory.assign (static_cast<std::size_t> (window), 1.0f);

        release = 1.0f - static_cast<float> (std::exp (-1.0 / (kReleaseSeconds * sampleRate)));
        reset();
    }

    // [RT]
    void reset() noexcept
    {
        for (auto& line : delay)
            std::fill (line.begin(), line.end(), 0.0f);
        std::fill (heldHistory.begin(), heldHistory.end(), 1.0f);
        delayPosition = 0;
        historyPosition = 0;
        heldSum = static_cast<double> (window);
        queueHead = queueSize = 0;
        time = 0;
        gain = 1.0f;
    }

    // The latency it adds, in samples.
    int getLatencySamples() const noexcept { return lookahead; }

    // The current gain reduction in dB (≤ 0), for meters.
    float getGainReductionDb() const noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-6f)); }

    // [RT] Limits `numChannels` channels in place.
    void process (float* const* channels, int numChannels, int numSamples, bool enabled) noexcept
    {
        const auto chs = std::min (numChannels, kMaxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            // What the incoming sample needs.
            float peak = 0.0f;
            for (int ch = 0; ch < chs; ++ch)
                peak = std::max (peak, std::abs (channels[ch][i]));
            const auto need = (enabled && peak > kCeiling) ? kCeiling / peak : 1.0f;

            // Running minimum over the last `window` needs (a monotonic queue).
            pushMinimum (need);
            const auto held = minimum();

            // Running average of the held values over the same window.
            const auto h = static_cast<std::size_t> (historyPosition);
            heldSum += static_cast<double> (held) - static_cast<double> (heldHistory[h]);
            heldHistory[h] = held;
            if (++historyPosition == window)
            {
                historyPosition = 0;
                resum(); // once per window: no rounding drift over a long session
            }
            const auto smooth = std::min (1.0f, static_cast<float> (heldSum / static_cast<double> (window)));

            // Falls at once to the smoothed need; recovers slowly.
            gain = smooth < gain ? smooth : gain + release * (smooth - gain);

            // The sample leaving the delay gets the gain.
            const auto d = static_cast<std::size_t> (delayPosition);
            for (int ch = 0; ch < chs; ++ch)
            {
                auto& line = delay[static_cast<std::size_t> (ch)];
                const auto out = line[d] * gain;
                line[d] = channels[ch][i];
                channels[ch][i] = enabled ? std::clamp (out, -kCeiling, kCeiling) : out;
            }
            if (++delayPosition == lookahead)
                delayPosition = 0;
            ++time;
        }
    }

private:
    void pushMinimum (float value) noexcept
    {
        const auto capacity = static_cast<int> (minQueueValue.size());

        // Drop values that can never be the minimum again.
        while (queueSize > 0 && minQueueValue[static_cast<std::size_t> (back())] >= value)
            --queueSize;

        auto tail = queueHead + queueSize;
        if (tail >= capacity)
            tail -= capacity;
        const auto slot = static_cast<std::size_t> (tail);
        minQueueValue[slot] = value;
        minQueueTime[slot] = time;
        ++queueSize;

        // Drop values older than the window.
        while (minQueueTime[static_cast<std::size_t> (queueHead)] <= time - window)
        {
            if (++queueHead == capacity)
                queueHead = 0;
            --queueSize;
        }
    }

    void resum() noexcept
    {
        heldSum = 0.0;
        for (const auto v : heldHistory)
            heldSum += static_cast<double> (v);
    }

    float minimum() const noexcept { return minQueueValue[static_cast<std::size_t> (queueHead)]; }
    int back() const noexcept
    {
        const auto capacity = static_cast<int> (minQueueValue.size());
        auto last = queueHead + queueSize - 1;
        if (last >= capacity)
            last -= capacity;
        return last;
    }

    int lookahead = 1, window = 2;
    std::array<std::vector<float>, kMaxChannels> delay;
    int delayPosition = 0;

    std::vector<float> minQueueValue;
    std::vector<std::int64_t> minQueueTime;
    int queueHead = 0, queueSize = 0;
    std::int64_t time = 0;

    std::vector<float> heldHistory;
    int historyPosition = 0;
    double heldSum = 0.0;

    float release = 0.0f;
    float gain = 1.0f;
};

} // namespace thunder::dsp
