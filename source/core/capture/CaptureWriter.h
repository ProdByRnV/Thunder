#pragma once

#include "CaptureController.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace thunder::capture
{

// The background side of capture (PRD FR-CAPT-005/009, ARCHITECTURE §8): drains
// the controller's queue and streams each capture to a WAV file.
//
// - The file is written as `<name>.wav.partial` and renamed when complete, so
//   a half-written capture is never offered for drag-out (ARCHITECTURE AD-04).
// - A capture that fails or is cancelled leaves no file behind.
// - 24-bit or 32-bit float at the host's sample rate, stereo.
// - Loop captures are folded (LoopFold.h) before the file is offered.
//
// Threads: the writer runs on its own background thread, started by the first
// start() call. Tests may instead call service() themselves without starting
// it. Getters may be called from any thread.
class CaptureWriter : private juce::Thread
{
public:
    enum class State
    {
        Idle,    // nothing written yet, or the last capture was cancelled or deleted
        Writing, // a capture is being written
        Ready,   // getLastFile() is a complete capture
        Failed   // getFailure() says why the last capture produced no file
    };

    // What the next capture uses; read when its first bar starts.
    struct Settings
    {
        juce::File folder;
        CaptureFormat format = CaptureFormat::Int24;
        juce::String presetName;
    };

    explicit CaptureWriter (CaptureController& controller);

    // Stops the thread; a capture still being written is discarded.
    ~CaptureWriter() override;

    // [MSG]
    void setSettings (const Settings& settings);

    // [MSG] Starts the background thread if needed and wakes it. Call right
    // after arming a capture.
    void start();

    // [BG] Writes whatever is in the queue. The thread calls this; tests call
    // it directly.
    void service();

    // Called on the writer's thread after each completed capture, with the file
    // and what started it (Input captures become the source). Set before the
    // first capture.
    std::function<void (const juce::File&, const CaptureStart&)> onCaptureFinished;

    // [MSG] Deletes the last completed capture's file (UI "Delete capture").
    void deleteLastCapture();

    State getState() const noexcept { return state.load (std::memory_order_acquire); }
    CaptureFailure getFailure() const noexcept { return failure.load (std::memory_order_acquire); }

    // The last completed capture (empty if none, or deleted).
    juce::File getLastFile() const;

    // How many captures have completed; lets the UI notice a new one.
    std::uint32_t getCompletedCount() const noexcept { return completed.load (std::memory_order_acquire); }

private:
    void run() override;

    void begin (const CaptureBlock& block);
    void write (const CaptureBlock& block);
    void end (const CaptureBlock& block);
    void discard();
    void fail (CaptureFailure reason);

    CaptureController& controller;

    juce::CriticalSection lock; // never taken on the audio thread
    Settings settings;
    juce::File lastFile;

    std::atomic<State> state{State::Idle};
    std::atomic<CaptureFailure> failure{CaptureFailure::None};
    std::atomic<std::uint32_t> completed{0};

    // Writer-thread state.
    CaptureBlock block;
    std::uint32_t openId = 0;
    CaptureStart openStart;
    CaptureFormat openFormat = CaptureFormat::Int24;
    bool isOpen = false;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File partialFile;
    juce::File finalFile;

    JUCE_DECLARE_NON_COPYABLE (CaptureWriter)
};

} // namespace thunder::capture
