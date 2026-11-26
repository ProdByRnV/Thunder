#include "RegressionSuite.h"

#include "support/TestSignals.h"

#include "core/music/Scale.h"
#include "core/presets/PresetLibrary.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace thunder::test::regression
{

using params::ParamId;
using params::ParamSnapshot;

namespace
{
// ---- Tolerances ---------------------------------------------------------------------------------------------------
// Each measurement is compared above a floor: below it, differences are
// inaudible and only rounding.
constexpr double kRmsTolerance = 0.5;       // dB
constexpr double kPeakTolerance = 1.0;      // dB
constexpr double kSideTolerance = 1.0;      // dB
constexpr double kSideFloor = -40.0;        // dB under the mid
constexpr double kCentroidTolerance = 0.03; // fraction
constexpr double kPitchTolerance = 5.0;     // cents
constexpr double kEnvelopeTolerance = 1.0;  // dB
constexpr double kEnvelopeFloor = -60.0;    // dBFS
constexpr double kBandTolerance = 1.0;      // dB
constexpr double kBandFloor = -50.0;        // dB under the total

constexpr double kLevelFloor = -120.0; // what silence measures as
constexpr int kFftOrder = 12;          // 4096 points
constexpr int kNumBands = 9;           // 62.5 Hz … 16 kHz

double toDb (double power) noexcept
{
    return power > 0.0 ? std::max (kLevelFloor, 10.0 * std::log10 (power)) : kLevelFloor;
}

// ---- The cases ----------------------------------------------------------------------------------------------------

template<typename Enum> void setChoice (ParamSnapshot& s, ParamId id, Enum value)
{
    s.set (id, static_cast<float> (params::enumIndex (value)));
}

std::int64_t samplesPerBeat (double sampleRate)
{
    return static_cast<std::int64_t> (std::llround (sampleRate * 60.0 / kBpm));
}

// The input: a plucked C minor melody (C4 E♭4 G4 B♭4, one note a beat) for two
// bars, then a bar of silence so the tails are measured too. Each note is a
// band-limited sawtooth (bright enough for the filters to matter) with a short
// noise pick; the noise comes from a hash of the sample index, so it is the
// same everywhere.
float inputAt (std::size_t sample, double sampleRate)
{
    constexpr double notes[] = {60.0, 63.0, 67.0, 70.0};
    const auto ppq = static_cast<double> (sample) * kBpm / (60.0 * sampleRate);
    if (ppq >= 8.0)
        return 0.0f;

    const auto beat = std::floor (ppq);
    const auto f = music::noteToFrequency (notes[static_cast<int> (beat) % 4]);
    const auto t = (ppq - beat) * 60.0 / kBpm; // seconds into the note
    auto envelope = std::exp (-4.0 * t) * std::min (1.0, t * 400.0);
    if (ppq > 7.75)
        envelope *= (8.0 - ppq) / 0.25; // the last note fades out

    auto v = 0.0;
    for (int h = 1; h <= 24 && f * h < 0.45 * sampleRate; ++h)
        v += std::sin (2.0 * juce::MathConstants<double>::pi * f * h * t) / h;

    auto hash = static_cast<std::uint32_t> (sample) * 2654435761u;
    hash ^= hash >> 15;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    const auto noise = static_cast<double> (hash) / 2147483648.0 - 1.0;

    return static_cast<float> (0.25 * envelope * v + 0.2 * noise * std::exp (-t * 300.0));
}

// The snapshot of a factory preset loaded over the base, as the plugin loads
// one: Key, Scale and Output Mode stay.
ParamSnapshot presetSnapshot (const presets::Preset& preset)
{
    auto s = baseSnapshot();
    for (const auto& def : params::kParamDefs)
        if (def.param != ParamId::keyRoot && def.param != ParamId::keyScale && def.param != ParamId::outputMode)
            s.set (def.param, preset.valueOr (def.param));
    return s;
}

std::vector<Case> makeCases()
{
    std::vector<Case> cases;
    auto add = [&] (juce::String name, juce::String group, std::function<void (Case&)> configure)
    {
        Case c;
        c.name = std::move (name);
        c.group = std::move (group);
        c.snapshot = baseSnapshot();
        configure (c);
        cases.push_back (std::move (c));
    };

    // ---- Engine settings
    const juce::String engine = "Engine settings";
    add ("Init", engine, [] (Case&) {});
    add ("Init at 44.1 kHz", engine, [] (Case& c) { c.sampleRate = 44100.0; });
    add ("Init at 96 kHz", engine, [] (Case& c) { c.sampleRate = 96000.0; });
    add ("Quality Eco", engine, [] (Case& c) { c.quality = params::Quality::Eco; });
    add ("Quality High", engine, [] (Case& c) { c.quality = params::Quality::High; });
    add ("Blend", engine, [] (Case& c) { setChoice (c.snapshot, ParamId::outputMode, params::OutputMode::Blend); });
    add ("Blend at Mix 80%", engine,
         [] (Case& c)
         {
             setChoice (c.snapshot, ParamId::outputMode, params::OutputMode::Blend);
             c.snapshot.set (ParamId::mix, 80.0f);
         });
    add ("Input +12 dB into the limiter", engine, [] (Case& c) { c.snapshot.set (ParamId::inputGain, 12.0f); });
    add ("Auto Gain off", engine, [] (Case& c) { c.snapshot.set (ParamId::autoGain, 0.0f); });
    add ("Semitone +7", engine,
         [] (Case& c)
         {
             setChoice (c.snapshot, ParamId::pitchMode, params::PitchMode::Semitone);
             c.snapshot.set (ParamId::pitchSemitones, 7.0f);
         });
    add ("Interval 5th up", engine,
         [] (Case& c)
         {
             setChoice (c.snapshot, ParamId::pitchMode, params::PitchMode::Interval);
             setChoice (c.snapshot, ParamId::pitchInterval, params::PitchInterval::Fifth);
             setChoice (c.snapshot, ParamId::pitchDirection, params::PitchDirection::Up);
         });
    add ("Pitch off", engine, [] (Case& c) { c.snapshot.set (ParamId::pitchOn, 0.0f); });
    add ("Freeze from bar 2", engine,
         [] (Case& c)
         {
             const auto bar = 4 * samplesPerBeat (c.sampleRate);
             c.perBlock = [bar] (ParamSnapshot& s, std::int64_t sample)
             { s.set (ParamId::grainFreeze, sample >= bar ? 1.0f : 0.0f); };
         });
    for (const auto mode :
         {params::SpaceMode::Cloud, params::SpaceMode::Plate, params::SpaceMode::Room, params::SpaceMode::Shimmer})
    {
        const auto modeName = juce::String (params::kSpaceModeNames[params::enumIndex (mode)].data());
        add ("Space " + modeName, engine,
             [mode] (Case& c)
             {
                 setChoice (c.snapshot, ParamId::spaceMode, mode);
                 c.snapshot.set (ParamId::spaceMix, 60.0f);
                 c.snapshot.set (ParamId::spaceDecay, 70.0f);
             });
    }

    // ---- Performance controls
    const juce::String perform = "Performance controls";
    add ("Storm 100%", perform, [] (Case& c) { c.snapshot.set (ParamId::stormAmount, 100.0f); });
    add ("Variation 100%", perform, [] (Case& c) { c.snapshot.set (ParamId::variationAmount, 100.0f); });
    add ("Thunderclap held in bar 2", perform,
         [] (Case& c)
         {
             const auto beat = samplesPerBeat (c.sampleRate);
             c.perBlock = [beat] (ParamSnapshot& s, std::int64_t sample)
             { s.set (ParamId::strikeClap, sample >= 4 * beat && sample < 6 * beat ? 1.0f : 0.0f); };
         });
    add ("Scatter held in bar 2", perform,
         [] (Case& c)
         {
             const auto beat = samplesPerBeat (c.sampleRate);
             c.perBlock = [beat] (ParamSnapshot& s, std::int64_t sample)
             { s.set (ParamId::strikeScatter, sample >= 4 * beat && sample < 6 * beat ? 1.0f : 0.0f); };
         });
    add ("Riser over 1 bar", perform,
         [] (Case& c)
         {
             setChoice (c.snapshot, ParamId::riserBars, params::RiserBars::Bar1);
             const auto beat = samplesPerBeat (c.sampleRate);
             c.perBlock = [beat] (ParamSnapshot& s, std::int64_t sample)
             { s.set (ParamId::riserGo, sample >= beat ? 1.0f : 0.0f); };
         });
    add ("Morph halfway to Glass Choir", perform,
         [] (Case& c)
         {
             c.snapshot.set (ParamId::morphAmount, 50.0f);
             c.setup = [] (ThunderEngine& e)
             {
                 for (const auto& f : presets::factoryPresets())
                 {
                     if (f.preset.meta.name != "Glass Choir")
                         continue;
                     perform::MorphSound b;
                     b.present = true;
                     for (const auto& def : params::kParamDefs)
                         b.values[params::toIndex (def.param)] = f.preset.valueOr (def.param);
                     e.getPerformance().setMorphSound (b);
                 }
             };
         });

    // ---- Every factory preset, by category
    for (const auto& f : presets::factoryPresets())
    {
        const auto snapshot = presetSnapshot (f.preset);
        add ("Preset " + f.preset.meta.name, f.preset.meta.category + " presets",
             [snapshot] (Case& c) { c.snapshot = snapshot; });
    }

    return cases;
}

// ---- Measuring ----------------------------------------------------------------------------------------------------

// Average power spectrum of `x` (Hann windows, half overlapping).
std::vector<double> powerSpectrum (const std::vector<float>& x)
{
    constexpr int size = 1 << kFftOrder;
    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> window (size), work (2 * size);
    for (int i = 0; i < size; ++i)
        window[static_cast<std::size_t> (i)] =
            0.5f - 0.5f * static_cast<float> (std::cos (2.0 * juce::MathConstants<double>::pi * i / size));

    std::vector<double> power (size / 2 + 1, 0.0);
    for (std::size_t start = 0; start + size <= x.size(); start += size / 2)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (std::size_t i = 0; i < size; ++i)
            work[i] = x[start + i] * window[i];
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (std::size_t k = 0; k < power.size(); ++k)
            power[k] += static_cast<double> (work[k]) * static_cast<double> (work[k]);
    }
    return power;
}

Metrics measure (const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    Metrics m;
    const auto n = left.size();

    double sum = 0.0, mid = 0.0, side = 0.0, peak = 0.0;
    std::vector<float> mono (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double l = left[i], r = right[i];
        sum += 0.5 * (l * l + r * r);
        mid += 0.25 * (l + r) * (l + r);
        side += 0.25 * (l - r) * (l - r);
        peak = std::max ({peak, std::abs (l), std::abs (r)});
        mono[i] = static_cast<float> (0.5 * (l + r));
    }
    m.rms = toDb (sum / static_cast<double> (n));
    m.peak = toDb (peak * peak);
    m.side = mid > 0.0 ? std::max (kLevelFloor, 10.0 * std::log10 (std::max (side, 1.0e-30) / mid)) : kLevelFloor;

    const auto step = static_cast<std::size_t> (samplesPerBeat (sampleRate) / 4); // a sixteenth note
    for (std::size_t start = 0; start + step <= n; start += step)
    {
        double e = 0.0;
        for (auto i = start; i < start + step; ++i)
            e += 0.5 * (static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i]);
        m.envelope.push_back (toDb (e / static_cast<double> (step)));
    }

    const auto power = powerSpectrum (mono);
    const auto binHz = sampleRate / (1 << kFftOrder);
    double weighted = 0.0, total = 0.0, cents = 0.0, pitched = 0.0;
    std::array<double, kNumBands> bands{};
    for (std::size_t k = 1; k < power.size(); ++k)
    {
        const auto hz = static_cast<double> (k) * binHz;
        if (hz < 20.0 || hz > 20000.0)
            continue;
        weighted += hz * power[k];
        total += power[k];
        // On a log scale, transposing moves this by exactly the interval.
        if (hz >= 50.0 && hz <= 10000.0)
        {
            cents += 1200.0 * std::log2 (hz / 440.0) * power[k];
            pitched += power[k];
        }
        // Octave bands centred on 62.5 Hz × 2^b, edges a half octave either side.
        const auto b = static_cast<int> (std::floor (std::log2 (hz / 62.5) + 0.5));
        if (b >= 0 && b < kNumBands)
            bands[static_cast<std::size_t> (b)] += power[k];
    }
    m.centroid = total > 0.0 ? weighted / total : 0.0;
    m.pitch = pitched > 0.0 ? cents / pitched : 0.0;
    for (const auto b : bands)
        m.bands.push_back (total > 0.0 && b > 0.0 ? std::max (kLevelFloor, 10.0 * std::log10 (b / total))
                                                  : kLevelFloor);
    return m;
}

