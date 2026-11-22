#include "VisualModel.h"

#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

namespace
{
constexpr int kSpectrumChunk = 4096;
} // namespace

VisualModel::VisualModel (UiBridge& b)
    : bridge (b)
{
    frameScratch.resize (static_cast<std::size_t> (visual::VisualTap::kFrameCapacity));
    spectrumScratch.resize (static_cast<std::size_t> (kSpectrumChunk));
}

void VisualModel::setWanted (std::uint32_t parts)
{
    if (parts == wanted)
        return;

    // Grains that are no longer read go away rather than freezing on screen.
    if ((parts & UiBridge::kGrains) == 0)
        grains.count = 0;

    wanted = parts;
    bridge.setWanted (parts);
}

VisualModel::Changes VisualModel::update (double seconds)
{
    Changes changes;

    history.prepare (bridge.getFrameSeconds());
    meters.prepare (bridge.getFrameSeconds());
    spectrum.prepare (bridge.getSampleRate(), spectrumDetail);

    // Frames: every one feeds the meters; those whose song position moves feed
    // the waveform, unless the display is frozen.
    auto moved = false;
    for (;;)
    {
        const auto count = bridge.readFrames (frameScratch.data(), static_cast<int> (frameScratch.size()));
        for (int i = 0; i < count; ++i)
        {
            const auto& frame = frameScratch[static_cast<std::size_t> (i)];
            meters.add (frame);
            addActivity (frame, bridge.getFrameSeconds());
            changes.modules = true;
            if (frame.isMoving())
            {
                moved = true;
                if (!frozen)
                {
                    history.add (frame);
                    changes.waveform = true;
                }
            }
        }
        if (count < static_cast<int> (frameScratch.size()))
            break;
    }

    if (changes.modules)
        duckReductionDb = pendingDuckDb;
    pendingDuckDb = 0.0f;

    const auto wasDimmed = secondsStopped > theme::visualiser::dimAfterSeconds;
    secondsStopped = moved ? 0.0 : secondsStopped + seconds;
    if (wasDimmed != (secondsStopped > theme::visualiser::dimAfterSeconds))
        changes.waveform = true;

    changes.meters = meters.advance (seconds);

    // Spectrum samples.
    for (;;)
    {
        const auto count = bridge.readSpectrum (spectrumScratch.data(), kSpectrumChunk);
        if (!frozen)
            spectrum.push (spectrumScratch.data(), count);
        if (count < kSpectrumChunk)
            break;
    }
    changes.spectrum = !frozen && spectrum.update (seconds);

    // Grains: only the newest snapshot matters.
    visual::GrainSnapshot latest;
    if (bridge.readGrains (latest) && !frozen)
    {
        changes.grains = latest.count > 0 || grains.count > 0;
        grains = latest;
    }

    return changes;
}

void VisualModel::addActivity (const visual::WaveFrame& frame, double frameSeconds) noexcept
{
    activity = frame.activity;
    pendingDuckDb = std::max (pendingDuckDb, frame.activity.duckReductionDb);

    // One trace point per kTraceSeconds / kTracePoints of audio.
    traceClock += frameSeconds;
    constexpr auto step = kTraceSeconds / kTracePoints;
    while (traceClock >= step)
    {
        traceClock -= step;
        for (std::size_t t = 0; t < staticTrace.size(); ++t)
            staticTrace[t][static_cast<std::size_t> (traceHead)] = activity.staticValues[t];
        traceHead = (traceHead + 1) % kTracePoints;
    }
}

float VisualModel::getStaticTrace (int target, int index) const noexcept
{
    if (target < 0 || target >= kStaticTargets || index < 0 || index >= kTracePoints)
        return 0.0f;
    return staticTrace[static_cast<std::size_t> (target)]
                      [static_cast<std::size_t> ((traceHead + index) % kTracePoints)];
}

} // namespace thunder::ui
