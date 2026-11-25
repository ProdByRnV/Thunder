#include "ThunderProcessor.h"

#include "ThunderEditor.h"
#include "core/host/TransportState.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"
#include "core/state/UserSettings.h"
#include "core/util/Crossfade.h"
#include "params/ParameterLayout.h"

#include <algorithm>
#include <cmath>

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

        // Every finished capture goes into the tray (PRD FR-CAPT-011).
        const juce::ScopedLock lock (trayLock);
        captureTray.add ({file, start.bars, start.source, start.tail, start.loop});
    };

    // The host's preset menu follows presets loaded in Thunder (FR-PRESET-015).
    presetManager.onLoaded = [this] (const juce::String& key) { programLoaded (key); };
}

ThunderProcessor::~ThunderProcessor()
{
    cancelPendingUpdate();
}

void ThunderProcessor::prepareToPlay (double sampleRate, int)
{
    // The engine processes in fixed-size chunks, so the host's block size
    // (which some hosts exceed or vary anyway) doesn't size anything.
    preparedSampleRate = sampleRate;
    engine.prepare (sampleRate, getSessionState().quality);
    setLatencySamples (engine.getLatencySamples());
    prepareBypass();

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
    process (buffer, false);
}

void ThunderProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void ThunderProcessor::prepareBypass()
{
    bypassDelay = engine.getLatencySamples();
    const auto size = static_cast<int> (juce::nextPowerOfTwo (bypassDelay + kBypassPart));
    for (auto& line : bypassLine)
        line.assign (static_cast<std::size_t> (size), 0.0f);
    bypassMask = size - 1;
    bypassWrite = 0;
    bypassStep = static_cast<float> (1.0 / std::max (1.0, util::kBypassFadeSeconds * preparedSampleRate));
}

void ThunderProcessor::process (juce::AudioBuffer<float>& buffer, bool bypass)
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
    auto transport = TransportState::fromPlayHead (getPlayHead());

    // In parts the bypass line has room for (a multiple of the engine's chunk,
    // so the output is the same as one call).
    auto* const* channels = buffer.getArrayOfWritePointers();
    for (int offset = 0; offset < numSamples;)
    {
        const auto part = std::min (kBypassPart, numSamples - offset);
        float* partChannels[ThunderEngine::kNumChannels] = {channels[0] + offset, channels[1] + offset};
        processPart (partChannels, part, transport, bypass);
        offset += part;
        if (offset < numSamples)
            transport = transport.advancedBy (part, preparedSampleRate);
    }
}

void ThunderProcessor::processPart (float* const* channels, int numSamples, const TransportState& transport,
                                    bool bypass) noexcept
{
    // The input goes into the line first, so bypass can start from it at any time.
    const auto start = bypassWrite;
    const auto haveLine = bypassMask > 0;
    if (haveLine)
    {
        for (int ch = 0; ch < ThunderEngine::kNumChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                bypassLine[static_cast<std::size_t> (ch)][static_cast<std::size_t> ((start + i) & bypassMask)] =
                    channels[ch][i];
        bypassWrite = (start + numSamples) & bypassMask;
    }

    engine.process (channels, numSamples, transport, snapshot);

    const auto target = bypass ? 1.0f : 0.0f;
    if (!haveLine || (juce::exactlyEqual (bypassGain, 0.0f) && !bypass))
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        bypassGain = bypassGain < target ? std::min (target, bypassGain + bypassStep)
                                         : std::max (target, bypassGain - bypassStep);
        const auto read = static_cast<std::size_t> ((start + i - bypassDelay) & bypassMask);
        for (int ch = 0; ch < ThunderEngine::kNumChannels; ++ch)
        {
            const auto delayed = bypassLine[static_cast<std::size_t> (ch)][read];
            channels[ch][i] += (delayed - channels[ch][i]) * bypassGain;
        }
    }
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

// Host programs (PRD FR-PRESET-015): Init and the factory presets, in the DAW's
// own preset menu, loaded through the same path as the browser.
int ThunderProcessor::getNumPrograms()
{
    return PresetManager::numPrograms();
}

int ThunderProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, getSessionState().presetProgram);
}

void ThunderProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    // Loading writes parameters and the session: on the message thread only.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        loadProgramUnlessCurrent (index);
        return;
    }

    pendingProgram.store (index);
    triggerAsyncUpdate();
}

void ThunderProcessor::handleAsyncUpdate()
{
    const auto index = pendingProgram.exchange (-1);
    if (index >= 0 && index < getNumPrograms())
        loadProgramUnlessCurrent (index);
}