double rounded (double v, double step = 0.01)
{
    return std::round (v / step) * step;
}

juce::var arrayOf (const std::vector<double>& values)
{
    juce::Array<juce::var> a;
    for (const auto v : values)
        a.add (rounded (v));
    return a;
}

bool readArray (const juce::var& v, std::vector<double>& out)
{
    const auto* a = v.getArray();
    if (a == nullptr)
        return false;
    out.clear();
    for (const auto& x : *a)
        out.push_back (static_cast<double> (x));
    return true;
}

// One measurement against its reference: how far it moved, as a fraction of
// its tolerance, comparing both above `floor`.
double deviation (double reference, double actual, double tolerance, double floor = kLevelFloor)
{
    return std::abs (std::max (actual, floor) - std::max (reference, floor)) / tolerance;
}

double relativeDeviation (double reference, double actual, double tolerance)
{
    return reference > 0.0 ? std::abs (actual - reference) / (reference * tolerance) : (actual > 0.0 ? 1.0e9 : 0.0);
}

// Every measurement's deviation with its label.
std::vector<std::pair<juce::String, double>> deviations (const Metrics& ref, const Metrics& act)
{
    std::vector<std::pair<juce::String, double>> d;
    auto value = [] (double v) { return juce::String (v, 2); };
    d.emplace_back ("level " + value (ref.rms) + " -> " + value (act.rms) + " dBFS",
                    deviation (ref.rms, act.rms, kRmsTolerance));
    d.emplace_back ("peak " + value (ref.peak) + " -> " + value (act.peak) + " dBFS",
                    deviation (ref.peak, act.peak, kPeakTolerance));
    d.emplace_back ("side " + value (ref.side) + " -> " + value (act.side) + " dB",
                    deviation (ref.side, act.side, kSideTolerance, kSideFloor));
    d.emplace_back ("centroid " + value (ref.centroid) + " -> " + value (act.centroid) + " Hz",
                    relativeDeviation (ref.centroid, act.centroid, kCentroidTolerance));
    d.emplace_back ("pitch " + value (ref.pitch) + " -> " + value (act.pitch) + " cents",
                    deviation (ref.pitch, act.pitch, kPitchTolerance, -1.0e9));

    if (ref.envelope.size() != act.envelope.size())
        d.emplace_back ("number of beats", 1.0e9);
    else
        for (std::size_t i = 0; i < ref.envelope.size(); ++i)
            d.emplace_back ("16th " + juce::String (i + 1) + " " + value (ref.envelope[i]) + " -> " +
                                value (act.envelope[i]) + " dBFS",
                            deviation (ref.envelope[i], act.envelope[i], kEnvelopeTolerance, kEnvelopeFloor));

    if (ref.bands.size() != act.bands.size())
        d.emplace_back ("number of bands", 1.0e9);
    else
        for (std::size_t i = 0; i < ref.bands.size(); ++i)
            d.emplace_back ("band " + juce::String (62.5 * std::pow (2.0, static_cast<double> (i)), 0) + " Hz " +
                                value (ref.bands[i]) + " -> " + value (act.bands[i]) + " dB",
                            deviation (ref.bands[i], act.bands[i], kBandTolerance, kBandFloor));
    return d;
}
} // namespace

