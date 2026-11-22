#include "GrainModule.h"

#include "core/music/Scale.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

namespace
{
// Cubic reads look one sample before and two after the read position.
constexpr int kCubicBefore = 1;
constexpr int kCubicAfter = 2;

// The level's smoothing (see "Level" in the header): the correlation is
// averaged over 100 ms, and the gain glides over 50 ms.
constexpr double kCorrelationSeconds = 0.100;
constexpr double kGainSeconds = 0.050;

// Below this the cross power is too small to measure (silence); the last
// measured correlation is kept.
constexpr double kMinCrossPower = 1e-12;

double onePoleCoefficient (double seconds, double sampleRate)
{
    return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
}
} // namespace

void GrainModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = std::clamp (spec.numChannels, 1, GrainBuffer::kMaxChannels);
    maxChunk = std::max (1, spec.maxBlockSize);
    maxGrainSamples = std::max (1, static_cast<int> (std::lround (kMaxGrainSeconds * sampleRate)));

    // The history Position Spread reaches into; the longest stretch a grain
    // reads (two grain lengths at the highest rate) plus its own length, during
    // which the writes move on; one chunk; and the interpolation's neighbours.
    const auto needed = static_cast<int> (std::ceil (kBufferSeconds * sampleRate)) + 3 * maxGrainSamples + maxChunk +
                        kCubicBefore + kCubicAfter + 2;
    buffer.prepare (numChannels, needed);

    for (auto& channel : cloud)
        channel.assign (static_cast<std::size_t> (maxChunk), 0.0f);
    grainSquares.assign (static_cast<std::size_t> (maxChunk), 0.0f);
    grainMagnitudes.assign (static_cast<std::size_t> (maxChunk), 0.0f);

    correlationCoefficient = onePoleCoefficient (kCorrelationSeconds, sampleRate);
    gainCoefficient = onePoleCoefficient (kGainSeconds, sampleRate);
    amount.prepare (sampleRate, util::kDefaultRampSeconds);
    clock.prepare (sampleRate);

    const auto eco = spec.quality == params::Quality::Eco;
    grainLimit = eco ? kMaxGrains / 2 : kMaxGrains;
    cubicReads = !eco;

    reset();
}

std::uint32_t GrainModule::scaleLockIntervals (std::uint16_t scaleMask) noexcept
{
    const auto mask = static_cast<unsigned> (scaleMask) & 0xfffu;
    const auto notes = std::popcount (mask);

    std::uint32_t allowed = 0;
    for (int k = -12; k <= 12; ++k)
    {
        // The scale moved up by k semitones, as pitch classes.
        const auto shift = static_cast<unsigned> (((k % 12) + 12) % 12);
        const auto moved = ((mask << shift) | (mask >> (12u - shift))) & 0xfffu;
        const auto kept = std::popcount (moved & mask);
        if (notes == 0 || kept >= notes - 1)
            allowed |= 1u << static_cast<unsigned> (k + 12);
    }
    return allowed;
}

double GrainModule::syncDivisionQuarters (double density, double bpm) noexcept
{
    // Rate of a division d (quarters): bpm / 60 / d grains a second.
    auto best = 4.0;
    auto bestDistance = 1.0e9;
    for (auto d = 4.0; d >= 1.0 / 32.0; d *= 0.5)
    {
        const auto rate = bpm / 60.0 / d;
        const auto distance = std::abs (std::log (rate / std::max (density, 1.0e-6)));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = d;
        }
    }
    return best;
}

void GrainModule::reset() noexcept
{
    buffer.reset();
    for (auto& grain : grains)
        grain = {};
    schedulerPhase = 0.0;

    // Nothing measured yet: assume identical grains, the quietest case.
    crossPower = 0.0;
    maxCrossPower = 0.0;
    correlation = 1.0;
    cloudGain = 1.0;
    snapGain = true;

    // The same random sequence after every reset, so a render is reproducible.
    rng.seed (util::Rng::kDefaultSeed);
    clock.reset();
    syncStarted = false;
    lastSyncStep = 0;
    firstChunk = true;
}

double GrainModule::positionSpreadSeconds (float spread) noexcept
{
    const auto s = static_cast<double> (std::clamp (spread, 0.0f, 1.0f));
    return s * s * kBufferSeconds;
}

