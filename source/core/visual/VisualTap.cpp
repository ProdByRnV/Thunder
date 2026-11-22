#include "VisualTap.h"

#include "core/dsp/grains/GrainModule.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace thunder::visual
{

static_assert (std::atomic<double>::is_always_lock_free);
static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
static_assert (GrainSnapshot::kMaxSparks >= dsp::GrainModule::kMaxGrains);

void VisualTap::prepare (double newSampleRate, int maxChunkSize, int newLatencySamples)
{
    jassert (newSampleRate > 0.0 && maxChunkSize > 0);
    sampleRate = newSampleRate;
    latencySamples = std::max (0, newLatencySamples);
    frameSamples = std::max (16, static_cast<int> (std::lround (sampleRate / kFramesPerSecond)));
    grainInterval = std::max (1, static_cast<int> (std::lround (sampleRate / kGrainsPerSecond)));
    sampleRateForReaders.store (sampleRate, std::memory_order_relaxed);
    frameSecondsForReaders.store (frameSamples / sampleRate, std::memory_order_relaxed);

    for (auto* v : {&layerLow, &layerHigh, &dryLow, &dryHigh})
        v->assign (static_cast<std::size_t> (maxChunkSize), 0.0f);
    spectrumScratch.assign (static_cast<std::size_t> (maxChunkSize), SpectrumSample{});

    kStages = dsp::LoudnessMeter::kWeighting (sampleRate);
    freeRunningSamples = 0;
    resetFrame();
    samplesUntilGrains = 0;
}

void VisualTap::setWanted (std::uint32_t parts)
{
    if (parts != 0 && !allocated)
    {
        // Before the audio thread can see any part wanted (the store below
        // publishes the queues), and never again.
        frames.setCapacity (kFrameCapacity);
        spectrum.setCapacity (kSpectrumCapacity);
        grainSnapshots.setCapacity (kGrainCapacity);
        allocated = true;
    }

    // A part turned on starts fresh: drop whatever is left from before (we are
    // the consumer, so this is safe while the audio thread pushes).
    const auto before = wanted.load (std::memory_order_relaxed);
    const auto turnedOn = parts & ~before;
    if (allocated)
    {
        if ((turnedOn & kFrames) != 0)
            while (frames.getNumReady() > 0)
            {
                WaveFrame f;
                frames.pop (&f, 1);
            }
        if ((turnedOn & kSpectrum) != 0)
        {
            SpectrumSample s[256];
            while (spectrum.pop (s, 256) > 0)
            {
            }
        }
        if ((turnedOn & kGrains) != 0)
        {
            GrainSnapshot g;
            while (grainSnapshots.pop (&g, 1) > 0)
            {
            }
        }
    }

    wanted.store (parts, std::memory_order_release);
}

void VisualTap::beginBlock() noexcept
{
    const auto now = wanted.load (std::memory_order_acquire);

    // A part that has just been turned on starts from a clean state.
    if ((now & kFrames) != 0 && (active & kFrames) == 0)
    {
        resetFrame();
        kState = {};
    }
    if ((now & kGrains) != 0 && (active & kGrains) == 0)
        samplesUntilGrains = 0;

    active = now;
}

void VisualTap::resetFrame() noexcept
{
    current = WaveFrame{};
    currentCount = 0;
}

void VisualTap::tapLayer (const float* const* layer, const float* const* dry, int numSamples) noexcept
{
    numSamples = std::min (numSamples, static_cast<int> (layerLow.size()));

    if ((active & kFrames) != 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            layerLow[index] = std::min (layer[0][i], layer[1][i]);
            layerHigh[index] = std::max (layer[0][i], layer[1][i]);
            dryLow[index] = std::min (dry[0][i], dry[1][i]);
            dryHigh[index] = std::max (dry[0][i], dry[1][i]);
        }
    }

    if ((active & kSpectrum) != 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto& s = spectrumScratch[static_cast<std::size_t> (i)];
            s.dry = 0.5f * (dry[0][i] + dry[1][i]);
            s.layer = 0.5f * (layer[0][i] + layer[1][i]);
        }
        spectrum.push (spectrumScratch.data(), numSamples);
    }
}

float VisualTap::kWeighted (int channel, float sample) noexcept
{
    // Transposed direct form II, as dsp::LoudnessMeter.
    auto x = static_cast<double> (sample);
    for (std::size_t s = 0; s < kStages.size(); ++s)
    {
        auto& z = kState[static_cast<std::size_t> (channel)][s];
        const auto& c = kStages[s];
        const auto y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1];
        z[1] = c.b2 * x - c.a2 * y;
        x = y;
    }
    return static_cast<float> (x);
}

