#pragma once

#include "core/visual/VisualTap.h"

#include <cstdint>

namespace thunder
{

// The one place the editor sees audio-side data (ARCHITECTURE §6.4): the
// visualiser's queues, read on the message thread. The editor never gets the
// engine or its modules; capture still goes through the processor's own calls.
class UiBridge
{
public:
    static constexpr std::uint32_t kFrames = visual::VisualTap::kFrames;
    static constexpr std::uint32_t kSpectrum = visual::VisualTap::kSpectrum;
    static constexpr std::uint32_t kGrains = visual::VisualTap::kGrains;

    explicit UiBridge (visual::VisualTap& visualTap) noexcept
        : tap (visualTap)
    {
    }

    // [MSG] What the editor shows; 0 when it is closed or hidden (PRD UI-VIS-006).
    void setWanted (std::uint32_t parts) { tap.setWanted (parts); }
    std::uint32_t getWanted() const noexcept { return tap.getWanted(); }

    // [MSG] Drain the queues.
    int readFrames (visual::WaveFrame* destination, int maxFrames) noexcept
    {
        return tap.readFrames (destination, maxFrames);
    }
    int readSpectrum (visual::SpectrumSample* destination, int maxSamples) noexcept
    {
        return tap.readSpectrum (destination, maxSamples);
    }
    bool readGrains (visual::GrainSnapshot& latest) noexcept { return tap.readGrains (latest); }

    double getSampleRate() const noexcept { return tap.getSampleRate(); }
    double getFrameSeconds() const noexcept { return tap.getFrameSeconds(); }

private:
    visual::VisualTap& tap;
};

} // namespace thunder
