#pragma once

#include "core/analysis/KeyDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace thunder::source
{

// A loaded or captured source (PRD FR-INPUT-001…004): audio for analysis now,
// and for rendering layers from it later (v2.0). Built on a background thread
// and never changed after it is published, so any thread may read it.
//
// The audio keeps the file's own sample rate: nothing plays it back in real
// time, and resampling now would only lose detail before a later render.
struct SourceBuffer
{
    juce::AudioBuffer<float> audio; // one or two channels
    double sampleRate = 0.0;

    juce::File file;   // where it came from (empty if only the saved copy remains)
    juce::String name; // what the UI calls it: the file's name
    bool fromCapture = false;

    // For captured input: the host's tempo and meter while it was recorded,
    // and how many bars it is. 0 bpm = unknown (dropped files): the bar grid
    // then follows the host's tempo.
    double bpm = 0.0;
    int bars = 0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    // Where the first bar starts, in samples (PRD FR-INPUT-004). Files and
    // captures both start on a bar.
    std::int64_t gridOffsetSamples = 0;

    analysis::KeyEstimate key;

    // The loudest sample (all channels) in each of kPeakCount equal slices,
    // for the source strip's waveform.
    static constexpr int kPeakCount = 1024;
    std::vector<float> peaks;

    // A copy saved with the project, when the source is small enough
    // (FLAC, 16-bit); null otherwise.
    std::shared_ptr<const juce::MemoryBlock> embedded;

    double getSeconds() const noexcept { return sampleRate > 0.0 ? audio.getNumSamples() / sampleRate : 0.0; }
};

} // namespace thunder::source
