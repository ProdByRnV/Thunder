#include "PerformanceStage.h"

#include "PerformControls.h"

#include <algorithm>
#include <cmath>

namespace thunder::perform
{

using params::Kind;
using params::ParamId;

float morphValue (const params::ParamDef& def, float a, float b, float t) noexcept
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;

    switch (def.kind)
    {
        case Kind::Bool:
        case Kind::Choice:
            return t < 0.5f ? a : b;

        case Kind::Int:
            return std::clamp (std::round (a + (b - a) * t), def.min, def.max);

        case Kind::Float:
            break;
    }

    if (def.taper == params::Taper::Log && a > 0.0f && b > 0.0f)
        return std::clamp (a * std::pow (b / a, t), def.min, def.max);

    return std::clamp (a + (b - a) * t, def.min, def.max);
}

void applyMorph (const params::ParamSnapshot& a, const MorphSound& b, float t, params::ParamSnapshot& out) noexcept
{
    out = a;
    if (!b.present)
        return;

    for (const auto& def : params::kParamDefs)
    {
        if (!isMorphed (def.param))
            continue;

        const auto i = params::toIndex (def.param);
        out.values[i] = morphValue (def, a.values[i], b.values[i], t);
    }
}

void PerformanceStage::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    clock.prepare (sampleRate);
    reset();
}

void PerformanceStage::reset() noexcept
{
    clock.reset();
    rise = {};
    riseNow = 0.0f;
    pressPending = false;
    primed = false;
    clapGlide = 0.0f;
    scatterGlide = 0.0f;
    stepKey = -1;
    dropSample = -1;
    anchorSample = -1;
    riserProgress.store (-1.0f, std::memory_order_relaxed);
}

const params::ParamSnapshot& PerformanceStage::apply (const params::ParamSnapshot& host) noexcept
{
    morph.update();
    const auto& b = morph.get();

    const auto t = std::clamp (host.get (ParamId::morphAmount) / 100.0f, 0.0f, 1.0f);
    if (!b.present || t <= 0.0f)
        return host;

    applyMorph (host, b, t, effective);
    return effective;
}

void PerformanceStage::beginBlock (const params::ParamSnapshot& morphed) noexcept
{
    const auto go = morphed.isOn (ParamId::riserGo);
    // A Go left on in a project doesn't start a rise when it opens.
    if (primed && go && !goWasOn)
        pressPending = true;
    if (riserTrigger.exchange (false, std::memory_order_acq_rel))
        pressPending = true;
    goWasOn = go;
    primed = true;
}

bool PerformanceStage::isActive (const params::ParamSnapshot& morphed) const noexcept
{
    return morphed.get (ParamId::stormAmount) > 0.0f || morphed.get (ParamId::variationAmount) > 0.0f ||
           morphed.isOn (ParamId::strikeClap) || morphed.isOn (ParamId::strikeScatter) || clapGlide > 0.0f ||
           scatterGlide > 0.0f || rise.running || pressPending;
}

int PerformanceStage::limitChunk (std::int64_t startSample, int chunk) const noexcept
{
    if (dropSample > startSample && dropSample < startSample + chunk)
        return static_cast<int> (dropSample - startSample);
    return chunk;
}

float PerformanceStage::riseAt (double ppq, double secondsPerQuarter) noexcept
{
    if (!rise.running)
        return 0.0f;

    if (ppq < rise.end)
    {
        const auto length = std::max (rise.end - rise.start, 1.0e-9);
        const auto progress = static_cast<float> (std::clamp ((ppq - rise.start) / length, 0.0, 1.0));
        riserProgress.store (progress, std::memory_order_relaxed);
        return rise.from + (1.0f - rise.from) * std::pow (progress, kRiserShape);
    }

    // The drop: from the peak on the bar line back to the knob.
    const auto since = (ppq - rise.end) * secondsPerQuarter;
    if (since < kRiserDropSeconds)
    {
        riserProgress.store (1.0f, std::memory_order_relaxed);
        return static_cast<float> (1.0 - since / kRiserDropSeconds);
    }

    rise.running = false;
    dropSample = -1;
    riserProgress.store (-1.0f, std::memory_order_relaxed);
    return 0.0f;
}

