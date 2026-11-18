#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace thunder
{

// The host's musical position, read once per host block (PRD FR-CORE-003).
//
// When the host gives no tempo (or an unusable one) Thunder falls back to
// 120 BPM in 4/4 and marks the state `hasHostInfo = false`, so tempo-synced
// features keep working in hosts or standalone setups without a transport.
struct TransportState
{
    static constexpr double kFallbackBpm = 120.0;
    static constexpr int kFallbackNumerator = 4;
    static constexpr int kFallbackDenominator = 4;

    double bpm = kFallbackBpm;
    int timeSigNumerator = kFallbackNumerator;
    int timeSigDenominator = kFallbackDenominator;
    bool isPlaying = false;
    bool hasHostInfo = false;     // false → tempo and meter are the fallback values
    double ppqPosition = 0.0;     // quarter notes since the song start, at the block start
    double ppqLastBarStart = 0.0; // quarter-note position of the bar containing ppqPosition
    std::int64_t samplePosition = 0;

    // [RT] Reads the host's position. A null play head, or one that reports
    // nothing, gives the fallback state.
    static TransportState fromPlayHead (const juce::AudioPlayHead* playHead) noexcept;

    // [RT] Builds the state from what the host reported, validating every field.
    static TransportState fromPosition (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept;

    double quarterNotesPerBar() const noexcept;
    double samplesPerQuarterNote (double sampleRate) const noexcept;
    double samplesPerBar (double sampleRate) const noexcept;

    // [RT] The state `numSamples` samples later, used to give each internal
    // chunk of a long host block its own position. Only moves while playing.
    TransportState advancedBy (int numSamples, double sampleRate) const noexcept;
};

} // namespace thunder
