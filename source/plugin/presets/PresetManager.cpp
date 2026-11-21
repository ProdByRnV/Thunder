#include "PresetManager.h"

#include "core/state/UserSettings.h"

#include <cmath>

namespace thunder
{

namespace
{
juce::String toJuce (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}

// Key, Scale and Output Mode describe the song and how Thunder sits in it, not
// the sound (PRD FR-PRESET-006).
bool isSessionParameter (params::ParamId id) noexcept
{
    return id == params::ParamId::keyRoot || id == params::ParamId::keyScale || id == params::ParamId::outputMode;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, SessionAccess access, juce::File settings)
    : apvts (state)
    , session (std::move (access))
    , settingsFile (std::move (settings))
    , userFolder (presets::defaultUserFolder())
{
    // A fresh instance is Init.
    snapshot = currentValues();
}

void PresetManager::setUserFolder (const juce::File& folder)
{
    userFolder = folder;
    listingValid = false;
}

const presets::Listing& PresetManager::getListing()
{
    if (!listingValid)
        refresh();
    return listing;
}

void PresetManager::refresh()
{
    listing = presets::listPresets (userFolder);
    listingValid = true;
}

PresetManager::Current PresetManager::getCurrent() const
{
    const auto s = session.get();

    Current c;
    c.key = s.presetKey;
    c.name = c.isInit() && s.presetName.isEmpty() ? juce::String ("Init") : s.presetName;
    c.modified = isModified();
    return c;
}

PresetManager::Values PresetManager::currentValues() const
{
    Values v{};
    for (const auto& def : params::kParamDefs)
        if (const auto* raw = apvts.getRawParameterValue (toJuce (def.id)))
            v[params::toIndex (def.param)] = raw->load();
    return v;
}

void PresetManager::apply (const presets::Preset& preset)
{
    const auto preserve = getPreserveSession();

    for (const auto& def : params::kParamDefs)
    {
        if (preserve && isSessionParameter (def.param))
            continue;

        if (auto* parameter = apvts.getParameter (toJuce (def.id)))
        {
            const auto target = parameter->convertTo0to1 (preset.valueOr (def.param));
            if (std::abs (parameter->getValue() - target) < 1.0e-7f)
                continue;

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (target);
            parameter->endChangeGesture();
        }
    }
}

void PresetManager::becomeLoaded (const presets::Metadata& meta, const juce::String& key)
{
    loadedMeta = meta;

    {
        const juce::ScopedLock sl (snapshotLock);
        snapshot = currentValues();
        snapshotValid = true;
    }

    auto s = session.get();
    s.presetName = meta.name;
    s.presetKey = key;
    s.presetModified = false;
    session.set (s);
}

juce::String PresetManager::load (const presets::Entry& entry)
{
    if (entry.isFactory)
    {
        for (const auto& factory : presets::factoryPresets())
            if (factory.preset.meta.name == entry.name)
            {
                apply (factory.preset);
                becomeLoaded (factory.preset.meta, entry.key);
                return {};
            }

        return presets::describe (presets::ReadResult::unreadable);
    }

    return loadFile (entry.file);
}

juce::String PresetManager::loadInit()
{
    const auto init = presets::initPreset();
    apply (init);
    becomeLoaded (init.meta, presets::kInitKey);
    return {};
}

juce::String PresetManager::loadFile (const juce::File& file)
{
    presets::Preset preset;
    const auto report = presets::readFile (file, preset);
    if (!report.ok())
        return presets::describe (report.result); // nothing changed

    if (preset.meta.name.isEmpty())
        preset.meta.name = file.getFileNameWithoutExtension();

    apply (preset);
    becomeLoaded (preset.meta, presets::userKey (file));
    return {};
}

juce::String PresetManager::step (int direction)
{
    const auto& entries = getListing().entries;
    if (entries.empty())
        return {};

    const auto key = session.get().presetKey;
    const auto count = static_cast<int> (entries.size());

    int index = -1;
    for (int i = 0; i < count; ++i)
        if (entries[static_cast<std::size_t> (i)].key == key)
            index = i;

    if (index < 0)
        index = direction > 0 ? 0 : count - 1;
    else
        index = ((index + direction) % count + count) % count;

    return load (entries[static_cast<std::size_t> (index)]);
}

std::optional<juce::File> PresetManager::fileForName (const juce::String& name) const
{
    return presets::presetFileFor (userFolder, presets::sanitise ({name, {}, {}, {}, {}}).name);
}

bool PresetManager::wouldReplace (const juce::String& name) const
{
    const auto file = fileForName (name);
    return file && file->exists();
}

presets::SaveResult PresetManager::saveAs (const presets::Metadata& meta)
{
    const auto clean = presets::sanitise (meta);
    const auto file = fileForName (clean.name);
    if (!file)
        return presets::SaveResult::invalidName;

    const auto result =
        presets::writePresetFile (*file, presets::Preset::complete (clean, currentValues()), THUNDER_VERSION_STRING);
    if (result != presets::SaveResult::ok)
        return result;

    auto settings = state::UserSettings::load (settingsFile);
    if (clean.author.isNotEmpty() && settings.presetAuthor != clean.author)
    {
        settings.presetAuthor = clean.author;
        settings.save (settingsFile);
    }

    becomeLoaded (clean, presets::userKey (*file));
    listingValid = false;
    return presets::SaveResult::ok;
}

presets::SaveResult PresetManager::save()
{
    const auto current = getCurrent();
    if (!current.isUser())
        return presets::SaveResult::isFactoryPreset;

    const juce::File file (current.key.fromFirstOccurrenceOf ("user:", false, false));
    if (!file.isAChildOf (userFolder))
        return presets::SaveResult::outsideLibrary;

    auto meta = loadedMeta;
    if (meta.name.isEmpty())
        meta.name = current.name;

    const auto result =
        presets::writePresetFile (file, presets::Preset::complete (meta, currentValues()), THUNDER_VERSION_STRING);
    if (result == presets::SaveResult::ok)
        becomeLoaded (meta, current.key);
    return result;
}

bool PresetManager::getPreserveSession() const
{
    return state::UserSettings::load (settingsFile).preservePresetSession;
}

void PresetManager::setPreserveSession (bool preserve)
{
    auto settings = state::UserSettings::load (settingsFile);
    settings.preservePresetSession = preserve;
    settings.save (settingsFile);
}

juce::String PresetManager::getRememberedAuthor() const
{
    return state::UserSettings::load (settingsFile).presetAuthor;
}

bool PresetManager::isModified() const
{
    const auto now = currentValues();
    const juce::ScopedLock sl (snapshotLock);

    if (!snapshotValid)
        return true;

    for (const auto& def : params::kParamDefs)
    {
        // Key, Scale and Output Mode aren't part of a preset's sound.
        if (isSessionParameter (def.param))
            continue;

        const auto i = params::toIndex (def.param);
        if (std::abs (now[i] - snapshot[i]) > 1.0e-4f * (def.max - def.min))
            return true;
    }

    return false;
}

void PresetManager::rememberLoadedSound (const state::SessionState& restored)
{
    const juce::ScopedLock sl (snapshotLock);
    snapshot = currentValues();
    snapshotValid = !restored.presetModified;

    // The preset's other details (author, category, description), so a later
    // Save keeps them.
    loadedMeta = {};
    loadedMeta.name = restored.presetName;

    if (restored.presetKey.startsWith ("factory:"))
    {
        for (const auto& factory : presets::factoryPresets())
            if (presets::factoryKey (factory.preset.meta.name) == restored.presetKey)
                loadedMeta = factory.preset.meta;
    }
    else if (restored.presetKey.startsWith ("user:"))
    {
        presets::Preset preset;
        if (presets::readFile (juce::File (restored.presetKey.fromFirstOccurrenceOf ("user:", false, false)), preset)
                .ok())
        {
            loadedMeta = preset.meta;
            loadedMeta.name = restored.presetName;
        }
    }
}

} // namespace thunder
