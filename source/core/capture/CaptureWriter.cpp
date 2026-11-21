#include "CaptureWriter.h"

#include "CaptureNaming.h"

namespace thunder::capture
{

namespace
{
// How often the thread checks the queue while a capture is armed or
// recording, and while idle. The queue holds 2 s or more, so both are safe.
constexpr int kBusyPollMs = 10;
constexpr int kIdlePollMs = 250;
} // namespace

CaptureWriter::CaptureWriter (CaptureController& c)
    : juce::Thread ("Thunder capture writer")
    , controller (c)
{
}

CaptureWriter::~CaptureWriter()
{
    stopThread (4000);
    discard();
}

void CaptureWriter::setSettings (const Settings& newSettings)
{
    const juce::ScopedLock sl (lock);
    settings = newSettings;
}

void CaptureWriter::start()
{
    if (!isThreadRunning())
        startThread (juce::Thread::Priority::normal);

    notify();
}

void CaptureWriter::run()
{
    while (!threadShouldExit())
    {
        service();

        const auto busy = isOpen || controller.getPhase() != CaptureController::Phase::Idle;
        wait (busy ? kBusyPollMs : kIdlePollMs);
    }
}

void CaptureWriter::service()
{
    if (!controller.isQueueAllocated())
        return;

    auto& queue = controller.getQueue();

    while (queue.pop (&block, 1) == 1)
    {
        switch (block.kind)
        {
            case CaptureBlock::Kind::Start:
                begin (block);
                break;
            case CaptureBlock::Kind::Audio:
                write (block);
                break;
            case CaptureBlock::Kind::End:
                end (block);
                break;
            case CaptureBlock::Kind::Failed:
                if (block.captureId == openId)
                {
                    discard();
                    fail (block.failure);
                }
                break;
            case CaptureBlock::Kind::Cancelled:
                if (block.captureId == openId)
                {
                    discard();
                    state.store (State::Idle, std::memory_order_release);
                }
                break;
        }
    }
}

void CaptureWriter::begin (const CaptureBlock& startBlock)
{
    discard(); // a capture that never ended (shouldn't happen) is dropped
    openId = startBlock.captureId;

    Settings s;
    {
        const juce::ScopedLock sl (lock);
        s = settings;
    }

    // Creates the folder if needed, too.
    auto claimed =
        claimCaptureFile (s.folder, buildCaptureName (startBlock.start, s.presetName, juce::Time::getCurrentTime()));
    if (claimed.stream == nullptr)
    {
        fail (CaptureFailure::CannotCreateFile);
        return;
    }

    finalFile = claimed.finalFile;
    partialFile = partialFileFor (finalFile);
    std::unique_ptr<juce::OutputStream> stream = std::move (claimed.stream);

    using Options = juce::AudioFormatWriterOptions;
    const auto isFloat = s.format == CaptureFormat::Float32;
    const auto options =
        Options{}
            .withSampleRate (startBlock.start.sampleRate)
            .withNumChannels (2)
            .withBitsPerSample (isFloat ? 32 : 24)
            .withSampleFormat (isFloat ? Options::SampleFormat::floatingPoint : Options::SampleFormat::integral);

    writer = juce::WavAudioFormat().createWriterFor (stream, options);
    if (writer == nullptr)
    {
        stream.reset();
        partialFile.deleteFile();
        fail (CaptureFailure::CannotCreateFile);
        return;
    }

    isOpen = true;
    failure.store (CaptureFailure::None, std::memory_order_release);
    state.store (State::Writing, std::memory_order_release);
}

void CaptureWriter::write (const CaptureBlock& audio)
{
    if (!isOpen || audio.captureId != openId)
        return;

    const float* channels[] = {audio.audio[0].data(), audio.audio[1].data()};
    if (!writer->writeFromFloatArrays (channels, 2, audio.numFrames))
    {
        discard();
        fail (CaptureFailure::WriteFailed);
    }
}

void CaptureWriter::end (const CaptureBlock& endBlock)
{
    if (!isOpen || endBlock.captureId != openId)
        return;

    // Deleting the writer finishes the WAV header and closes the file.
    const auto flushed = writer->flush();
    writer.reset();
    isOpen = false;

    const auto finished = flushed ? finishCaptureFile (partialFile, finalFile) : juce::File();
    if (finished == juce::File())
    {
        partialFile.deleteFile();
        fail (CaptureFailure::WriteFailed);
        return;
    }
    finalFile = finished;

    {
        const juce::ScopedLock sl (lock);
        lastFile = finalFile;
    }

    completed.fetch_add (1, std::memory_order_acq_rel);
    state.store (State::Ready, std::memory_order_release);
}

void CaptureWriter::discard()
{
    if (!isOpen)
        return;

    writer.reset();
    partialFile.deleteFile();
    isOpen = false;
}

void CaptureWriter::fail (CaptureFailure reason)
{
    failure.store (reason, std::memory_order_release);
    state.store (State::Failed, std::memory_order_release);
}

void CaptureWriter::deleteLastCapture()
{
    const juce::ScopedLock sl (lock);

    if (lastFile == juce::File())
        return;

    lastFile.deleteFile();
    lastFile = juce::File();

    if (state.load (std::memory_order_acquire) == State::Ready)
        state.store (State::Idle, std::memory_order_release);
}

juce::File CaptureWriter::getLastFile() const
{
    const juce::ScopedLock sl (lock);
    return lastFile;
}

} // namespace thunder::capture
