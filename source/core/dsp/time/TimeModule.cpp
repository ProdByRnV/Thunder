#include "TimeModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

namespace
{
constexpr double kGrainSeconds = 0.040;
constexpr double kSearchSeconds = 0.010;
constexpr double kMatchSeconds = 0.008;
constexpr double kMatchRate = 16000.0;    // the similarity is measured at about this rate
constexpr double kMinFadeSeconds = 0.002; // the shortest segment crossfade
constexpr double kMaxFadeShare = 0.125;   // Smooth 100%: an eighth of the bar

double speedFor (params::HalftimeSpeed speed) noexcept
{
    return speed == params::HalftimeSpeed::Quarter ? 0.25 : 0.5;
}
} // namespace

void TimeModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = std::clamp (spec.numChannels, 1, GrainBuffer::kMaxChannels);

    hop = std::max (1, static_cast<int> (std::lround (kGrainSeconds * 0.5 * sampleRate)));
    grainLength = 2 * hop;
    searchRadius = std::max (1, static_cast<int> (std::lround (kSearchSeconds * sampleRate)));
    matchLength = std::max (1, static_cast<int> (std::lround (kMatchSeconds * sampleRate)));
    matchStep = std::max (1, static_cast<int> (std::lround (sampleRate / kMatchRate)));
    maxSegmentSamples = static_cast<std::int64_t> (kMaxSegmentSeconds * sampleRate);

    // A periodic Hann window: two of them, half a window apart, sum to exactly 1.
    window.resize (static_cast<std::size_t> (grainLength));
    for (int i = 0; i < grainLength; ++i)
        window[static_cast<std::size_t> (i)] = static_cast<float> (
            0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / static_cast<double> (grainLength)));

    // The oldest audio a voice reads: a whole segment behind at the slowest
    // speed, plus the similarity search and its lookback, plus a chunk.
    const auto longest = static_cast<int> (maxSegmentSamples) + grainLength + searchRadius + matchLength +
                         spec.maxBlockSize + static_cast<int> (sampleRate * 0.5);
    history.prepare (numChannels, longest);
    clock.prepare (sampleRate);

    reset();
}

void TimeModule::reset() noexcept
{
    history.reset();
    clock.reset();
    for (auto& v : voices)
        v = {};
    live = 0;
    fadePosition = 1.0;
    lastSegmentStart = -1;
    segmentsStarted = 0;
}

void TimeModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples <= 0)
        return;

    const auto channels = std::min (numChannels, static_cast<int> (block.getNumChannels()));
    float* data[GrainBuffer::kMaxChannels] = {};
    for (int ch = 0; ch < channels; ++ch)
        data[ch] = block.getChannelPointer (static_cast<std::size_t> (ch));
    if (channels == 1)
        data[1] = data[0];

    // The whole chunk goes in first; no voice ever reads past "now".
    const auto chunkStart = history.getWriteCount();
    history.write (data, channels, numSamples);

    const auto& p = context.params;
    const auto speed = speedFor (p.getChoice<params::HalftimeSpeed> (ParamId::halftimeSpeed));
    const auto smooth = std::clamp (p.get (ParamId::halftimeSmooth) / 100.0, 0.0, 1.0);

    // Where the bar lines fall in this chunk (at most one: a chunk is far
    // shorter than any bar).
    const auto position = clock.next (context.transport, numSamples);
    const auto barSamples = position.quarterNotesPerBar * position.samplesPerQuarter;
    const auto intoBar = (position.ppq - position.barStart) * position.samplesPerQuarter;

    // The first sample at or after a bar line starts the segment. When the line
    // falls after the previous chunk's last sample, that is this chunk's first.
    int barLineAt = -1;
    if (position.jumped || intoBar < 1.0 - 1.0e-9)
        barLineAt = 0;
    else if (const auto toNext = std::ceil (barSamples - intoBar - 1.0e-6); toNext < numSamples)
        barLineAt = std::max (0, static_cast<int> (toNext));

    const auto fadeSamples =
        std::min (kMinFadeSeconds * sampleRate + smooth * kMaxFadeShare * barSamples, 0.5 * barSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto now = chunkStart + i;
        const auto& current = voices[static_cast<std::size_t> (live)];

        const auto newSegment = !current.active || i == barLineAt || now - current.segmentStart >= maxSegmentSamples;
        if (newSegment && now != lastSegmentStart)
            startSegment (now, speed, fadeSamples);

        float left = 0.0f, right = 0.0f;
        renderSample (now, left, right);
        data[0][i] = left;
        if (channels > 1)
            data[1][i] = right;
    }
}

