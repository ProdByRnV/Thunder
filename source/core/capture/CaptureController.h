#pragma once

#include "CaptureTypes.h"

#include "core/host/TransportState.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/SpscFifo.h"

#include <atomic>
#include <cstdint>

namespace thunder::capture
{

// The audio-thread side of capture (PRD FR-CAPT-001…006, ARCHITECTURE §8).
//
// Arm → wait for the transport to play → start at the next bar → record N bars
// of the Layer or of Thunder's whole output → done. Audio goes into a lock-free
// queue that a CaptureWriter drains on a background thread, so the audio thread
// never touches the disk.
//
// Bar alignment (FR-CAPT-006): bars are found in input time from the host's
// position. Thunder's output is `latency` samples behind its input, so the
// capture starts and ends `latency` samples after the bar lines in output time,
// and the file's first sample is exactly what Thunder made from the bar's first
// input sample.
//
// Transport (ARCHITECTURE §8.2):
// - Armed, stopped: waits ("Press play to capture").
// - Recording, stopped before the last bar's input: the capture fails.
// - Recording, jumped by a whole number of bars (a loop wrapping around): the
//   audio is continuous and on the same bar grid, so the capture carries on.
//   Any other jump fails the capture.
// The end is found by counting beats at the host's tempo, so tempo changes
// during a capture still give exactly N bars.
//
// Include Tail and Loop (PRD FR-CAPT-004): after the N bars, recording carries
// on until the captured audio has stayed below −60 dBFS for 0.25 s, for at most
// 10 s. Transport changes no longer matter then (the tail rings on when the
// DAW stops); Stop, a host reset or a re-prepare end the tail and keep the
// capture. The End block says where the N bars end, so the writer can fold the
// tail onto the start for Loop (LoopFold.h).
//
// Input captures (PRD FR-INPUT-003) record Thunder's input, in input time: the
// file starts exactly on the bar line. They have no tail.
//
// Threads: arm() and cancel() are [MSG]; everything else that changes state is
// [RT]. The getters may be called from any thread.
class CaptureController
{
public:
    enum class Phase
    {
        Idle,
        Armed,
        Recording
    };

    // The queue holds 2 s of audio at 192 kHz (more at lower rates), about 3 MB.
    static constexpr double kQueueSeconds = 2.0;
    static constexpr double kQueueMaxSampleRate = 192000.0;

    // Transport positions this close (in quarter notes: a 1/32 note, about 16 ms
    // at 120 BPM) count as continuous. Some hosts report positions rounded to
    // their own tick grid; real jumps are far bigger.
    static constexpr double kPositionTolerance = 1.0 / 32.0;

    // Include Tail: quiet means below this level (−60 dBFS) for this long.
    static constexpr float kTailQuietLevel = 0.001f;
    static constexpr double kTailQuietSeconds = 0.25;
    static constexpr double kMaxTailSeconds = 10.0;

    CaptureController() = default;

    // [NRT] Called from the engine's prepare(). Ends any capture in progress.
    void prepare (double sampleRate, int latencySamples) noexcept;

    // [MSG] Allocates the queue the first time it is called (not in prepare(),
    // so instances that never capture don't hold the memory). Call before arm().
    void allocateQueue();
    bool isQueueAllocated() const noexcept { return queueAllocated.load (std::memory_order_acquire); }

    // [MSG] Arms a capture of `bars` bars. Ignored while recording; while armed
    // it changes the length, source and tail options of the coming capture.
    // `loop` implies `tail`; neither applies to Input captures.
    void arm (int bars, CaptureSource source, bool tail = false, bool loop = false) noexcept;

    // [MSG] Disarms, or stops a recording and discards it.
    void cancel() noexcept;

    // [RT] Applies arm/cancel requests. The engine calls it at the start of
    // every host block, before asking wantsLayer().
    void handleRequests() noexcept;

    // [RT] True while a Layer capture is armed or recording: the engine then
    // renders the layer as Layer Only would play it.
    bool wantsLayer() const noexcept { return phase != Phase::Idle && source == CaptureSource::Layer; }

    // [RT] True while an Input capture is armed or recording: the engine then
    // passes Thunder's input to process().
    bool wantsInput() const noexcept { return phase != Phase::Idle && source == CaptureSource::Input; }

