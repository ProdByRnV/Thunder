// Reading the host's tempo, meter and position (PRD FR-CORE-003).

#include "core/host/TransportState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

using Catch::Matchers::WithinAbs;
using thunder::TransportState;

namespace
{
using PositionInfo = juce::AudioPlayHead::PositionInfo;

void checkIsFallback (const TransportState& t)
{
    CHECK_FALSE (t.hasHostInfo);
    CHECK_THAT (t.bpm, WithinAbs (120.0, 1e-9));
    CHECK (t.timeSigNumerator == 4);
    CHECK (t.timeSigDenominator == 4);
}
} // namespace

TEST_CASE ("No host transport gives 120 BPM in 4/4", "[unit][host]")
{
    const auto noPlayHead = TransportState::fromPlayHead (nullptr);
    checkIsFallback (noPlayHead);
    CHECK_FALSE (noPlayHead.isPlaying);

    checkIsFallback (TransportState::fromPosition ({}));
}

TEST_CASE ("The host's tempo, meter and position are used", "[unit][host]")
{
    PositionInfo info;
    info.setBpm (90.0);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature{7, 8});
    info.setIsPlaying (true);
    info.setPpqPosition (9.0);
    info.setPpqPositionOfLastBarStart (7.0);
    info.setTimeInSamples (123456);

    const auto t = TransportState::fromPosition (info);

    CHECK (t.hasHostInfo);
    CHECK (t.isPlaying);
    CHECK_THAT (t.bpm, WithinAbs (90.0, 1e-9));
    CHECK (t.timeSigNumerator == 7);
    CHECK (t.timeSigDenominator == 8);
    CHECK_THAT (t.ppqPosition, WithinAbs (9.0, 1e-9));
    CHECK_THAT (t.ppqLastBarStart, WithinAbs (7.0, 1e-9));
    CHECK (t.samplePosition == 123456);
    CHECK_THAT (t.quarterNotesPerBar(), WithinAbs (3.5, 1e-9));
}

TEST_CASE ("Unusable host values fall back safely", "[unit][host]")
{
    for (const auto badBpm : {0.0, -120.0, 5000.0, std::numeric_limits<double>::quiet_NaN()})
    {
        PositionInfo info;
        info.setBpm (badBpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature{3, 5}); // not a real meter
        checkIsFallback (TransportState::fromPosition (info));
    }
}

TEST_CASE ("A missing or wrong bar start is worked out from the position", "[unit][host]")
{
    PositionInfo info;
    info.setBpm (120.0);
    info.setPpqPosition (9.5);

    CHECK_THAT (TransportState::fromPosition (info).ppqLastBarStart, WithinAbs (8.0, 1e-9));

    info.setPpqPositionOfLastBarStart (2.0); // not the bar containing 9.5
    CHECK_THAT (TransportState::fromPosition (info).ppqLastBarStart, WithinAbs (8.0, 1e-9));
}

TEST_CASE ("Tempo converts to samples", "[unit][host]")
{
    TransportState t;
    CHECK_THAT (t.samplesPerQuarterNote (48000.0), WithinAbs (24000.0, 1e-9));
    CHECK_THAT (t.samplesPerBar (48000.0), WithinAbs (96000.0, 1e-9));

    t.timeSigNumerator = 6;
    t.timeSigDenominator = 8;
    CHECK_THAT (t.samplesPerBar (48000.0), WithinAbs (72000.0, 1e-9));
}

TEST_CASE ("Advancing moves the position only while playing", "[unit][host]")
{
    TransportState t;
    t.isPlaying = true;
    t.ppqPosition = 3.5;
    t.ppqLastBarStart = 0.0;
    t.samplePosition = 1000;

    const auto later = t.advancedBy (24000, 48000.0); // one quarter note at 120 BPM
    CHECK_THAT (later.ppqPosition, WithinAbs (4.5, 1e-9));
    CHECK_THAT (later.ppqLastBarStart, WithinAbs (4.0, 1e-9));
    CHECK (later.samplePosition == 25000);

    t.isPlaying = false;
    const auto stopped = t.advancedBy (24000, 48000.0);
    CHECK_THAT (stopped.ppqPosition, WithinAbs (3.5, 1e-9));
    CHECK (stopped.samplePosition == 1000);
}