float GrainModule::gainForOverlap (double overlap, double correlation) noexcept
{
    // The cloud's power for unit-level grains whose pairs correlate by ρ on
    // average: identical copies add in amplitude, (0.5·N)² (the Hann window
    // averages 0.5); unrelated ones in power, 0.375·N (its mean square). In
    // between, the pairs' share decides:
    //     (1 − ρ)·0.375·N + ρ·0.25·N²
    // Blending in power rather than in gain matters when part of the cloud is
    // coherent: with half the grains reversed, the forward half still adds in
    // amplitude, and a blended gain left the cloud 7 dB too loud.
    constexpr double hannMeanSquare = 0.375;
    const auto n = std::max (0.0, overlap);
    const auto rho = std::clamp (correlation, 0.0, 1.0);
    const auto power = (1.0 - rho) * hannMeanSquare * n + rho * 0.25 * n * n;

    // Sparse clouds aren't boosted: their gaps are part of the sound.
    return power <= 1.0 ? 1.0f : static_cast<float> (1.0 / std::sqrt (power));
}

int GrainModule::getActiveGrains() const noexcept
{
    return static_cast<int> (std::count_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.active; }));
}

void GrainModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto& p = context.params;
    Settings settings{p.get (ParamId::grainSize) / 1000.0,   p.get (ParamId::grainDensity),
                      p.get (ParamId::grainAmount) / 100.0f, p.get (ParamId::grainPosSpread) / 100.0f,
                      p.get (ParamId::grainPitchSpread),     p.get (ParamId::grainPanSpread) / 100.0f,
                      p.get (ParamId::grainReverse) / 100.0f};
    if (p.isOn (ParamId::grainScaleLock))
        settings.scaleLockMask = music::scaleMask (p.getChoice<params::Scale> (ParamId::keyScale));
    settings.sync = p.isOn (ParamId::grainSync);
    settings.freeze = p.isOn (ParamId::grainFreeze);
    process (block, settings, &context.transport);
}

void GrainModule::process (juce::dsp::AudioBlock<float>& block, const Settings& settings,
                           const TransportState* transport) noexcept
{
    const auto total = static_cast<int> (block.getNumSamples());
    if (total == 0)
        return;

    jassert (static_cast<int> (block.getNumChannels()) >= numChannels);

    const auto amountTarget = std::clamp (settings.amount, 0.0f, 1.0f);
    if (firstChunk)
    {
        amount.snapTo (amountTarget);
        firstChunk = false;
    }
    else
    {
        amount.setTarget (amountTarget);
    }

    // The scratch buffers hold one chunk; longer blocks go through in pieces.
    for (int offset = 0; offset < total; offset += maxChunk)
    {
        float* channels[GrainBuffer::kMaxChannels] = {};
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch] = block.getChannelPointer (static_cast<std::size_t> (ch)) + offset;

        const auto n = std::min (maxChunk, total - offset);

        // The song position of this piece, for Tempo Sync (the clock keeps
        // counting whether or not Sync is on, so switching it on lands in phase).
        MusicalClock::Chunk position;
        const MusicalClock::Chunk* where = nullptr;
        if (transport != nullptr)
        {
            position = clock.next (offset == 0 ? *transport : transport->advancedBy (offset, sampleRate), n);
            where = &position;
        }

        processChunk (channels, n, settings, where);
    }
}

void GrainModule::processChunk (float* const* channels, int numSamples, const Settings& settings,
                                const MusicalClock::Chunk* position) noexcept
{
    // The whole chunk goes into the buffer first: a grain starting at sample i
    // reads only indices before (chunk start + i), all of which are now written.
    // Frozen, the buffer keeps what it has and "now" stays where it stopped.
    const auto chunkStart = buffer.getWriteCount();
    if (!settings.freeze)
        buffer.write (channels, numChannels, numSamples);

    const auto length =
        std::clamp (static_cast<int> (std::lround (settings.sizeSeconds * sampleRate)), 1, maxGrainSamples);
    const auto maxOffset = positionSpreadSeconds (settings.positionSpread) * sampleRate;
    const auto allowed = settings.scaleLockMask != 0 ? scaleLockIntervals (settings.scaleLockMask) : 0u;
    const auto nowAt = [&] (int i) { return settings.freeze ? chunkStart : chunkStart + i; };

    auto density = std::max (0.0, settings.density);

    if (settings.sync && position != nullptr)
    {
        // One grain each time the song crosses a line of the note grid.
        const auto bpm = 60.0 * sampleRate / position->samplesPerQuarter;
        const auto division = syncDivisionQuarters (density, bpm);
        density = bpm / 60.0 / division;
        if (position->jumped)
            syncStarted = false;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto ppq = position->ppq + i / position->samplesPerQuarter;
            const auto step = static_cast<long long> (std::floor (ppq / division + 1.0e-9));
            if (syncStarted && step != lastSyncStep)
                startGrain (i, nowAt (i), length, maxOffset, settings, allowed);
            lastSyncStep = step;
            syncStarted = true;
        }
    }
    else
    {
        syncStarted = false;

        // One grain each time the accumulator passes 1.
        const auto increment = density / sampleRate;
        for (int i = 0; i < numSamples; ++i)
        {
            schedulerPhase += increment;
            while (schedulerPhase >= 1.0)
            {
                schedulerPhase -= 1.0;
                startGrain (i, nowAt (i), length, maxOffset, settings, allowed);
            }
        }
    }

    const auto overlap = density * static_cast<double> (length) / sampleRate;
    render (numSamples);
    normalise (numSamples, overlap);
    blend (channels, numSamples);
}

