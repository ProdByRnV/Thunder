#pragma once

#include "core/capture/CaptureStatus.h"
#include "core/capture/CaptureWriter.h"
#include "core/engine/ThunderEngine.h"
#include "core/params/ParamSnapshot.h"
#include "core/state/SessionState.h"

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

    // [MSG] Non-automatable per-session settings (PRD §9.12).
    state::SessionState getSessionState() const;
    void setSessionState (const state::SessionState& newState);

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
    void setSettingsFile (const juce::File& file) { settingsFile = file; }
    const juce::File& getSettingsFile() const noexcept { return settingsFile; }

private:
    juce::AudioProcessorValueTreeState apvts;
    params::ParamRefs parameterRefs{};
    params::ParamSnapshot snapshot = params::ParamSnapshot::defaults();

    ThunderEngine engine;
    capture::CaptureWriter captureWriter{engine.getCapture()}; // after the engine: destroyed first
    juce::File settingsFile;

    mutable juce::CriticalSection sessionLock; // never taken on the audio thread
    state::SessionState session;

    std::atomic<bool> loadedNewerState{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderProcessor)
};

} // namespace thunder
