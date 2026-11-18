#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace thunder
{

// Host-facing audio processor.
//
// At this stage it is a clean stereo pass-through so the plugin can be built,
// scanned and loaded by hosts. Parameters, state, the engine and the editor are
// added on top of this class.
class ThunderProcessor final : public juce::AudioProcessor
{
public:
    ThunderProcessor();
    ~ThunderProcessor() override = default;

    // [NRT] Called by the host before playback starts or when settings change.
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // [RT] Audio thread.
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderProcessor)
};

} // namespace thunder
