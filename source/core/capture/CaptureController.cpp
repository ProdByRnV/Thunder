#include "CaptureController.h"

#include "core/music/KeyParams.h"

#include <algorithm>
#include <cmath>

namespace thunder::capture
{

namespace
{
constexpr int kValidBars[] = {1, 2, 4, 8, 16};

bool isValidBars (int bars) noexcept
{
    return std::find (std::begin (kValidBars), std::end (kValidBars), bars) != std::end (kValidBars);
}

// Space kept free so a capture can always be ended (End, Failed or Cancelled),
// even when the writer has fallen behind.
constexpr int kReservedBlocks = 1;
} // namespace

void CaptureController::prepare (double newSampleRate, int latencySamples) noexcept
{
    jassert (newSampleRate > 0.0 && latencySamples >= 0);

    // prepare() never runs alongside process(), so this thread may act as the producer.
    if (phase == Phase::Recording)
    {
        if (isInTail())
            endKeepingCapture();
        else
            finish (CaptureBlock::Kind::Failed, CaptureFailure::Restarted);
    }

    sampleRate = newSampleRate;
    latency = latencySamples;
    clock = 0;
    setPhase (Phase::Idle);
}

void CaptureController::allocateQueue()
{
    if (queueAllocated.load (std::memory_order_acquire))
        return;

    const auto blocks = static_cast<int> (std::ceil (kQueueSeconds * kQueueMaxSampleRate / CaptureBlock::kMaxFrames));
    queue.setCapacity (blocks + kReservedBlocks + 1);
    queueAllocated.store (true, std::memory_order_release);
}

void CaptureController::arm (int newBars, CaptureSource newSource, bool newTail, bool newLoop) noexcept
{
    jassert (isQueueAllocated());
    if (!isQueueAllocated())
        return;

    requestedBars.store (isValidBars (newBars) ? newBars : 4, std::memory_order_relaxed);
    requestedSource.store (newSource, std::memory_order_relaxed);
    requestedTail.store (newTail || newLoop, std::memory_order_relaxed);
    requestedLoop.store (newLoop, std::memory_order_relaxed);
    request.store (Request::Arm, std::memory_order_release);
}

void CaptureController::cancel() noexcept
{
    request.store (Request::Cancel, std::memory_order_release);
}

void CaptureController::handleRequests() noexcept
{
    const auto r = request.exchange (Request::None, std::memory_order_acquire);

    if (r == Request::Arm && phase != Phase::Recording)
    {
        bars = requestedBars.load (std::memory_order_relaxed);
        source = requestedSource.load (std::memory_order_relaxed);
        tail = source != CaptureSource::Input && requestedTail.load (std::memory_order_relaxed);
        loop = tail && requestedLoop.load (std::memory_order_relaxed);
        publishedBars.store (bars, std::memory_order_relaxed);
        progress.store (0.0f, std::memory_order_relaxed);
        setPhase (Phase::Armed);
    }
    else if (r == Request::Cancel)
    {
        // Stop during the tail ends it: the N bars are already recorded.
        if (phase == Phase::Recording && isInTail())
            endKeepingCapture();
        else if (phase == Phase::Recording)
            finish (CaptureBlock::Kind::Cancelled);
        else
            setPhase (Phase::Idle);
    }
}

void CaptureController::reset() noexcept
{
    if (phase == Phase::Recording && isInTail())
        endKeepingCapture();
    else if (phase == Phase::Recording)
        finish (CaptureBlock::Kind::Failed, CaptureFailure::Restarted);
}

void CaptureController::endKeepingCapture() noexcept
{
    if (flushPending())
        finish (CaptureBlock::Kind::End);
    else
        finish (CaptureBlock::Kind::Failed, CaptureFailure::TooSlow);
}

int CaptureController::findBarStart (const TransportState& transport, double rate, int numSamples) noexcept
{
    const auto samplesPerQuarter = transport.samplesPerQuarterNote (rate);
    const auto halfSample = 0.5 / samplesPerQuarter;

    // A bar that started less than half a sample ago starts on this chunk's
    // first sample; otherwise the next bar line is the one that counts.
    const auto sinceBar = transport.ppqPosition - transport.ppqLastBarStart;
    const auto barPpq =
        sinceBar <= halfSample ? transport.ppqLastBarStart : transport.ppqLastBarStart + transport.quarterNotesPerBar();

    const auto offset = std::max<long long> (0, std::llround ((barPpq - transport.ppqPosition) * samplesPerQuarter));
    return offset < numSamples ? static_cast<int> (offset) : -1;
}

void CaptureController::process (const float* const* mix, const float* const* layer, const float* const* input,
                                 int numSamples, const TransportState& transport,
                                 const params::ParamSnapshot& snapshot) noexcept
{
    if (phase == Phase::Armed)
    {
        waitingForPlay.store (!transport.isPlaying, std::memory_order_relaxed);

        // Room for the Start block, one audio block and the reserve; if the
        // writer is still busy with the last capture, try again at the next bar.
        if (transport.isPlaying && queue.getFreeSpace() >= 2 + kReservedBlocks)
            if (const auto offset = findBarStart (transport, sampleRate, numSamples); offset >= 0)
                beginRecording (transport, offset, snapshot);
    }

    if (phase == Phase::Recording)
    {
        if (endSample < 0)
        {
            if (!checkTransport (transport))
            {
                clock += numSamples;
                return;
            }

            // Does the input reach the end of the last bar in this chunk?
            const auto samplesPerQuarter = transport.samplesPerQuarterNote (sampleRate);
            const auto toEnd = std::max<long long> (0, std::llround ((beatsToRecord - beatsDone) * samplesPerQuarter));
            if (toEnd < numSamples)
                endSample = clock + toEnd + captureLatency;

            beatsDone += numSamples / samplesPerQuarter;
            expectedPpq = transport.ppqPosition + numSamples / samplesPerQuarter;
            hasExpectedPpq = true;
        }

        const auto* const* audio = source == CaptureSource::Layer   ? layer
                                   : source == CaptureSource::Input ? input
                                                                    : mix;
        jassert (audio != nullptr);
        if (audio != nullptr)
            collect (audio, numSamples);
    }

    clock += numSamples;
}

void CaptureController::beginRecording (const TransportState& transport, int offset,
                                        const params::ParamSnapshot& snapshot) noexcept
{
    const auto key = music::keyFromParams (snapshot);
    const auto samplesPerBar = transport.samplesPerBar (sampleRate);

    ++captureId;
    captureLatency = source == CaptureSource::Input ? 0 : latency;
    startSample = clock + offset + captureLatency;
    tailFrames = 0;
    quietFrames = 0;
    recordingTail.store (false, std::memory_order_relaxed);
    endSample = -1;
    // At this chunk's start the bar line is `offset` samples ahead.
    beatsDone = -offset / transport.samplesPerQuarterNote (sampleRate);
    beatsToRecord = bars * transport.quarterNotesPerBar();
    hasExpectedPpq = false;
    framesCaptured = 0;
    expectedFrames = std::max<std::int64_t> (1, std::llround (bars * samplesPerBar));

    CaptureBlock start;
    start.captureId = captureId;
    start.kind = CaptureBlock::Kind::Start;
    start.start.sampleRate = sampleRate;
    start.start.bpm = transport.bpm;
    start.start.timeSigNumerator = transport.timeSigNumerator;
    start.start.timeSigDenominator = transport.timeSigDenominator;
    start.start.bars = bars;
    start.start.source = source;
    start.start.keyRoot = key.root;
    start.start.keyScale = static_cast<int> (params::enumIndex (key.scale));
    start.start.expectedFrames = expectedFrames;
    start.start.tail = tail;
    start.start.loop = loop;
    queue.push (&start, 1);

    pending.captureId = captureId;
    pending.kind = CaptureBlock::Kind::Audio;
    pending.numFrames = 0;

    progress.store (0.0f, std::memory_order_relaxed);
    waitingForPlay.store (false, std::memory_order_relaxed);
    setPhase (Phase::Recording);
}

bool CaptureController::checkTransport (const TransportState& transport) noexcept
{
    if (!hasExpectedPpq)
        return true;

    if (!transport.isPlaying)
    {
        finish (CaptureBlock::Kind::Failed, CaptureFailure::TransportStopped);
        return false;
    }

    const auto jump = expectedPpq - transport.ppqPosition;
    if (std::abs (jump) <= kPositionTolerance)
        return true;

    // A loop wrapping around by whole bars keeps the bar grid and the audio
    // continuous: carry on.
    const auto barLength = transport.quarterNotesPerBar();
    if (std::abs (jump - std::round (jump / barLength) * barLength) <= kPositionTolerance)
        return true;

    finish (CaptureBlock::Kind::Failed, CaptureFailure::TransportJumped);
    return false;
}

void CaptureController::collect (const float* const* audio, int numSamples) noexcept
{
    const auto chunkEnd = clock + numSamples;
    const auto from = std::max (clock, startSample);
    auto to = endSample >= 0 && !tail ? std::min (chunkEnd, endSample) : chunkEnd;

    // The tail: from the end of the N bars until the audio has been quiet for
    // a while, or for at most kMaxTailSeconds.
    auto tailDone = false;
    if (tail && endSample >= 0 && chunkEnd > endSample)
    {
        recordingTail.store (true, std::memory_order_relaxed);
        const auto quietNeeded = static_cast<std::int64_t> (kTailQuietSeconds * sampleRate);
        const auto maxTail = static_cast<std::int64_t> (kMaxTailSeconds * sampleRate);
        for (auto position = std::max (from, endSample); position < chunkEnd; ++position)
        {
            const auto index = static_cast<int> (position - clock);
            const auto loud =
                std::abs (audio[0][index]) > kTailQuietLevel || std::abs (audio[1][index]) > kTailQuietLevel;
            quietFrames = loud ? 0 : quietFrames + 1;
            if (++tailFrames >= maxTail || quietFrames >= quietNeeded)
            {
                to = position + 1;
                tailDone = true;
                break;
            }
        }
    }

    for (auto position = from; position < to;)
    {
        const auto index = static_cast<int> (position - clock);
        const auto count =
            static_cast<int> (std::min<std::int64_t> (to - position, CaptureBlock::kMaxFrames - pending.numFrames));

        for (int ch = 0; ch < 2; ++ch)
            std::copy_n (audio[ch] + index, count,
                         pending.audio[static_cast<std::size_t> (ch)].begin() + pending.numFrames);

        pending.numFrames += count;
        position += count;
        framesCaptured += count;

        if (pending.numFrames == CaptureBlock::kMaxFrames && !flushPending())
        {
            finish (CaptureBlock::Kind::Failed, CaptureFailure::TooSlow);
            return;
        }
    }

    progress.store (static_cast<float> (
                        std::min (1.0, static_cast<double> (framesCaptured) / static_cast<double> (expectedFrames))),
                    std::memory_order_relaxed);

    if (tailDone || (!tail && endSample >= 0 && chunkEnd >= endSample))
        endKeepingCapture();
}

bool CaptureController::flushPending() noexcept
{
    if (pending.numFrames == 0)
        return true;

    if (queue.getFreeSpace() < 1 + kReservedBlocks)
        return false;

    queue.push (&pending, 1);
    pending.numFrames = 0;
    return true;
}

void CaptureController::finish (CaptureBlock::Kind kind, CaptureFailure failure) noexcept
{
    // The reserve guarantees this fits.
    CaptureBlock last;
    last.captureId = captureId;
    last.kind = kind;
    last.failure = failure;
    if (kind == CaptureBlock::Kind::End)
        last.loopFrames = endSample >= 0 ? std::min (framesCaptured, endSample - startSample) : framesCaptured;
    queue.push (&last, 1);

    pending.numFrames = 0;
    recordingTail.store (false, std::memory_order_relaxed);
    if (kind == CaptureBlock::Kind::End)
        progress.store (1.0f, std::memory_order_relaxed);
    setPhase (Phase::Idle);
}

void CaptureController::setPhase (Phase newPhase) noexcept
{
    phase = newPhase;
    publishedPhase.store (newPhase, std::memory_order_release);
    if (newPhase != Phase::Armed)
        waitingForPlay.store (false, std::memory_order_relaxed);
}

} // namespace thunder::capture
