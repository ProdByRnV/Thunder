#include "FdnReverb.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

namespace
{
struct Shape
{
    std::array<double, FdnReverb::kLines> linesMs;
    std::array<std::array<double, FdnReverb::kDiffusers>, 2> diffusersMs;
    float diffusion;
    double modulationMs;
    float outputGain;
};

// Line lengths are spread unevenly (no common factors at audio rates) so the
// echoes don't line up; the diffusers are shorter and differ left to right.
// clang-format off
constexpr Shape kPlate{
    {13.1, 15.7, 18.9, 22.3, 25.1, 28.7, 31.9, 35.3},
    {{{4.1, 3.3, 2.6, 1.9}, {4.4, 3.1, 2.8, 1.7}}},
    0.72f, 0.15, 0.71f};

constexpr Shape kRoom{
    {7.3, 9.1, 10.9, 12.7, 14.9, 16.6, 18.8, 21.1},
    {{{2.1, 1.7, 1.3, 0.9}, {2.3, 1.6, 1.2, 1.0}}},
    0.55f, 0.08, 0.79f};

constexpr Shape kShimmer{
    {41.3, 47.9, 53.7, 61.1, 67.3, 73.9, 79.1, 86.9},
    {{{6.3, 4.9, 3.7, 2.9}, {6.7, 4.6, 3.9, 2.7}}},
    0.7f, 0.35, 0.84f};
// clang-format on

constexpr double kShifterWindowSeconds = 0.040;
constexpr double kMinModRateHz = 0.23;
constexpr double kModRateSpreadHz = 0.11; // each line a little faster than the last

const Shape& shapeFor (FdnReverb::Character c) noexcept
{
    switch (c)
    {
        case FdnReverb::Character::plate:
            return kPlate;
        case FdnReverb::Character::room:
            return kRoom;
        case FdnReverb::Character::shimmer:
            return kShimmer;
    }
    return kPlate;
}

int powerOfTwoAtLeast (int n) noexcept
{
    auto size = 1;
    while (size < n)
        size <<= 1;
    return size;
}
} // namespace

void FdnReverb::prepare (double newSampleRate, Character newCharacter)
{
    sampleRate = newSampleRate;
    character = newCharacter;
    const auto& shape = shapeFor (character);

    diffusion = shape.diffusion;
    modDepth = shape.modulationMs * 0.001 * sampleRate;
    outputGain = shape.outputGain;

    for (int i = 0; i < kLines; ++i)
    {
        auto& line = lines[static_cast<std::size_t> (i)];
        line.length = shape.linesMs[static_cast<std::size_t> (i)] * 0.001 * sampleRate;
        const auto size = powerOfTwoAtLeast (static_cast<int> (std::ceil (line.length + modDepth)) + 4);
        line.buffer.assign (static_cast<std::size_t> (size), 0.0f);
        line.mask = size - 1;
        const auto step = juce::MathConstants<double>::twoPi * (kMinModRateHz + kModRateSpreadHz * i) / sampleRate;
        line.stepCos = std::cos (step);
        line.stepSin = std::sin (step);
    }

    for (std::size_t ch = 0; ch < 2; ++ch)
        for (std::size_t d = 0; d < static_cast<std::size_t> (kDiffusers); ++d)
        {
            auto& a = diffusers[ch][d];
            a.length = std::max (1, static_cast<int> (std::lround (shape.diffusersMs[ch][d] * 0.001 * sampleRate)));
            a.buffer.assign (static_cast<std::size_t> (a.length), 0.0f);
        }

    shifterWindow = std::max (16, static_cast<int> (std::lround (kShifterWindowSeconds * sampleRate)));
    shifterWeights.resize (static_cast<std::size_t> (shifterWindow));
    for (int i = 0; i < shifterWindow; ++i)
    {
        const auto w = std::sin (juce::MathConstants<double>::pi * i / shifterWindow);
        shifterWeights[static_cast<std::size_t> (i)] = static_cast<float> (w * w);
    }
    for (auto& s : shifters)
    {
        const auto size = powerOfTwoAtLeast (shifterWindow + 2);
        s.buffer.assign (static_cast<std::size_t> (size), 0.0f);
        s.mask = size - 1;
    }

    const auto preDelaySize = powerOfTwoAtLeast (static_cast<int> (std::ceil (kMaxPreDelaySeconds * sampleRate)) + 2);
    for (auto& channel : preDelay)
        channel.assign (static_cast<std::size_t> (preDelaySize), 0.0f);
    preDelayMask = preDelaySize - 1;

    reset();
    updateGains();
}

void FdnReverb::reset() noexcept
{
    for (int i = 0; i < kLines; ++i)
    {
        auto& line = lines[static_cast<std::size_t> (i)];
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.write = 0;
        line.lowpass = 0.0f;
        // Fixed, spread start phases: reproducible.
        line.modCos = std::cos (0.7 * i);
        line.modSin = std::sin (0.7 * i);
    }
    for (auto& channel : diffusers)
        for (auto& a : channel)
        {
            std::fill (a.buffer.begin(), a.buffer.end(), 0.0f);
            a.position = 0;
        }
    for (auto& s : shifters)
    {
        std::fill (s.buffer.begin(), s.buffer.end(), 0.0f);
        s.write = 0;
        s.position = 0;
    }
    for (auto& channel : preDelay)
        std::fill (channel.begin(), channel.end(), 0.0f);
    preDelayWrite = 0;
}