    // [RT] One engine chunk. `mix` is Thunder's output; `layer` is the layer as
    // Layer Only would play it, and may be null unless wantsLayer() is true;
    // `input` is the chunk's input, and may be null unless wantsInput() is true.
    // `transport` is the chunk's position in input time.
    void process (const float* const* mix, const float* const* layer, const float* const* input, int numSamples,
                  const TransportState& transport, const params::ParamSnapshot& snapshot) noexcept;

    // [RT] The host reset the plugin: a recording fails (a tail ends and the
    // capture is kept); an armed capture stays armed.
    void reset() noexcept;

    // The queue the writer drains (single consumer).
    util::SpscFifo<CaptureBlock>& getQueue() noexcept { return queue; }

    Phase getPhase() const noexcept { return publishedPhase.load (std::memory_order_acquire); }
    bool isWaitingForPlay() const noexcept { return waitingForPlay.load (std::memory_order_relaxed); }

    // Fraction of the current recording done (0 … 1).
    float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

    // True while recording the tail after the last bar.
    bool isRecordingTail() const noexcept { return recordingTail.load (std::memory_order_relaxed); }

    // Bars of the capture armed or recording.
    int getBars() const noexcept { return publishedBars.load (std::memory_order_relaxed); }

    // [RT] The first sample of the chunk on or after a bar line (the sample
    // nearest the bar's exact time), or −1 if no bar starts in this chunk.
    static int findBarStart (const TransportState& transport, double sampleRate, int numSamples) noexcept;

private:
    enum class Request
    {
        None,
        Arm,
        Cancel
    };

    void setPhase (Phase newPhase) noexcept;
    void beginRecording (const TransportState& transport, int offset, const params::ParamSnapshot& snapshot) noexcept;
    bool checkTransport (const TransportState& transport) noexcept;
    void collect (const float* const* audio, int numSamples) noexcept;
    bool flushPending() noexcept;
    void finish (CaptureBlock::Kind kind, CaptureFailure failure = CaptureFailure::None) noexcept;
    bool isInTail() const noexcept { return tail && endSample >= 0 && clock >= endSample; }
    void endKeepingCapture() noexcept; // a tail cut short: the N bars are complete

    util::SpscFifo<CaptureBlock> queue;
    std::atomic<bool> queueAllocated{false};

    // [MSG] → [RT]
    std::atomic<Request> request{Request::None};
    std::atomic<int> requestedBars{4};
    std::atomic<CaptureSource> requestedSource{CaptureSource::Layer};
    std::atomic<bool> requestedTail{false};
    std::atomic<bool> requestedLoop{false};

    // [RT] → anyone
    std::atomic<Phase> publishedPhase{Phase::Idle};
    std::atomic<bool> waitingForPlay{false};
    std::atomic<float> progress{0.0f};
    std::atomic<int> publishedBars{4};
    std::atomic<bool> recordingTail{false};

    // Audio-thread state.
    double sampleRate = 44100.0;
    int latency = 0;
    Phase phase = Phase::Idle;
    int bars = 4;
    CaptureSource source = CaptureSource::Layer;
    bool tail = false;
    bool loop = false;
    int captureLatency = 0; // the output's latency, or 0 for Input captures
    std::uint32_t captureId = 0;

    std::int64_t clock = 0;       // output samples processed since prepare()
    std::int64_t startSample = 0; // first output sample of the capture
    std::int64_t endSample = -1;  // one past the last; −1 until the input reaches the end
    double beatsDone = 0.0;       // input-time quarter notes from the first bar line to the next chunk
    double beatsToRecord = 0.0;   // N bars in quarter notes
    double expectedPpq = 0.0;     // where the next chunk should start if playback is continuous
    bool hasExpectedPpq = false;
    std::int64_t framesCaptured = 0;
    std::int64_t expectedFrames = 1;
    std::int64_t tailFrames = 0;  // recorded after the N bars
    std::int64_t quietFrames = 0; // the tail's latest run of quiet frames

    CaptureBlock pending; // audio waiting for a full block

    JUCE_DECLARE_NON_COPYABLE (CaptureController)
};

static_assert (std::atomic<CaptureController::Phase>::is_always_lock_free);
static_assert (std::atomic<CaptureSource>::is_always_lock_free);

} // namespace thunder::capture
