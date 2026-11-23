#include "AnalysisWorker.h"

namespace thunder::analysis
{

namespace
{
constexpr int kPollMs = 100;
constexpr int kReadChunk = 8192;
} // namespace

AnalysisWorker::AnalysisWorker (AnalysisTap& t)
    : juce::Thread ("Thunder analysis")
    , tap (t)
{
    detector.setMemorySeconds (kLiveMemorySeconds);
    scratch.resize (kReadChunk);
}

AnalysisWorker::~AnalysisWorker()
{
    tap.setEnabled (false);
    latestJob.fetch_add (1); // cancels a load in progress
    stopThread (4000);
}

void AnalysisWorker::start()
{
    tap.allocate();
    tap.setEnabled (liveOn.load());
    if (!isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void AnalysisWorker::setLiveDetection (bool on)
{
    if (liveOn.exchange (on) == on)
        return;

    tap.setEnabled (on);
    liveReset.store (true);
    if (!on)
    {
        const juce::ScopedLock sl (lock);
        liveEstimate = {};
    }
}

void AnalysisWorker::loadFile (const juce::File& file)
{
    Job job;
    job.kind = Job::Kind::file;
    job.file = file;
    {
        const juce::ScopedLock sl (lock);
        job.id = latestJob.fetch_add (1) + 1;
        ref = {};
        ref.path = file.getFullPathName();
        status.phase = SourcePhase::Loading;
        status.progress = 0.0f;
        status.name = file.getFileName();
        status.error = source::LoadError::None;
        status.usedSavedCopy = false;
        ++status.version;
        pending = job;
    }
    notify();
}

void AnalysisWorker::loadCapture (const juce::File& file, const CaptureInfo& info)
{
    Job job;
    job.kind = Job::Kind::capture;
    job.file = file;
    job.capture = info;
    {
        const juce::ScopedLock sl (lock);
        job.id = latestJob.fetch_add (1) + 1;
        ref = {};
        ref.path = file.getFullPathName();
        ref.fromCapture = true;
        ref.bpm = info.bpm;
        ref.bars = info.bars;
        ref.timeSigNumerator = info.timeSigNumerator;
        ref.timeSigDenominator = info.timeSigDenominator;
        status.phase = SourcePhase::Loading;
        status.progress = 0.0f;
        status.name = file.getFileName();
        status.error = source::LoadError::None;
        status.usedSavedCopy = false;
        ++status.version;
        pending = job;
    }
    notify();
}

void AnalysisWorker::restore (const source::SourceRef& saved)
{
    {
        const juce::ScopedLock sl (lock);
        if (saved.sameSourceAs (ref) && status.phase != SourcePhase::Failed)
            return;

        latestJob.fetch_add (1);
        ref = saved;
        pending.reset();
        if (saved.isEmpty())
        {
            const auto version = status.version;
            status = {};
            status.version = version + 1;
            return;
        }

        Job job;
        job.kind = Job::Kind::restore;
        job.ref = saved;
        job.id = latestJob.load();
        status.phase = SourcePhase::Loading;
        status.progress = 0.0f;
        status.name = saved.path.isNotEmpty() ? juce::File (saved.path).getFileName() : juce::String ("Saved source");
        status.error = source::LoadError::None;
        status.usedSavedCopy = false;
        status.buffer = nullptr;
        ++status.version;
        pending = job;
    }
    notify();
}

void AnalysisWorker::clearSource()
{
    const juce::ScopedLock sl (lock);
    latestJob.fetch_add (1);
    pending.reset();
    ref = {};
    const auto version = status.version;
    status = {};
    status.version = version + 1;
}

source::SourceRef AnalysisWorker::getSourceRef() const
{
    const juce::ScopedLock sl (lock);
    return ref;
}

AnalysisWorker::SourceStatus AnalysisWorker::getSourceStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

KeyEstimate AnalysisWorker::getLiveEstimate() const
{
    const juce::ScopedLock sl (lock);
    return liveEstimate;
}

void AnalysisWorker::run()
{
    while (!threadShouldExit())
    {
        service();
        wait (kPollMs);
    }
}

void AnalysisWorker::service()
{
    std::optional<Job> job;
    {
        const juce::ScopedLock sl (lock);
        job.swap (pending);
    }
    if (job.has_value())
        runJob (*job);

    analyseLive();
}

void AnalysisWorker::publish (const std::function<void (SourceStatus&)>& change)
{
    const juce::ScopedLock sl (lock);
    change (status);
    ++status.version;
}

void AnalysisWorker::runJob (const Job& job)
{
    const auto id = job.id;
    auto lastShown = 0.0f;
    const source::Progress progress = [this, id, &lastShown] (float done)
    {
        if (!isCurrent (id) || threadShouldExit())
            return false;
        if (done - lastShown >= 0.02f || done >= 1.0f)
        {
            lastShown = done;
            const juce::ScopedLock sl (lock);
            if (isCurrent (id))
            {
                status.progress = done;
                ++status.version;
            }
        }
        return true;
    };

    source::LoadResult result;
    auto usedSavedCopy = false;
    std::shared_ptr<const juce::MemoryBlock> savedCopy;

    if (job.kind == Job::Kind::restore)
    {
        const juce::File file (job.ref.path);
        if (job.ref.path.isNotEmpty() && juce::File::isAbsolutePath (job.ref.path))
            result = source::loadFile (file, progress);
        else
            result.error = source::LoadError::NotFound;

        // The file is gone (or unreadable): the copy saved with the project.
        if (result.buffer == nullptr && result.error != source::LoadError::Cancelled && job.ref.embedded != nullptr)
        {
            auto fromCopy = source::loadEmbedded (*job.ref.embedded, progress);
            if (fromCopy.buffer != nullptr)
            {
                fromCopy.buffer->file = file;
                fromCopy.buffer->name = job.ref.path.isNotEmpty() ? file.getFileName() : juce::String ("Saved source");
                usedSavedCopy = true;
                savedCopy = job.ref.embedded;
                result = std::move (fromCopy);
            }
        }
    }
    else
    {
        result = source::loadFile (job.file, progress);
    }

    if (result.error == source::LoadError::Cancelled || !isCurrent (id))
        return;

    if (result.buffer == nullptr)
    {
        publish (
            [&] (SourceStatus& s)
            {
                if (!isCurrent (id))
                    return;
                s.phase = SourcePhase::Failed;
                s.error = result.error;
                s.buffer = nullptr;
            });
        return;
    }

    auto& buffer = *result.buffer;

    if (job.kind == Job::Kind::capture)
    {
        buffer.fromCapture = true;
        buffer.bpm = job.capture.bpm;
        buffer.bars = job.capture.bars;
        buffer.timeSigNumerator = job.capture.timeSigNumerator;
        buffer.timeSigDenominator = job.capture.timeSigDenominator;
    }
    else if (job.kind == Job::Kind::restore)
    {
        buffer.fromCapture = job.ref.fromCapture;
        buffer.bpm = job.ref.bpm;
        buffer.bars = job.ref.bars;
        buffer.timeSigNumerator = job.ref.timeSigNumerator;
        buffer.timeSigDenominator = job.ref.timeSigDenominator;
        buffer.gridOffsetSamples = job.ref.gridOffsetSamples;
    }

    source::finishSource (buffer);
    buffer.embedded = savedCopy != nullptr ? savedCopy : source::encodeEmbedded (buffer);

    const juce::ScopedLock sl (lock);
    if (!isCurrent (id))
        return;

    ref.embedded = buffer.embedded;
    ref.gridOffsetSamples = buffer.gridOffsetSamples;
    status.phase = SourcePhase::Ready;
    status.progress = 1.0f;
    status.name = buffer.name;
    status.error = source::LoadError::None;
    status.usedSavedCopy = usedSavedCopy;
    status.buffer = std::shared_ptr<const source::SourceBuffer> (result.buffer);
    ++status.version;
}

void AnalysisWorker::analyseLive()
{
    // A change of sample rate, or Auto Detect switched off and on: start over.
    const auto generation = tap.getGeneration();
    if (generation != tapGeneration || liveReset.exchange (false))
    {
        tapGeneration = generation;
        detector.prepare (tap.getSampleRate());
        detector.setMemorySeconds (kLiveMemorySeconds);
    }

    auto heard = false;
    while (true)
    {
        const auto n = tap.read (scratch.data(), kReadChunk);
        if (n <= 0)
            break;
        if (liveOn.load())
            detector.push (scratch.data(), n);
        heard = true;
    }

    if (heard && liveOn.load())
    {
        const auto estimate = detector.getEstimate();
        const juce::ScopedLock sl (lock);
        liveEstimate = estimate;
    }
}

} // namespace thunder::analysis
