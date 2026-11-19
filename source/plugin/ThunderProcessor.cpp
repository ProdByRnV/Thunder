#include "ThunderProcessor.h"

#include "core/host/TransportState.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"
#include "params/ParameterLayout.h"

namespace thunder
{

ThunderProcessor::ThunderProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, state::ids::parameters, createParameterLayout())
{
    // Cache a pointer to every parameter's live value once, so the audio thread
    // never looks parameters up by name.
    for (const auto& def : params::kParamDefs)
    {
        const auto id = juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size()));
        parameterRefs[params::toIndex (def.param)] = apvts.getRawParameterValue (id);
        jassert (parameterRefs[params::toIndex (def.param)] != nullptr);
    }
}

void ThunderProcessor::prepareToPlay (double sampleRate, int)
{
    // The engine processes in fixed-size chunks, so the host's block size
    // (which some hosts exceed or vary anyway) doesn't size anything.
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatencySamples());
}

void ThunderProcessor::releaseResources() {}

void ThunderProcessor::reset()
{
    engine.reset();
}

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

    if (numSamples == 0 || numOut < ThunderEngine::kNumChannels ||
        buffer.getNumChannels() < ThunderEngine::kNumChannels)
        return;

    // Mono input: the engine is stereo, so copy the single channel to the right
    // (dual-mono) instead of leaving whatever the host had there.
    if (numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (auto ch = ThunderEngine::kNumChannels; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    snapshot.readFrom (parameterRefs);
    const auto transport = TransportState::fromPlayHead (getPlayHead());

    engine.process (buffer.getArrayOfWritePointers(), numSamples, transport, snapshot);
}

juce::AudioProcessorEditor* ThunderProcessor::createEditor()
{
    // Temporary: a generic list of every parameter, until Thunder's own UI (v0.5).
    return new juce::GenericAudioProcessorEditor (*this);
}

bool ThunderProcessor::hasEditor() const
{
    return true;
}

const juce::String ThunderProcessor::getName() const
{
    return "Thunder";
}

bool ThunderProcessor::acceptsMidi() const
{
    return false;
}

bool ThunderProcessor::producesMidi() const
{
    return false;
}

double ThunderProcessor::getTailLengthSeconds() const
{
    return engine.getTailSeconds();
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

void ThunderProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto tree = state::createState (apvts.copyState(), getSessionState(), THUNDER_VERSION_STRING);

    if (const auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void ThunderProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Anything unreadable leaves the current state untouched.
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto loaded = state::loadState (juce::ValueTree::fromXml (*xml));
    if (!loaded)
        return;

    apvts.replaceState (loaded->parameters);
    setSessionState (loaded->session);
    loadedNewerState.store (loaded->fromNewerVersion);
}

state::SessionState ThunderProcessor::getSessionState() const
{
    const juce::ScopedLock lock (sessionLock);
    return session;
}

void ThunderProcessor::setSessionState (const state::SessionState& newState)
{
    const juce::ScopedLock lock (sessionLock);
    session = newState;
}

} // namespace thunder