// ---- Public -------------------------------------------------------------------------------------------------------

ParamSnapshot baseSnapshot()
{
    auto s = ParamSnapshot::defaults();
    setChoice (s, ParamId::keyRoot, params::KeyRoot::C);
    setChoice (s, ParamId::keyScale, params::Scale::NaturalMinor);
    setChoice (s, ParamId::outputMode, params::OutputMode::LayerOnly);
    return s;
}

const std::vector<Case>& allCases()
{
    static const auto cases = makeCases();
    return cases;
}

Metrics render (const Case& c)
{
    auto engine = std::make_unique<ThunderEngine>();
    engine->prepare (c.sampleRate, c.quality);
    if (c.setup)
        c.setup (*engine);

    const auto total = static_cast<std::size_t> (kBars * 4 * samplesPerBeat (c.sampleRate));
    std::vector<float> left (total), right (total);
    const auto samplesPerQuarter = c.sampleRate * 60.0 / kBpm;
    for (std::size_t i = 0; i < total; ++i)
        left[i] = right[i] = inputAt (i, c.sampleRate);

    auto snapshot = c.snapshot;
    for (std::size_t start = 0; start < total; start += kBlockSize)
    {
        const auto n = std::min<std::size_t> (kBlockSize, total - start);
        TransportState transport;
        transport.isPlaying = true;
        transport.hasHostInfo = true;
        transport.bpm = kBpm;
        transport.samplePosition = static_cast<std::int64_t> (start);
        transport.ppqPosition = static_cast<double> (start) / samplesPerQuarter;
        transport.ppqLastBarStart = std::floor (transport.ppqPosition / 4.0) * 4.0;

        if (c.perBlock)
            c.perBlock (snapshot, static_cast<std::int64_t> (start));

        float* channels[] = {left.data() + start, right.data() + start};
        engine->process (channels, static_cast<int> (n), transport, snapshot);
    }

    return measure (left, right, c.sampleRate);
}