void ThunderProcessor::loadProgramUnlessCurrent (int index)
{
    // A host sending the program the project is on (many do when a project
    // reopens) must never load over its own, perhaps edited, sound. Once
    // another preset is loaded in Thunder, choosing the program loads it.
    const auto onIt = PresetManager::programForKey (presetManager.getCurrent().key) == index;
    if (index == getSessionState().presetProgram && onIt)
        return;

    presetManager.loadProgram (index);
}

void ThunderProcessor::programLoaded (const juce::String& key)
{
    // Init and factory presets are programs: the host's menu shows them. Other
    // presets leave the host's program as it was.
    const auto program = PresetManager::programForKey (key);
    if (program < 0)
        return;

    auto s = getSessionState();
    if (s.presetProgram == program)
        return;

    s.presetProgram = program;
    setSessionState (s);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String ThunderProcessor::getProgramName (int index)
{
    return PresetManager::programName (index);
}

void ThunderProcessor::changeProgramName (int, const juce::String&) {}

void ThunderProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto current = getSessionState();
    current.presetModified = presetManager.isModified();
    const auto b = getSoundB();
    juce::ValueTree tray;
    {
        const juce::ScopedLock lock (trayLock);
        tray = captureTray.toValueTree();
    }
    const auto tree = state::createState (apvts.copyState(), current, THUNDER_VERSION_STRING,
                                          analysisWorker.getSourceRef().toValueTree(),
                                          b ? b->toValueTree (THUNDER_VERSION_STRING) : juce::ValueTree(), tray);

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

    // A host can leave a switch or choice holding an in-between raw value (a
    // switch at 0.67 reads as on). The parameter tree then counts a saved "on"
    // as no change and skips it, so the raw value would outlive the restore and
    // the host would read it back. Every stepped parameter ends on a legal value.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            ranged != nullptr && ranged->isDiscrete())
        {
            const auto raw = ranged->getValue();
            const auto legal = ranged->convertTo0to1 (ranged->convertFrom0to1 (raw));
            if (std::abs (legal - raw) > 1.0e-6f)
            {
                ranged->setValue (legal);
                ranged->sendValueChangedMessageToListeners (legal);
            }
        }

    setSessionState (loaded->session);
    presetManager.rememberLoadedSound (loaded->session);
    loadedNewerState.store (loaded->fromNewerVersion);

    // Morph's sound B is the project's: restored with it, or none.
    setSoundB (perform::SoundB::fromValueTree (loaded->morphSoundB));

    // So is the capture tray.
    {
        const juce::ScopedLock lock (trayLock);
        captureTray = capture::CaptureTray::fromValueTree (loaded->captureTray);
    }

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
    prepareBypass();
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

void ThunderProcessor::setSoundB (std::optional<perform::SoundB> b)
{
    // One writer at a time: the UI and a host restoring state may both set it.
    const juce::ScopedLock lock (soundBLock);
    soundB = std::move (b);
    engine.getPerformance().setMorphSound (soundB ? soundB->toMorphSound() : perform::MorphSound{});
}

std::optional<perform::SoundB> ThunderProcessor::getSoundB() const
{
    const juce::ScopedLock lock (soundBLock);
    return soundB;
}

std::optional<params::ParamSnapshot> ThunderProcessor::getMorphedValues() const
{
    params::ParamSnapshot a;
    a.readFrom (parameterRefs);
    const auto t = juce::jlimit (0.0f, 1.0f, a.get (params::ParamId::morphAmount) / 100.0f);

    const auto b = getSoundB();
    if (!b || t <= 0.0f)
        return std::nullopt;

    params::ParamSnapshot morphed;
    perform::applyMorph (a, b->toMorphSound(), t, morphed);
    return morphed;
}

bool ThunderProcessor::readPerformedValues (std::optional<params::ParamSnapshot>& out)
{
    perform::PerformedValues v;
    if (!engine.getPerformance().readPublished (v))
        return false;
    out = v.moved ? std::optional<params::ParamSnapshot> (v.values) : std::nullopt;
    return true;
}

capture::CaptureTray ThunderProcessor::getCaptureTray()
{
    // Checking a handful of files is cheap; the editor asks about once a second.
    const juce::ScopedLock lock (trayLock);
    captureTray.dropMissing();
    return captureTray;
}

void ThunderProcessor::removeFromCaptureTray (const juce::File& file)
{
    const juce::ScopedLock lock (trayLock);
    captureTray.remove (file);
}

capture::CaptureStatus ThunderProcessor::getCaptureStatus() const
{
    return capture::CaptureStatus::from (engine.getCapture(), captureWriter);
}

} // namespace thunder
