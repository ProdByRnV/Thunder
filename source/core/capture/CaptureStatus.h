#pragma once

#include "CaptureWriter.h"

#include <juce_core/juce_core.h>

namespace thunder::capture
{

// What the capture widget shows (UI.md §9.10), put together from the audio
// side and the writer. Message thread.
struct CaptureStatus
{
    enum class Phase
    {
        Idle,           // nothing armed; `file` is the last capture, if any
        WaitingForPlay, // armed, transport stopped
        Armed,          // armed, waiting for the next bar
        Recording,
        Saving,
        Ready, // `file` was just captured
        Failed
    };

    Phase phase = Phase::Idle;
    int bars = 4;
    float progress = 0.0f; // 0 … 1 while recording
    bool tail = false;     // recording the tail after the last bar (Include Tail)
    CaptureFailure failure = CaptureFailure::None;
    juce::File file;

    static CaptureStatus from (const CaptureController& controller, const CaptureWriter& writer);

    // True while the Arm button should read "armed" or "stop".
    bool isActive() const noexcept
    {
        return phase == Phase::WaitingForPlay || phase == Phase::Armed || phase == Phase::Recording;
    }

    // The bar being recorded, 1 … bars.
    int currentBar() const noexcept;

    // The capture chip's text: the file name, or what is happening.
    juce::String describe() const;
};

// A short sentence for each failure.
juce::String describe (CaptureFailure failure);

} // namespace thunder::capture