void GrainModule::startGrain (int offset, std::int64_t now, int length, double maxOffsetSamples,
                              const Settings& settings, std::uint32_t allowedIntervals) noexcept
{
    const auto active = std::count_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.active; });
    const auto slot = std::find_if (grains.begin(), grains.end(), [] (const Grain& g) { return !g.active; });
    if (slot == grains.end() || active >= grainLimit)
    {
        ++grainsSkipped;
        return;
    }

    const auto pitchSpread = std::clamp (static_cast<double> (settings.pitchSpread), 0.0, kMaxPitchSpreadSemitones);
    // Drawn only with a spread, so the random sequence (and the sound) without
    // one is what it always was.
    auto semitones = pitchSpread > 0.0 ? pitchSpread * static_cast<double> (rng.nextBipolar()) : 0.0;
    if (allowedIntervals != 0)
    {
        // Scale Lock: the same draw picks one of the safe intervals within the spread.
        int choices[25] = {};
        int count = 0;
        for (int k = -12; k <= 12; ++k)
            if ((allowedIntervals >> static_cast<unsigned> (k + 12)) & 1u)
                if (std::abs (k) <= pitchSpread + 1.0e-6)
                    choices[count++] = k;

        const auto u = pitchSpread > 0.0 ? 0.5 * (semitones / pitchSpread + 1.0) : 0.5;
        const auto index = std::clamp (static_cast<int> (u * count), 0, std::max (count - 1, 0));
        semitones = count > 0 ? static_cast<double> (choices[index]) : 0.0;
    }
    const auto rate = std::exp2 (semitones / 12.0);
    const auto interpolate = !juce::exactlyEqual (rate, 1.0);

    // Where the grain's reads may lie (absolute buffer indices), for its whole life:
    // - the newest: everything it reads must already be written. Forwards it
    //   ends where "now" was when it started after covering `span` samples of
    //   input, and backwards it starts there.
    // - the oldest: nothing it reads may be overwritten before it is played. The
    //   writes move on by its length plus up to one chunk meanwhile. (Only the
    //   history since the last reset counts, so this holds from the start.)
    const auto span = static_cast<std::int64_t> (std::ceil (static_cast<double> (length) * std::max (rate, 1.0)));
    const auto newestBase = now - span - (interpolate ? kCubicAfter : 0);
    const auto oldestBase =
        std::max<std::int64_t> (0, now + length + maxChunk - buffer.getSize()) + (interpolate ? kCubicBefore : 0);

    // Not enough input yet for even one grain.
    if (newestBase < oldestBase)
        return;

    // Position Spread: further back, as far as the buffer allows.
    const auto wanted = static_cast<std::int64_t> (static_cast<double> (rng.nextFloat()) * maxOffsetSamples);
    const auto base = newestBase - std::min (wanted, newestBase - oldestBase);

    const auto pan = std::clamp (settings.panSpread, 0.0f, 1.0f) * rng.nextBipolar();
    const auto reverse = rng.nextFloat() < settings.reverseProbability;

    slot->active = true;
    slot->interpolate = interpolate;
    slot->cubic = cubicReads;
    slot->startOffset = offset;
    slot->length = length;
    slot->elapsed = 0;
    slot->base = base;
    slot->step = reverse ? -rate : rate;
    slot->firstPosition = reverse ? static_cast<double> (length - 1) * rate : 0.0;
    slot->windowStep = static_cast<float> (GrainWindow::kSegments) / static_cast<float> (length);
    slot->pan = pan;

    if (numChannels < 2 || juce::exactlyEqual (pan, 0.0f))
    {
        slot->gains = {1.0f, 1.0f};
    }
    else
    {
        // Equal-power, unity at the centre: √2·cos and √2·sin of (pan + 1)·π/4.
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        slot->gains = {juce::MathConstants<float>::sqrt2 * std::cos (angle),
                       juce::MathConstants<float>::sqrt2 * std::sin (angle)};
    }

    ++grainsStarted;
    if (reverse)
        ++grainsReversed;
}

