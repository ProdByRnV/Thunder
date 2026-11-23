#pragma once

#include "AnalysisTap.h"
#include "KeyDetector.h"

#include "core/source/SourceLoader.h"
#include "core/source/SourceRef.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace thunder::analysis
{

// Thunder's analysis and source thread (ARCHITECTURE §7.3–§7.4, §11): one
// background thread per instance that
// - loads sources: dropped files, captured input and the source saved with a
//   project (decode, peaks, key, the project copy), and publishes each as an
//   immutable SourceBuffer;
// - detects the key of the live input from the audio thread's AnalysisTap
//   while Auto Detect is on (PRD FR-KEY-002).
//
// Detection only reports. Nothing here ever changes the key parameters: the
// user does, by choosing the detected key (FR-KEY-003).
//
// The audio thread never holds a source: in v1.0 nothing plays one back, and
// only analysis and the editor read it, so it is swapped under a lock that
// only this thread and the message thread take.
//
// Threads: the [MSG] calls may also come from other background threads (the
// capture writer); getters from any thread except the audio thread.
class AnalysisWorker : private juce::Thread
{
public:
    // The live estimate forgets audio older than about this (seconds).
    static constexpr double kLiveMemorySeconds = 30.0;

    enum class SourcePhase
    {
        None,    // no source
        Loading, // `name` is being loaded
        Ready,   // `buffer` is the source
        Failed   // `error` says why; a saved path stays remembered
    };

    struct SourceStatus
    {
        SourcePhase phase = SourcePhase::None;
        float progress = 0.0f;
        juce::String name;
        source::LoadError error = source::LoadError::None;
        bool usedSavedCopy = false; // the file was missing: the project's copy was loaded
        std::shared_ptr<const source::SourceBuffer> buffer;
        std::uint32_t version = 0; // changes whenever anything above does
    };

    // What a captured file is (from the capture that wrote it).
    struct CaptureInfo
    {
        double bpm = 0.0;
        int bars = 0;
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
    };

    explicit AnalysisWorker (AnalysisTap& tap);
    ~AnalysisWorker() override;

    // [MSG] Starts the thread (and the tap's queue) if it isn't running.
    void start();

    // [MSG] Auto Detect: listen to the input (or stop, and forget what was heard).
    void setLiveDetection (bool on);
    bool isLiveDetectionOn() const noexcept { return liveOn.load(); }

    // [MSG] Load a dropped file as the source.
    void loadFile (const juce::File& file);

    // [MSG/BG] Load an input capture that has just been written.
    void loadCapture (const juce::File& file, const CaptureInfo& info);

    // [MSG] The source saved with a project. Loading the source already loaded
    // (hosts restore state more than once) does nothing.
    void restore (const source::SourceRef& ref);

    // [MSG] No source.
    void clearSource();

    // What to save with the project: the source asked for (even while it
    // loads, or if its file is missing), with its copy when there is one.
    source::SourceRef getSourceRef() const;

    SourceStatus getSourceStatus() const;
    KeyEstimate getLiveEstimate() const;

    // [BG] One round of work: the pending source request, then the live input.
    // The thread calls it; tests call it directly instead of start().
    void service();

private:
    struct Job
    {
        enum class Kind
        {
            file,
            capture,
            restore
        };

        Kind kind = Kind::file;
        juce::File file;
        CaptureInfo capture;
        source::SourceRef ref;
        std::uint32_t id = 0;
    };

    void run() override;
    void runJob (const Job& job);
    void analyseLive();
    void publish (const std::function<void (SourceStatus&)>& change);
    bool isCurrent (std::uint32_t id) const noexcept { return latestJob.load() == id; }

    AnalysisTap& tap;

    mutable juce::CriticalSection lock; // never taken on the audio thread
    std::optional<Job> pending;
    source::SourceRef ref;
    SourceStatus status;
    KeyEstimate liveEstimate;
    std::atomic<std::uint32_t> latestJob{0};
    std::atomic<bool> liveOn{false};
    std::atomic<bool> liveReset{false};

    // Analysis-thread state.
    KeyDetector detector;
    std::uint32_t tapGeneration = 0;
    std::vector<float> scratch;

    JUCE_DECLARE_NON_COPYABLE (AnalysisWorker)
};

} // namespace thunder::analysis
