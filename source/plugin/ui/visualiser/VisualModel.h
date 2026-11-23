#pragma once

#include "Lightning.h"
#include "MeterModel.h"
#include "SpectrumAnalyser.h"
#include "WaveformHistory.h"

#include "core/params/Enums.h"
#include "plugin/bridge/UiBridge.h"

#include <array>
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

    // [MSG] The spectrum's FFT is smaller at Eco and larger at High (FR-CORE-013).
    void setQuality (params::Quality quality) noexcept
    {
        spectrumDetail = quality == params::Quality::Eco ? -1 : (quality == params::Quality::High ? 1 : 0);
    }
    std::uint32_t getWanted() const noexcept { return wanted; }

    struct Changes
    {
        bool waveform = false;
        bool spectrum = false;
        bool meters = false;
        bool grains = false;
        bool modules = false; // the module displays' data (any frame arrived)
        bool strike = false;  // a layer transient: strike a bolt (UI.md §12.3)
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

    // The module panels' live displays (UI.md §11): what the modules did in
    // the newest frame, Duck's largest reduction in the last update, and
    // Static's movement over the last kTraceSeconds.
    static constexpr int kTracePoints = 200;
    static constexpr double kTraceSeconds = 2.0;
    static constexpr int kStaticTargets = 5;
    const visual::ModuleActivity& getActivity() const noexcept { return activity; }
    float getDuckReductionDb() const noexcept { return duckReductionDb; }
    // Static's value for `target` at trace point `index` (0 oldest … kTracePoints − 1 newest).
    float getStaticTrace (int target, int index) const noexcept;

    // Lightning (UI.md §12.3): the transient detector, fed every frame. While
    // the display is frozen nothing strikes.
    const lightning::StrikeDetector& getStrikes() const noexcept { return strikes; }
    void setStrikeSensitivity (float s) noexcept { strikes.setSensitivity (s); }

    // How long the song position has stood still (the waveform dims after 2 s).
    double getSecondsStopped() const noexcept { return secondsStopped; }
    double getSampleRate() const noexcept { return bridge.getSampleRate(); }

private:
    UiBridge& bridge;
    std::uint32_t wanted = 0;
    WaveformHistory history;
    MeterModel meters;
    SpectrumAnalyser spectrum;
    int spectrumDetail = 0;
    visual::GrainSnapshot grains;
    std::vector<visual::WaveFrame> frameScratch;
    std::vector<visual::SpectrumSample> spectrumScratch;
    bool frozen = false;
    double secondsStopped = 1.0e9; // nothing has moved yet

    void addActivity (const visual::WaveFrame& frame, double frameSeconds) noexcept;
    lightning::StrikeDetector strikes;
    visual::ModuleActivity activity;
    float duckReductionDb = 0.0f;
    float pendingDuckDb = 0.0f; // the largest in the frames read so far this update
    std::array<std::array<float, kTracePoints>, kStaticTargets> staticTrace{};
    int traceHead = 0; // the next point written (the oldest)
    double traceClock = 0.0;
};

} // namespace thunder::ui