juce::StringArray compare (const Metrics& reference, const Metrics& actual)
{
    juce::StringArray problems;
    for (const auto& [label, d] : deviations (reference, actual))
        if (d > 1.0)
            problems.add (label);
    return problems;
}

void Drift::add (const Metrics& reference, const Metrics& actual, const juce::String& caseName)
{
    for (const auto& [label, d] : deviations (reference, actual))
    {
        if (d > worst)
        {
            worst = d;
            where = caseName + ": " + label;
        }
    }
}

juce::var Metrics::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("rms", rounded (rms));
    o->setProperty ("peak", rounded (peak));
    o->setProperty ("side", rounded (side));
    o->setProperty ("centroid", rounded (centroid, 0.1));
    o->setProperty ("pitch", rounded (pitch, 0.1));
    o->setProperty ("envelope", arrayOf (envelope));
    o->setProperty ("bands", arrayOf (bands));
    return juce::var (o);
}

bool Metrics::fromVar (const juce::var& v, Metrics& out)
{
    if (!v.isObject())
        return false;
    out.rms = v["rms"];
    out.peak = v["peak"];
    out.side = v["side"];
    out.centroid = v["centroid"];
    if (!v.hasProperty ("pitch"))
        return false;
    out.pitch = v["pitch"];
    return readArray (v["envelope"], out.envelope) && readArray (v["bands"], out.bands);
}

