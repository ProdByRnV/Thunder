#pragma once

#include "bridge/UiBridge.h"
#include "core/analysis/AnalysisWorker.h"
#include "core/capture/CaptureStatus.h"
#include "core/capture/CaptureTray.h"
#include "core/capture/CaptureWriter.h"
#include "core/engine/ThunderEngine.h"
#include "core/params/ParamSnapshot.h"
#include "core/perform/SoundB.h"
#include "core/state/SessionState.h"
#include "presets/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace thunder
{

// Host-facing audio processor: adapts the host (buses, play head, parameters,
// saved state) to ThunderEngine, which does the processing.
class ThunderProcessor final
    : public juce::AudioProcessor
    , private juce::AsyncUpdater
{
public:
    ThunderProcessor();
    ~ThunderProcessor() override;

    // [NRT] Called by the host before playback starts or when settings change.
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    // [RT] Host reset / transport jump: clears the engine's state.
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // [RT] Audio thread.
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // The host's bypass: Thunder keeps running and fades to its input, delayed
    // by its latency, so the track stays in time and switching never clicks.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    // Thunder is an audio effect and uses no MIDI. JUCE requires every
    // processor to answer these two; both return false.
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    // [NRT] Saved with the host session; see core/state/StateManager.h.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    // [MSG] Non-automatable per-session settings (PRD §9.12). A change of Quality
    // re-prepares the engine (see setQuality).
    state::SessionState getSessionState() const;
    void setSessionState (const state::SessionState& newState);

    // [MSG] Quality (PRD FR-CORE-013): saved with the session. Changing it while
    // prepared re-prepares the engine with audio processing suspended (it may
    // allocate, and High changes the latency, which the host is told about).
    void setQuality (params::Quality quality);
    params::Quality getQuality() const { return getSessionState().quality; }

    // True if the last state loaded was saved by a newer Thunder. The editor
    // shows a warning for it (from v0.5).
    bool lastLoadWasFromNewerVersion() const noexcept { return loadedNewerState.load(); }

    const ThunderEngine& getEngine() const noexcept { return engine; }

    // [MSG] Key detection and the source (PRD FR-KEY-002/003, FR-INPUT-*).
    // Auto Detect is the session's keyAutoDetect.
    analysis::AnalysisWorker& getAnalysisWorker() noexcept { return analysisWorker; }
    void loadSourceFile (const juce::File& file);

    // [MSG] Capture (PRD FR-CAPT-*). Arming uses the session's bars and source
    // and the global captures folder and format.
    void armCapture();
    void cancelCapture();
    capture::CaptureStatus getCaptureStatus() const;
    capture::CaptureWriter& getCaptureWriter() noexcept { return captureWriter; }

    // [MSG] Where captures are written: the user's setting, or the default.
    // `settingsFile` is the global settings file; tests point it elsewhere.
    juce::File getCapturesFolder() const;
    void setSettingsFile (const juce::File& file)
    {
        settingsFile = file;
        presetManager.setSettingsFile (file);
    }
    const juce::File& getSettingsFile() const noexcept { return settingsFile; }

    // [MSG] Presets (PRD FR-PRESET-*).
    PresetManager& getPresetManager() noexcept { return presetManager; }

    // [MSG] Morph's sound B (PRD FR-PLAY-002): set from the current sound or a
    // preset, cleared, saved with the project. The audio thread gets it as one
    // complete snapshot, swapped in without locks.
    void setSoundB (std::optional<perform::SoundB> b);
    std::optional<perform::SoundB> getSoundB() const;

    // [MSG] The values Morph is sending to the modules now (for the knobs'
    // second ring): nullopt unless Morph is between A and a B.
    std::optional<params::ParamSnapshot> getMorphedValues() const;

    // [MSG] What the performance controls (Morph, Storm, strikes, Riser,
    // Variation) sent to the modules in the latest block, when anything moved
    // them; nullopt when nothing did. False when no block has run since the last call.
    bool readPerformedValues (std::optional<params::ParamSnapshot>& out);

    // [any thread] The Riser (PRD FR-PLAY-004): RISE in the editor, and how far
    // the current rise is (0–1, or −1 when none runs).
    void triggerRiser() noexcept { engine.getPerformance().triggerRiser(); }
    float getRiserProgress() const noexcept { return engine.getPerformance().getRiserProgress(); }

    // [MSG] The capture tray (PRD FR-CAPT-011): the project's last captures,
    // newest first; files that are gone drop out when it is read.
    capture::CaptureTray getCaptureTray();
    void removeFromCaptureTray (const juce::File& file);

    // [MSG] The visualiser's data (ARCHITECTURE §6.4).
    UiBridge& getUiBridge() noexcept { return uiBridge; }

private:
    juce::AudioProcessorValueTreeState apvts;
    params::ParamRefs parameterRefs{};
    params::ParamSnapshot snapshot = params::ParamSnapshot::defaults();

    ThunderEngine engine;
    analysis::AnalysisWorker analysisWorker{engine.getAnalysisTap()}; // after the engine: destroyed first
    capture::CaptureWriter captureWriter{engine.getCapture()};        // after the worker it reports to
    UiBridge uiBridge{engine.getVisualTap()};
    juce::File settingsFile;

    mutable juce::CriticalSection sessionLock; // never taken on the audio thread
    state::SessionState session;

    PresetManager presetManager{
        apvts,
        {[this] { return getSessionState(); }, [this] (const state::SessionState& s) { setSessionState (s); }},
        settingsFile};

    void applyQuality (params::Quality quality);
    void handleAsyncUpdate() override; // a program change that arrived off the message thread
    void programLoaded (const juce::String& key);
    void loadProgramUnlessCurrent (int index); // [MSG]

    // [RT] One block, faded towards the delayed input while `bypass` is set.
    void process (juce::AudioBuffer<float>& buffer, bool bypass);
    void processPart (float* const* channels, int numSamples, const TransportState& transport, bool bypass) noexcept;
    void prepareBypass(); // [NRT] after the latency is known

    // The input, kept for bypass (a power-of-two ring with room for the latency and one part).
    static constexpr int kBypassPart = 4096;
    std::array<std::vector<float>, ThunderEngine::kNumChannels> bypassLine;
    int bypassMask = 0;
    int bypassWrite = 0;
    int bypassDelay = 0;
    float bypassGain = 0.0f; // 0 = Thunder, 1 = bypassed
    float bypassStep = 1.0f;

    mutable juce::CriticalSection soundBLock; // never taken on the audio thread
    std::optional<perform::SoundB> soundB;
    std::atomic<int> pendingProgram{-1};

    mutable juce::CriticalSection trayLock; // never taken on the audio thread
    capture::CaptureTray captureTray;

    double preparedSampleRate = 0.0; // 0 until prepareToPlay

    std::atomic<bool> loadedNewerState{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderProcessor)
};

} // namespace thunder
