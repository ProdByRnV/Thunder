#include "ThunderProcessor.h"

namespace thunder
{

ThunderProcessor::ThunderProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ThunderProcessor::prepareToPlay (double, int) {}

void ThunderProcessor::releaseResources() {}

bool ThunderProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo out; stereo or mono in (mono input is treated as dual-mono).
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void ThunderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    // Mono input: copy the single channel to the right output so the signal
    // stays centred instead of playing only on the left.
    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (auto ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

juce::AudioProcessorEditor* ThunderProcessor::createEditor()
{
    return nullptr;
}

bool ThunderProcessor::hasEditor() const
{
    return false;
}

const juce::String ThunderProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ThunderProcessor::acceptsMidi() const
{
    return false;
}

bool ThunderProcessor::producesMidi() const
{
    return false;
}

bool ThunderProcessor::isMidiEffect() const
{
    return false;
}

double ThunderProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ThunderProcessor::getNumPrograms()
{
    // Some hosts misbehave when a plugin reports zero programs.
    return 1;
}

int ThunderProcessor::getCurrentProgram()
{
    return 0;
}

void ThunderProcessor::setCurrentProgram (int) {}

const juce::String ThunderProcessor::getProgramName (int)
{
    return {};
}

void ThunderProcessor::changeProgramName (int, const juce::String&) {}

void ThunderProcessor::getStateInformation (juce::MemoryBlock&) {}

void ThunderProcessor::setStateInformation (const void*, int) {}

} // namespace thunder

// Entry point used by every plugin format wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new thunder::ThunderProcessor();
}
