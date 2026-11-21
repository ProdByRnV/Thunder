#include "CaptureStatus.h"

#include <algorithm>
#include <cmath>

namespace thunder::capture
{

CaptureStatus CaptureStatus::from (const CaptureController& controller, const CaptureWriter& writer)
{
    CaptureStatus status;
    status.bars = controller.getBars();
    status.file = writer.getLastFile();

    switch (controller.getPhase())
    {
        case CaptureController::Phase::Armed:
            status.phase = controller.isWaitingForPlay() ? Phase::WaitingForPlay : Phase::Armed;
            return status;
        case CaptureController::Phase::Recording:
            status.phase = Phase::Recording;
            status.progress = controller.getProgress();
            return status;
        case CaptureController::Phase::Idle:
            break;
    }

    switch (writer.getState())
    {
        case CaptureWriter::State::Idle:
            status.phase = Phase::Idle;
            break;
        case CaptureWriter::State::Writing:
            status.phase = Phase::Saving;
            break;
        case CaptureWriter::State::Ready:
            status.phase = Phase::Ready;
            break;
        case CaptureWriter::State::Failed:
            status.phase = Phase::Failed;
            status.failure = writer.getFailure();
            break;
    }

    return status;
}

int CaptureStatus::currentBar() const noexcept
{
    const auto done = static_cast<int> (std::floor (progress * static_cast<float> (bars)));
    return std::clamp (done + 1, 1, std::max (1, bars));
}

juce::String CaptureStatus::describe() const
{
    switch (phase)
    {
        case Phase::Idle:
            return file == juce::File() ? juce::String ("Arm, then play: capture starts at the next bar")
                                        : file.getFileName();
        case Phase::WaitingForPlay:
            return "Press play to capture";
        case Phase::Armed:
            return "Starts at the next bar";
        case Phase::Recording:
            return "Recording bar " + juce::String (currentBar()) + " of " + juce::String (bars);
        case Phase::Saving:
            return juce::String::fromUTF8 ("Saving\xe2\x80\xa6");
        case Phase::Ready:
            return file.getFileName();
        case Phase::Failed:
            return capture::describe (failure);
    }

    return {};
}

juce::String describe (CaptureFailure failure)
{
    switch (failure)
    {
        case CaptureFailure::TransportStopped:
            return "Playback stopped before the last bar. Nothing was saved.";
        case CaptureFailure::TransportJumped:
            return "Playback jumped during the capture. Nothing was saved.";
        case CaptureFailure::Restarted:
            return "Playback restarted during the capture. Nothing was saved.";
        case CaptureFailure::TooSlow:
            return "The disk couldn't keep up. Nothing was saved.";
        case CaptureFailure::CannotCreateFile:
            return "Couldn't create the file. Check the captures folder.";
        case CaptureFailure::WriteFailed:
            return "Couldn't finish the file. Is the disk full?";
        case CaptureFailure::None:
            break;
    }

    return "The capture failed.";
}

} // namespace thunder::capture
