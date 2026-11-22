#include "TransportState.h"

#include <cmath>

namespace thunder
{

namespace
{
// Anything outside this is treated as a host bug, not a tempo.
constexpr double kMinBpm = 10.0;
constexpr double kMaxBpm = 1000.0;

bool isPowerOfTwo (int n) noexcept
{
    return n > 0 && (n & (n - 1)) == 0;
}

// Start of the bar containing `ppq`, assuming the meter has not changed since
// the song start. Only used when the host doesn't report the bar start itself.
double barStartFor (double ppq, double quarterNotesPerBar) noexcept
{
    return std::floor (ppq / quarterNotesPerBar) * quarterNotesPerBar;
}
} // namespace

TransportState TransportState::fromPlayHead (const juce::AudioPlayHead* playHead) noexcept
{
    if (playHead == nullptr)
        return {};

    return fromPosition (playHead->getPosition());
}

TransportState TransportState::fromPosition (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept
{
    TransportState state;

    if (!position.hasValue())
        return state;

    if (const auto bpm = position->getBpm();
        bpm.hasValue() && std::isfinite (*bpm) && *bpm >= kMinBpm && *bpm <= kMaxBpm)
    {
        state.bpm = *bpm;
        state.hasHostInfo = true;
    }

    if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->numerator >= 1 && sig->numerator <= 64 &&
                                                       isPowerOfTwo (sig->denominator) && sig->denominator <= 64)
    {
        state.timeSigNumerator = sig->numerator;
        state.timeSigDenominator = sig->denominator;
    }

    state.isPlaying = position->getIsPlaying();

    if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
    {
        state.ppqPosition = *ppq;

        const auto barStart = position->getPpqPositionOfLastBarStart();
        const auto barLength = state.quarterNotesPerBar();

        // Use the host's bar start only if it really is the start of the bar we're in.
        if (barStart.hasValue() && std::isfinite (*barStart) && *barStart <= *ppq + 1.0e-9 &&
            *ppq - *barStart < barLength + 1.0e-9)
            state.ppqLastBarStart = *barStart;
        else
            state.ppqLastBarStart = barStartFor (*ppq, barLength);
    }

    if (const auto samples = position->getTimeInSamples(); samples.hasValue())
        state.samplePosition = *samples;

    return state;
}

double TransportState::quarterNotesPerBar() const noexcept
{
    return timeSigNumerator * 4.0 / timeSigDenominator;
}

double TransportState::samplesPerQuarterNote (double sampleRate) const noexcept
{
    return sampleRate * 60.0 / bpm;
}

double TransportState::samplesPerBar (double sampleRate) const noexcept
{
    return samplesPerQuarterNote (sampleRate) * quarterNotesPerBar();
}

TransportState TransportState::advancedBy (int numSamples, double sampleRate) const noexcept
{
    auto next = *this;

    if (!isPlaying || numSamples <= 0)
        return next;

    next.samplePosition += numSamples;
    next.ppqPosition += numSamples / samplesPerQuarterNote (sampleRate);

    const auto barLength = quarterNotesPerBar();
    if (next.ppqPosition >= ppqLastBarStart + barLength)
        next.ppqLastBarStart =
            ppqLastBarStart + std::floor ((next.ppqPosition - ppqLastBarStart) / barLength) * barLength;

    return next;
}

TransportState TransportState::delayedBy (int numSamples, double sampleRate) const noexcept
{
    auto earlier = *this;

    if (!isPlaying || numSamples <= 0)
        return earlier;

    earlier.samplePosition -= numSamples;
    earlier.ppqPosition -= numSamples / samplesPerQuarterNote (sampleRate);

    const auto barLength = quarterNotesPerBar();
    if (earlier.ppqPosition < ppqLastBarStart)
        earlier.ppqLastBarStart =
            ppqLastBarStart - std::ceil ((ppqLastBarStart - earlier.ppqPosition) / barLength) * barLength;

    return earlier;
}

} // namespace thunder
