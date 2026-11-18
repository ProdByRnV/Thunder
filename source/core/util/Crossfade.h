#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <utility>

// Click-free switching between two signals (PRD FR-CORE-009, ARCHITECTURE §13.2).
//
// Used for module enable/bypass (A = the signal without the module, B = with
// it), mode changes (A = old mode, B = new mode) and halftime segment edges.
//
// Equal-power (the default) keeps the perceived level steady when A and B are
// unrelated, e.g. dry vs. reverb. Linear keeps the level steady when A and B are
// nearly the same signal, where equal-power would bump it by up to 3 dB.

namespace thunder::util
{

enum class FadeLaw
{
    EqualPower,
    Linear
};

inline constexpr double kBypassFadeSeconds = 0.020;
inline constexpr double kModeFadeSeconds = 0.030;

class Crossfade
{
public:
    // [NRT] Sets the fade time. Keeps the current position.
    void prepare (double sampleRate, double fadeSeconds, FadeLaw fadeLaw = FadeLaw::EqualPower) noexcept
    {
        law = fadeLaw;
        const auto fadeSamples = std::max (1.0, sampleRate * fadeSeconds);
        step = static_cast<float> (1.0 / fadeSamples);
    }

    // [RT] Starts fading towards B (true) or back towards A (false). Reversing
    // mid-fade continues smoothly from the current position.
    void setTarget (bool towardsB) noexcept { targetPosition = towardsB ? 1.0f : 0.0f; }

    // [RT] Jumps to A or B with no fade.
    void snapTo (bool atB) noexcept
    {
        targetPosition = atB ? 1.0f : 0.0f;
        position = targetPosition;
    }

    bool isFading() const noexcept { return !juce::exactlyEqual (position, targetPosition); }
    bool isFullyA() const noexcept { return !isFading() && !targetIsB(); }
    bool isFullyB() const noexcept { return !isFading() && targetIsB(); }
    bool targetIsB() const noexcept { return targetPosition > 0.5f; }

    // 0 = all A, 1 = all B.
    float getPosition() const noexcept { return position; }

    // [RT] a[ch][i] = a[ch][i]·gainA + b[ch][i]·gainB, advancing the fade one step
    // per sample. When fully at A nothing changes; fully at B, B is copied.
    void process (float* const* a, const float* const* b, int numChannels, int numSamples) noexcept
    {
        if (!isFading())
        {
            if (targetIsB())
                for (int ch = 0; ch < numChannels; ++ch)
                    juce::FloatVectorOperations::copy (a[ch], b[ch], numSamples);
            return;
        }

        const auto direction = targetIsB() ? step : -step;

        for (int i = 0; i < numSamples; ++i)
        {
            position = std::clamp (position + direction, 0.0f, 1.0f);
            const auto [gainA, gainB] = gainsAt (position);

            for (int ch = 0; ch < numChannels; ++ch)
                a[ch][i] = a[ch][i] * gainA + b[ch][i] * gainB;
        }
    }

    // The A and B gains at fade position `p` (0..1).
    std::pair<float, float> gainsAt (float p) const noexcept
    {
        switch (law)
        {
            case FadeLaw::EqualPower:
            {
                const auto angle = p * juce::MathConstants<float>::halfPi;
                return {std::cos (angle), std::sin (angle)};
            }

            case FadeLaw::Linear:
                break;
        }

        return {1.0f - p, p};
    }

private:
    FadeLaw law = FadeLaw::EqualPower;
    float step = 1.0f / 960.0f; // replaced in prepare()
    float position = 0.0f;
    float targetPosition = 0.0f;
};

} // namespace thunder::util