juce::File referenceFile()
{
    const auto folder = juce::File (THUNDER_TEST_FIXTURES).getChildFile ("audio");
#if JUCE_MAC
    return folder.getChildFile ("regression-macos.json");
#else
    return folder.getChildFile ("regression.json");
#endif
}

juce::File outputFile()
{
    const auto out = juce::SystemStats::getEnvironmentVariable ("THUNDER_REGRESSION_OUT", {});
    return out.isEmpty() ? referenceFile() : juce::File::getCurrentWorkingDirectory().getChildFile (out);
}

juce::var readReferences()
{
    return juce::JSON::parse (referenceFile());
}

bool writeReferences (const std::vector<std::pair<juce::String, Metrics>>& results)
{
    // One case per line, in the cases' order, so a change shows as a readable diff.
    juce::String text = "{\n";
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        text << "  " << juce::JSON::toString (results[i].first) << ": "
             << juce::JSON::toString (
                    results[i].second.toVar(),
                    juce::JSON::FormatOptions{}.withMaxDecimalPlaces (2).withSpacing (juce::JSON::Spacing::singleLine))
             << (i + 1 < results.size() ? ",\n" : "\n");
    }
    text << "}\n";
    return outputFile().replaceWithText (text, false, false, "\n");
}

} // namespace thunder::test::regression
