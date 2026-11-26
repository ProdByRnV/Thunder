#pragma once

// The audio regression suite (PRD QA-AUDIO-001, ROADMAP T-0.9-10): fixed
// settings rendered through the whole engine from the same synthetic melody,
// measured, and compared with the measurements stored in
// tests/fixtures/audio/regression.json (Windows and Linux, which agree to within
// rounding) or regression-macos.json (macOS: the octave-up shifter with Preserve
// Formants measures 5–12 cents higher there, PRD DEC-061).
//
// Measurements, not samples: compilers and processors round floating point
// differently, so the same settings never give bit-identical audio everywhere.
// What is compared is what a listener would notice: loudness, peak, stereo
// width, brightness, pitch, the level of every sixteenth note and the balance
// of nine octave bands. Each has a tolerance well above rounding and small
// enough to catch a nudged control (a few dB of reverb, a 20% filter move, a
// shift of under 10 cents).
//
// After an intended change to the sound, rewrite the references with
//   ThunderTests "[regression-write]"
// and commit the file with the change. The environment variable
// THUNDER_REGRESSION_OUT writes them somewhere else instead; CI does that when
// a job fails and keeps the file, which is how the macOS references are made.

#include "core/engine/ThunderEngine.h"
#include "core/params/ParamSnapshot.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace thunder::test::regression
{

// One fixed setting.
struct Case
{
    juce::String name;
    juce::String group; // the test case that checks it
    double sampleRate = 48000.0;
    params::Quality quality = params::Quality::Standard;
    params::ParamSnapshot snapshot;
    std::function<void (ThunderEngine&)> setup;                                 // before the first block
    std::function<void (params::ParamSnapshot&, std::int64_t sample)> perBlock; // changes during the render
};

struct Metrics
{
    double rms = 0.0;             // dBFS, both channels
    double peak = 0.0;            // dBFS
    double side = 0.0;            // side against mid, dB
    double centroid = 0.0;        // Hz
    double pitch = 0.0;           // where the spectrum sits on a log scale, cents from A4
    std::vector<double> envelope; // the level of each sixteenth note, dBFS
    std::vector<double> bands;    // octave bands from 62.5 Hz to 16 kHz, dB against the total

    juce::var toVar() const;
    static bool fromVar (const juce::var& v, Metrics& out);
};

// The render: three bars at 120 BPM of the plucked C minor melody, the song
// playing from its start, key C Natural Minor, Output Mode Layer Only (the
// layer itself is measured, not diluted by the melody; two cases measure Blend).
inline constexpr double kBpm = 120.0;
inline constexpr int kBars = 3;
inline constexpr int kBlockSize = 512;

// The settings every case starts from: the defaults, key C Natural Minor,
// Layer Only.
params::ParamSnapshot baseSnapshot();

// Every case, in a fixed order.
const std::vector<Case>& allCases();

Metrics render (const Case& c);

// Empty if `actual` is within tolerance of `reference`; otherwise one line per
// measurement that moved too far.
juce::StringArray compare (const Metrics& reference, const Metrics& actual);

// How far each kind of measurement moved, as a fraction of its tolerance (the
// largest over the cases given so far).
struct Drift
{
    double worst = 0.0;
    juce::String where;
    void add (const Metrics& reference, const Metrics& actual, const juce::String& caseName);
};

juce::File referenceFile();
juce::File outputFile(); // where [regression-write] writes
juce::var readReferences();
bool writeReferences (const std::vector<std::pair<juce::String, Metrics>>& results);

} // namespace thunder::test::regression