void FdnReverb::setDecaySeconds (double seconds) noexcept
{
    if (!juce::exactlyEqual (seconds, decaySeconds))
    {
        decaySeconds = std::max (seconds, 0.05);
        updateGains();
    }
}

void FdnReverb::setDampingHz (double hertz) noexcept
{
    const auto cutoff = std::clamp (hertz, 20.0, 0.45 * sampleRate);
    dampingCoeff = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / sampleRate));
}

void FdnReverb::setPreDelaySeconds (double seconds) noexcept
{
    preDelaySamples =
        std::clamp (static_cast<int> (std::lround (seconds * sampleRate)), 0, static_cast<int> (preDelayMask) - 1);
}

void FdnReverb::updateGains() noexcept
{
    // Each line loses 60 dB over the decay time, in proportion to its length.
    for (auto& line : lines)
        line.gain = static_cast<float> (std::pow (10.0, -3.0 * line.length / (decaySeconds * sampleRate)));
}

float FdnReverb::readLine (const Line& line, double delay) const noexcept
{
    const auto position = static_cast<double> (line.write) - delay;
    const auto whole = std::floor (position);
    const auto t = static_cast<float> (position - whole);
    const auto i = static_cast<int> (whole);
    const auto a = line.buffer[static_cast<std::size_t> (i & line.mask)];
    const auto b = line.buffer[static_cast<std::size_t> ((i + 1) & line.mask)];
    return a + t * (b - a);
}

// An octave up: two read heads sweep the last `shifterWindow` samples at twice
// the write speed, half a window apart, crossfaded with sin² weights that sum
// to 1 (so the output is never louder than the input).
float FdnReverb::shift (Shifter& s, float input) noexcept
{
    s.buffer[static_cast<std::size_t> (s.write)] = input;

    float out = 0.0f;
    for (const auto offset : {0, shifterWindow / 2})
    {
        auto position = s.position + offset;
        if (position >= shifterWindow)
            position -= shifterWindow;
        const auto delay = shifterWindow - position; // falls by one sample per sample: twice the speed
        out += shifterWeights[static_cast<std::size_t> (position)] *
               s.buffer[static_cast<std::size_t> ((s.write - delay) & s.mask)];
    }

    s.write = (s.write + 1) & s.mask;
    if (++s.position >= shifterWindow)
        s.position = 0;
    return out;
}

void FdnReverb::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                         int numSamples) noexcept
{
    constexpr float householder = 2.0f / static_cast<float> (kLines);
    constexpr float inputScale = 0.5f;
    const auto shimmer = character == Character::shimmer;

    for (int n = 0; n < numSamples; ++n)
    {
        // Pre-delay.
        preDelay[0][static_cast<std::size_t> (preDelayWrite)] = inLeft[n];
        preDelay[1][static_cast<std::size_t> (preDelayWrite)] = inRight[n];
        const auto readAt = static_cast<std::size_t> ((preDelayWrite - preDelaySamples) & preDelayMask);
        std::array<float, 2> in{preDelay[0][readAt], preDelay[1][readAt]};
        preDelayWrite = (preDelayWrite + 1) & preDelayMask;

        // Diffusion: Schroeder allpasses.
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (auto& a : diffusers[ch])
            {
                const auto delayed = a.buffer[static_cast<std::size_t> (a.position)];
                const auto v = in[ch] + diffusion * delayed;
                in[ch] = delayed - diffusion * v;
                a.buffer[static_cast<std::size_t> (a.position)] = v;
                a.position = a.position + 1 < a.length ? a.position + 1 : 0;
            }

        // The lines' outputs, damped and scaled for the decay.
        std::array<float, kLines> y{};
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[static_cast<std::size_t> (i)];
            auto out = readLine (line, line.length + modDepth * (1.0 + line.modSin));
            const auto c = line.modCos * line.stepCos - line.modSin * line.stepSin;
            line.modSin = line.modSin * line.stepCos + line.modCos * line.stepSin;
            line.modCos = c;

            if (shimmer && i < 2)
                out = shift (shifters[static_cast<std::size_t> (i)], out);

            line.lowpass += dampingCoeff * (out - line.lowpass);
            y[static_cast<std::size_t> (i)] = line.lowpass * line.gain;
            sum += y[static_cast<std::size_t> (i)];
        }

        // Householder feedback plus the input, alternate lines per channel.
        const auto reflected = householder * sum;
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[static_cast<std::size_t> (i)];
            const auto yi = y[static_cast<std::size_t> (i)];
            line.buffer[static_cast<std::size_t> (line.write)] =
                yi - reflected + inputScale * in[static_cast<std::size_t> (i & 1)];
            line.write = (line.write + 1) & line.mask;

            const auto sign = (i & 2) != 0 ? -1.0f : 1.0f;
            if ((i & 1) == 0)
                left += sign * yi;
            else
                right += sign * yi;
        }

        outLeft[n] = left * outputGain;
        outRight[n] = right * outputGain;
    }

    // Keep the modulation phasors on the unit circle (rounding would let them drift).
    for (auto& line : lines)
    {
        const auto norm = 1.0 / std::sqrt (line.modCos * line.modCos + line.modSin * line.modSin);
        line.modCos *= norm;
        line.modSin *= norm;
    }
}

} // namespace thunder::dsp