const params::ParamSnapshot& PerformanceStage::process (const params::ParamSnapshot& morphed,
                                                        const TransportState& transport, std::int64_t startSample,
                                                        int numSamples) noexcept
{
    const auto position = clock.next (transport, numSamples);
    samplesPerQuarter = std::max (position.samplesPerQuarter, 1.0);
    const auto secondsPerQuarter = samplesPerQuarter / sampleRate;

    // A start, stop, loop or jump ends a rise: its bar line is somewhere else now.
    if (position.jumped && rise.running)
    {
        rise.running = false;
        dropSample = -1;
        riserProgress.store (-1.0f, std::memory_order_relaxed);
    }

    // The stage counts its own position from an anchor in whole samples, so every
    // host block size gives exactly the same numbers. It re-anchors on the
    // host's position after a jump or a change of tempo or meter.
    if (anchorSample < 0 || position.jumped || !juce::exactlyEqual (anchorSpq, samplesPerQuarter) ||
        !juce::exactlyEqual (anchorBarLength, position.quarterNotesPerBar))
    {
        anchorSample = startSample;
        anchorPpq = position.ppq;
        anchorBar = position.barStart;
        anchorSpq = samplesPerQuarter;
        anchorBarLength = position.quarterNotesPerBar;
    }
    const auto ppqAt = [this] (std::int64_t sample)
    { return anchorPpq + static_cast<double> (sample - anchorSample) / anchorSpq; };
    const auto barStartAt = [this] (double ppq)
    { return anchorBar + std::floor ((ppq - anchorBar) / anchorBarLength) * anchorBarLength; };

    // The step this chunk belongs to: its grid line, or the drop if that came later.
    auto key = startSample - startSample % kControlInterval;
    if (dropSample > key && dropSample <= startSample)
        key = dropSample;

    if (key != stepKey || pressPending)
    {
        const auto elapsed = stepKey < 0 ? static_cast<double> (kControlInterval)
                                         : static_cast<double> (std::max<std::int64_t> (0, key - stepKey));
        stepKey = key;

        // The song position at the step's start.
        const auto ppq = ppqAt (key);

        if (pressPending)
        {
            pressPending = false;
            rise.from = riseNow;
            rise.start = ppq;
            rise.end = riserEnd (ppq, barStartAt (ppq), anchorBarLength, riserBars (morphed));
            rise.running = true;
        }

        // The strikes glide in and out (~30 ms).
        const auto glideStep = static_cast<float> (elapsed / std::max (1.0, kStrikeGlideSeconds * sampleRate));
        auto glide = [glideStep] (float g, bool on)
        { return on ? std::min (1.0f, g + glideStep) : std::max (0.0f, g - glideStep); };
        clapGlide = glide (clapGlide, morphed.isOn (ParamId::strikeClap));
        scatterGlide = glide (scatterGlide, morphed.isOn (ParamId::strikeScatter));

        riseNow = riseAt (ppq, secondsPerQuarter);

        performed = morphed;
        const auto knob = morphed.get (ParamId::stormAmount);
        const auto storm = knob + (100.0f - knob) * riseNow;
        applyStorm (performed, storm);
        performed.set (ParamId::stormAmount, storm); // Storm as it acts (no module reads it): the knob shows the rise
        applyThunderclap (performed, clapGlide);
        applyScatter (performed, scatterGlide);

        const auto barStart = barStartAt (ppq);
        const auto bar = static_cast<std::int64_t> (std::llround (barStart / anchorBarLength));
        const auto variationGlide =
            static_cast<float> ((ppq - barStart) * secondsPerQuarter / kVariationGlideSeconds);
        applyVariation (performed, morphed.get (ParamId::variationAmount), bar, variationGlide);
    }

    // Where the drop starts, from this chunk's position (the engine ends a chunk there).
    if (rise.running && ppqAt (startSample) < rise.end)
        dropSample = anchorSample + static_cast<std::int64_t> (std::llround ((rise.end - anchorPpq) * anchorSpq));

    return performed;
}

void PerformanceStage::publish (const params::ParamSnapshot& values, bool moved) noexcept
{
    PerformedValues v;
    v.values = values;
    v.moved = moved;
    published.write (v);
}

bool PerformanceStage::readPublished (PerformedValues& out) noexcept
{
    if (!published.update())
        return false;
    out = published.get();
    return true;
}

} // namespace thunder::perform
