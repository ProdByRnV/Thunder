#include "ThunderProcessor.h"

#include "ThunderEditor.h"
#include "core/host/TransportState.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"
#include "core/state/UserSettings.h"
#include "params/ParameterLayout.h"

namespace thunder
{

ThunderProcessor::ThunderProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, state::ids::parameters, createParameterLayout())
    , settingsFile (state::UserSettings::defaultFile())
{
    // Cache a pointer to every parameter's live value once, so the audio thread
    // never looks parameters up by name.
    for (const auto& def : params::kParamDefs)
    {
        const auto id = juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size()));
        parameterRefs[params::toIndex (def.param)] = apvts.getRawParameterValue (id);
        jassert (parameterRefs[params::toIndex (def.param)] != nullptr);
    }

    // A finished Input capture becomes the source (PRD FR-INPUT-003). Called on
    // the writer's thread; the worker takes requests from any thread.
    captureWriter.onCaptureFinished = [this] (const juce::File& file, const capture::CaptureStart& start)
    {
        if (start.source == capture::CaptureSource::Input)
            analysisWorker.loadCapture (file,
                                        {start.bpm, start.bars, start.timeSigNumerator, start.timeSigDenominator});
    };
}

void ThunderProcessor::prepareToPlay (double sampleRate, int)
{
    // The engine processes in fixed-size chunks, so the host's block size
    // (which some hosts exceed or vary anyway) doesn't size anything.
    preparedSampleRate = sampleRate;
    engine.prepare (sampleRate, getSessionState().quality);
    setLatencySamples (engine.getLatencySamples());

    analysisWorker.setLiveDetection (getSessionState().keyAutoDetect);
    analysisWorker.start();
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
    return new ThunderEditor (*this);
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
    auto current = getSessionState();
    current.presetModified = presetManager.isModified();
    const auto tree = state::createState (apvts.copyState(), current, THUNDER_VERSION_STRING,
                                          analysisWorker.getSourceRef().toValueTree());

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
    presetManager.rememberLoadedSound (loaded->session);
    loadedNewerState.store (loaded->fromNewerVersion);

    // The project's source loads in the background; a missing file is reported
    // by the editor, never here (PRD FR-INPUT-005).
    const auto source = source::SourceRef::fromValueTree (loaded->source);
    if (!source.isEmpty())
        analysisWorker.start();
    analysisWorker.restore (source);
}

state::SessionState ThunderProcessor::getSessionState() const
{
    const juce::ScopedLock lock (sessionLock);
    return session;
}

void ThunderProcessor::setSessionState (const state::SessionState& newState)
{
    params::Quality previous;
    {
        const juce::ScopedLock lock (sessionLock);
        previous = session.quality;
        session = newState;
    }

    analysisWorker.setLiveDetection (newState.keyAutoDetect);
    if (newState.quality != previous)
        applyQuality (newState.quality);
}

void ThunderProcessor::setQuality (params::Quality quality)
{
    auto s = getSessionState();
    s.quality = quality;
    setSessionState (s);
}

void ThunderProcessor::applyQuality (params::Quality quality)
{
    // Before the host prepares, prepareToPlay picks the session's Quality up.
    if (preparedSampleRate <= 0.0)
        return;

    // Re-preparing allocates and resets: never while processBlock runs.
    suspendProcessing (true);
    engine.prepare (preparedSampleRate, quality);
    setLatencySamples (engine.getLatencySamples()); // tells the host when it changed
    suspendProcessing (false);
}

juce::File ThunderProcessor::getCapturesFolder() const
{
    return state::UserSettings::load (settingsFile).getCapturesFolder();
}

void ThunderProcessor::armCapture()
{
    const auto current = getSessionState();
    const auto settings = state::UserSettings::load (settingsFile);

    // Init isn't a name worth putting in a file name.
    const auto preset = presetManager.getCurrent();
    captureWriter.setSettings (
        {settings.getCapturesFolder(), settings.captureFormat, preset.isInit() ? juce::String() : preset.name});

    auto& controller = engine.getCapture();
    controller.allocateQueue();
    controller.arm (current.captureBars, current.captureSource, current.captureTail, current.captureLoop);
    captureWriter.start();
}

void ThunderProcessor::cancelCapture()
{
    engine.getCapture().cancel();
}

void ThunderProcessor::loadSourceFile (const juce::File& file)
{
    analysisWorker.start();
    analysisWorker.loadFile (file);
}

capture::CaptureStatus ThunderProcessor::getCaptureStatus() const
{
    return capture::CaptureStatus::from (engine.getCapture(), captureWriter);
}

} // namespace thunder