void GrainModule::render (int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::clear (cloud[static_cast<std::size_t> (ch)].data(), numSamples);
    juce::FloatVectorOperations::clear (grainSquares.data(), numSamples);
    juce::FloatVectorOperations::clear (grainMagnitudes.data(), numSamples);

    float* out[GrainBuffer::kMaxChannels] = {cloud[0].data(), cloud[1].data()};
    auto* squares = grainSquares.data();
    auto* magnitudes = grainMagnitudes.data();

    for (auto& grain : grains)
    {
        if (!grain.active)
            continue;

        const auto first = grain.startOffset;
        const auto count = std::min (numSamples - first, grain.length - grain.elapsed);

        for (int k = 0; k < count; ++k)
        {
            const auto played = grain.elapsed + k;
            const auto w = window.atSegment (static_cast<float> (played) * grain.windowStep);

            // From the start each time rather than accumulated, so a reversed
            // grain ends exactly on its first sample.
            const auto position = std::max (0.0, grain.firstPosition + static_cast<double> (played) * grain.step);
            const auto whole = static_cast<std::int64_t> (position);
            const auto fraction = static_cast<float> (position - static_cast<double> (whole));
            const auto i = first + k;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto sample = 0.0f;
                if (!grain.interpolate)
                    sample = buffer.read (ch, grain.base + whole);
                else if (grain.cubic)
                    sample = buffer.readCubic (ch, grain.base + whole, fraction);
                else
                {
                    const auto a = buffer.read (ch, grain.base + whole);
                    sample = a + fraction * (buffer.read (ch, grain.base + whole + 1) - a);
                }
                const auto contribution = grain.gains[static_cast<std::size_t> (ch)] * w * sample;
                out[ch][i] += contribution;

                if (ch == 0)
                {
                    squares[i] += contribution * contribution;
                    magnitudes[i] += std::abs (contribution);
                }
            }
        }

        grain.elapsed += count;
        grain.startOffset = 0;
        if (grain.elapsed >= grain.length)
            grain.active = false;
    }
}

void GrainModule::normalise (int numSamples, double overlap) noexcept
{
    const auto* sum = cloud[0].data();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto index = static_cast<std::size_t> (i);
        const auto s = static_cast<double> (sum[index]);
        const auto q = static_cast<double> (grainSquares[index]);
        const auto a = static_cast<double> (grainMagnitudes[index]);

        // What the pairs of grains add to the power, against what they would
        // add if they were identical (all in step).
        crossPower += correlationCoefficient * ((s * s - q) - crossPower);
        maxCrossPower += correlationCoefficient * ((a * a - q) - maxCrossPower);
        if (maxCrossPower > kMinCrossPower)
            correlation = std::clamp (crossPower / maxCrossPower, 0.0, 1.0);

        const auto target = static_cast<double> (gainForOverlap (overlap, correlation));
        if (snapGain)
        {
            cloudGain = target;
            snapGain = false;
        }
        else
        {
            cloudGain += gainCoefficient * (target - cloudGain);
        }

        const auto g = static_cast<float> (cloudGain);
        for (int ch = 0; ch < numChannels; ++ch)
            cloud[static_cast<std::size_t> (ch)][index] *= g;
    }
}

void GrainModule::blend (float* const* channels, int numSamples) noexcept
{
    // Equal-power between the input and the cloud: they are unrelated signals
    // (the cloud is delayed and scattered).
    if (!amount.isSmoothing())
    {
        const auto a = amount.current();
        if (a >= 1.0f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (channels[ch], cloud[static_cast<std::size_t> (ch)].data(),
                                                   numSamples);
            return;
        }

        const auto angle = a * juce::MathConstants<float>::halfPi;
        const auto inputGain = std::cos (angle);
        const auto wetGain = std::sin (angle);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            juce::FloatVectorOperations::multiply (channels[ch], inputGain, numSamples);
            juce::FloatVectorOperations::addWithMultiply (channels[ch], cloud[static_cast<std::size_t> (ch)].data(),
                                                          wetGain, numSamples);
        }
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto angle = amount.next() * juce::MathConstants<float>::halfPi;
        const auto inputGain = std::cos (angle);
        const auto wetGain = std::sin (angle);
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = channels[ch][i] * inputGain +
                              cloud[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] * wetGain;
    }
}

} // namespace thunder::dsp
