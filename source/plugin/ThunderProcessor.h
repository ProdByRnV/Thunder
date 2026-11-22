#pragma once

#include "bridge/UiBridge.h"
#include "core/capture/CaptureStatus.h"
#include "core/capture/CaptureWriter.h"
#include "core/engine/ThunderEngine.h"
#include "core/params/ParamSnapshot.h"
#include "core/state/SessionState.h"
#include "presets/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace thunder
{

// Host-facing audio processor: adapts the host (buses, play head, parameters,
// saved state) to ThunderEngine, which does the processing.
class ThunderProcessor final : public juce::AudioProcessor
{
public:
    ThunderProcessor();
    ~ThunderProcessor() override = default;

    // [NRT] Called by the host before playback starts or when settings change.
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    // [RT] Host reset / transport jump: clears the engine's state.
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // [RT] Audio thread.
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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

    // [MSG] The visualiser's data (ARCHITECTURE §6.4).
    UiBridge& getUiBridge() noexcept { return uiBridge; }

private:
    juce::AudioProcessorValueTreeState apvts;
    params::ParamRefs parameterRefs{};
    params::ParamSnapshot snapshot = params::ParamSnapshot::defaults();

    ThunderEngine engine;
    capture::CaptureWriter captureWriter{engine.getCapture()}; // after the engine: destroyed first
    UiBridge uiBridge{engine.getVisualTap()};
    juce::File settingsFile;

    mutable juce::CriticalSection sessionLock; // never taken on the audio thread
    state::SessionState session;

    PresetManager presetManager{
        apvts,
        {[this] { return getSessionState(); }, [this] (const state::SessionState& s) { setSessionState (s); }},
        settingsFile};

    void applyQuality (params::Quality quality);

    double preparedSampleRate = 0.0; // 0 until prepareToPlay

    std::atomic<bool> loadedNewerState{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderProcessor)
};

} // namespace thunder
