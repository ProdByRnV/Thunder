#pragma once

#include "MeterModel.h"
#include "SpectrumAnalyser.h"
#include "WaveformHistory.h"

#include "plugin/bridge/UiBridge.h"

#include <vector>

namespace thunder::ui
{

// Everything the visualiser and the meters show, read from UiBridge once per
// frame by the editor's frame timer (ARCHITECTURE §4.1, §12.2). The views only
// read from here. Message thread only.
class VisualModel
{
public:
    explicit VisualModel (UiBridge& bridge);

    // [MSG] Which parts are on screen now (UiBridge::kFrames | kSpectrum |
    // kGrains); 0 when the editor is hidden. Parts not wanted cost the audio
    // thread nothing.
    void setWanted (std::uint32_t parts);
    std::uint32_t getWanted() const noexcept { return wanted; }

    struct Changes
    {
        bool waveform = false;
        bool spectrum = false;
        bool meters = false;
        bool grains = false;
    };

    // [MSG] Reads what has arrived; `seconds` have passed since the last call.
    Changes update (double seconds);

    // Freeze display (UI.md §10.1): the waveform, the spectrum and the grains
    // stop changing; the meters carry on.
    void setFrozen (bool shouldFreeze) noexcept { frozen = shouldFreeze; }
    bool isFrozen() const noexcept { return frozen; }

    const WaveformHistory& getHistory() const noexcept { return history; }
    MeterModel& getMeters() noexcept { return meters; }
    const MeterModel& getMeters() const noexcept { return meters; }
    const SpectrumAnalyser& getSpectrum() const noexcept { return spectrum; }
    const visual::GrainSnapshot& getGrains() const noexcept { return grains; }

    // How long the song position has stood still (the waveform dims after 2 s).
    double getSecondsStopped() const noexcept { return secondsStopped; }
    double getSampleRate() const noexcept { return bridge.getSampleRate(); }

private:
    UiBridge& bridge;
    std::uint32_t wanted = 0;
    WaveformHistory history;
    MeterModel meters;
    SpectrumAnalyser spectrum;
    visual::GrainSnapshot grains;
    std::vector<visual::WaveFrame> frameScratch;
    std::vector<visual::SpectrumSample> spectrumScratch;
    bool frozen = false;
    double secondsStopped = 1.0e9; // nothing has moved yet
};

} // namespace thunder::ui