void VisualTap::startFrame (const TransportState& transport, int offset) noexcept
{
    current = WaveFrame{};
    current.dryMin = current.layerMin = std::numeric_limits<float>::max();
    current.dryMax = current.layerMax = std::numeric_limits<float>::lowest();

    // Without a host transport, Thunder's own clock runs at the fallback tempo
    // so the view still scrolls (UI.md §16.4: bar lines at 120 BPM).
    const auto hostClock = transport.hasHostInfo;
    const auto samplesPerQuarter = transport.samplesPerQuarterNote (sampleRate);
    const auto barLength = transport.quarterNotesPerBar();

    double ppq = 0.0, barStart = 0.0;
    if (hostClock)
    {
        ppq = transport.ppqPosition + (transport.isPlaying ? offset / samplesPerQuarter : 0.0);
        barStart = transport.ppqLastBarStart;
    }
    else
    {
        ppq = static_cast<double> (freeRunningSamples + offset) / samplesPerQuarter;
        barStart = std::floor (ppq / barLength) * barLength;
    }

    // What is heard now entered the chain `latencySamples` earlier.
    ppq -= latencySamples / samplesPerQuarter;

    if (barLength > 0.0)
    {
        while (ppq >= barStart + barLength)
            barStart += barLength;
        while (ppq < barStart)
            barStart -= barLength;
    }

    current.ppq = ppq;
    current.barStartPpq = barStart;
    current.bpm = static_cast<float> (transport.bpm);
    current.timeSigNumerator = static_cast<std::uint8_t> (transport.timeSigNumerator);
    current.timeSigDenominator = static_cast<std::uint8_t> (transport.timeSigDenominator);
    current.flags = static_cast<std::uint8_t> (((transport.isPlaying || !hostClock) ? WaveFrame::kMoving : 0) |
                                               (hostClock ? WaveFrame::kHostTempo : 0));
}

void VisualTap::finishFrame() noexcept
{
    current.numSamples = static_cast<std::uint16_t> (currentCount);
    frames.push (&current, 1);
    currentCount = 0;
}

void VisualTap::tapOutput (const float* const* output, int numSamples, const TransportState& transport,
                           float limiterGainDb) noexcept
{
    if ((active & kFrames) == 0)
        return;

    numSamples = std::min (numSamples, static_cast<int> (layerLow.size()));
    const auto reduction = std::max (0.0f, -limiterGainDb);

    for (int i = 0; i < numSamples; ++i)
    {
        if (currentCount == 0)
            startFrame (transport, i);

        const auto index = static_cast<std::size_t> (i);
        current.dryMin = std::min (current.dryMin, dryLow[index]);
        current.dryMax = std::max (current.dryMax, dryHigh[index]);
        current.layerMin = std::min (current.layerMin, layerLow[index]);
        current.layerMax = std::max (current.layerMax, layerHigh[index]);

        const auto left = output[0][i];
        const auto right = output[1][i];
        current.peakLeft = std::max (current.peakLeft, std::abs (left));
        current.peakRight = std::max (current.peakRight, std::abs (right));

        const auto kl = kWeighted (0, left);
        const auto kr = kWeighted (1, right);
        current.loudnessPower += kl * kl + kr * kr;
        current.sumLL += left * left;
        current.sumRR += right * right;
        current.sumLR += left * right;
        current.limiterReductionDb = std::max (current.limiterReductionDb, reduction);

        if (++currentCount >= frameSamples)
            finishFrame();
    }

    if (!transport.hasHostInfo)
        freeRunningSamples += numSamples;
}

void VisualTap::tapGrains (const dsp::GrainModule& grains, bool sounding, int numSamples) noexcept
{
    if ((active & kGrains) == 0)
        return;

    samplesUntilGrains -= numSamples;
    if (samplesUntilGrains > 0)
        return;
    samplesUntilGrains += grainInterval;
    samplesUntilGrains = std::max (samplesUntilGrains, 1);

    auto& snapshot = grainScratch;
    snapshot.count = 0;

    if (sounding)
    {
        const auto now = static_cast<double> (grains.getBuffer().getWriteCount());
        for (const auto& grain : grains.getGrains())
        {
            if (!grain.active || grain.length <= 0 || snapshot.count >= GrainSnapshot::kMaxSparks)
                continue;

            // The read positions as GrainModule::render computes them.
            const auto start = static_cast<double> (grain.base) + grain.firstPosition;
            const auto position = start + static_cast<double> (grain.elapsed) * grain.step;
            const auto phase = static_cast<double> (grain.elapsed) / grain.length;
            const auto hann = std::sin (juce::MathConstants<double>::pi * phase);

            auto& spark = snapshot.sparks[static_cast<std::size_t> (snapshot.count++)];
            spark.ageSeconds = static_cast<float> ((now - position) / sampleRate);
            spark.startAgeSeconds = static_cast<float> ((now - start) / sampleRate);
            spark.semitones = static_cast<float> (12.0 * std::log2 (std::max (1.0e-6, std::abs (grain.step))));
            spark.pan = grain.pan;
            spark.window = static_cast<float> (hann * hann);
        }
    }

    grainSnapshots.push (&snapshot, 1);
}

int VisualTap::readFrames (WaveFrame* destination, int maxFrames) noexcept
{
    return allocated ? frames.pop (destination, maxFrames) : 0;
}

int VisualTap::readSpectrum (SpectrumSample* destination, int maxSamples) noexcept
{
    return allocated ? spectrum.pop (destination, maxSamples) : 0;
}

bool VisualTap::readGrains (GrainSnapshot& latest) noexcept
{
    if (!allocated)
        return false;

    auto any = false;
    while (grainSnapshots.pop (&latest, 1) > 0)
        any = true;
    return any;
}

} // namespace thunder::visual