void TimeModule::startSegment (std::int64_t now, double speed, double crossfadeSamples) noexcept
{
    auto& previous = voices[static_cast<std::size_t> (live)];
    if (previous.active)
    {
        // The old voice fades out while the new one comes in.
        live ^= 1;
        fadePosition = 0.0;
        fadeStep = 1.0 / std::max (1.0, crossfadeSamples);
    }

    auto& voice = voices[static_cast<std::size_t> (live)];
    voice.active = true;
    voice.segmentStart = now;
    voice.speed = speed;

    // Start mid-way through a grain pair that both read the input from "now"
    // on: until the next grain, the voice plays the input exactly.
    voice.current = {now, 0};
    voice.previous = {now - hop, hop};

    lastSegmentStart = now;
    ++segmentsStarted;
}

void TimeModule::renderSample (std::int64_t now, float& left, float& right) noexcept
{
    auto& voice = voices[static_cast<std::size_t> (live)];
    renderVoice (voice, now, left, right);

    if (fadePosition >= 1.0)
        return;

    auto& old = voices[static_cast<std::size_t> (live ^ 1)];
    float oldLeft = 0.0f, oldRight = 0.0f;
    renderVoice (old, now, oldLeft, oldRight);

    // Equal power: the two segments hold unrelated audio.
    const auto angle = static_cast<float> (fadePosition * juce::MathConstants<double>::halfPi);
    const auto gainIn = std::sin (angle);
    const auto gainOut = std::cos (angle);
    left = left * gainIn + oldLeft * gainOut;
    right = right * gainIn + oldRight * gainOut;

    fadePosition += fadeStep;
    if (fadePosition >= 1.0)
        old.active = false;
}

void TimeModule::renderVoice (Voice& voice, std::int64_t now, float& left, float& right) noexcept
{
    if (voice.current.age >= hop)
    {
        voice.previous = voice.current;
        voice.current = {chooseSource (voice, now), 0};
    }

    const auto last = numChannels - 1;
    for (const auto* grain : {&voice.previous, &voice.current})
    {
        const auto w = window[static_cast<std::size_t> (grain->age)];
        const auto index = grain->source + grain->age;
        left += w * history.read (0, index);
        right += w * history.read (last, index);
    }

    ++voice.previous.age;
    ++voice.current.age;
}

std::int64_t TimeModule::chooseSource (const Voice& voice, std::int64_t now) const noexcept
{
    // Where the slow playhead is now.
    const auto target =
        voice.segmentStart + static_cast<std::int64_t> (voice.speed * static_cast<double> (now - voice.segmentStart));

    // How the previous grain would have continued: the new grain should start
    // where the waveform leading up to it looks like the waveform leading up to
    // that point.
    const auto natural = voice.current.source + hop;

    // Never ahead of the input; never before audio we still hold (with room
    // for the lookback).
    const auto oldest = history.getOldestIndex() + matchLength + 1;
    const auto lo = std::max (target - searchRadius, oldest);
    const auto hi = std::min (target + searchRadius, now);
    if (hi < lo || natural - matchLength < oldest)
        return std::clamp (target, std::min (oldest, now), now);

    // Coarse steps first, then every sample around the best one.
    const auto coarse = static_cast<std::int64_t> (4 * matchStep);
    auto best = std::clamp (target, lo, hi);
    auto bestScore = similarity (best, natural);

    for (auto c = lo; c <= hi; c += coarse)
        if (const auto s = similarity (c, natural); s > bestScore)
        {
            bestScore = s;
            best = c;
        }

    const auto from = std::max (lo, best - coarse + 1);
    const auto to = std::min (hi, best + coarse - 1);
    for (auto c = from; c <= to; ++c)
        if (const auto s = similarity (c, natural); s > bestScore)
        {
            bestScore = s;
            best = c;
        }

    return best;
}

float TimeModule::mid (std::int64_t index) const noexcept
{
    return 0.5f * (history.read (0, index) + history.read (numChannels - 1, index));
}

// Normalised cross-correlation of the matchLength samples before `candidate`
// with those before `reference` (sampled every matchStep samples).
float TimeModule::similarity (std::int64_t candidate, std::int64_t reference) const noexcept
{
    float cross = 0.0f, energy = 1.0e-9f;
    for (int j = matchLength; j > 0; j -= matchStep)
    {
        const auto c = mid (candidate - j);
        cross += c * mid (reference - j);
        energy += c * c;
    }
    return cross / std::sqrt (energy);
}

} // namespace thunder::dsp
