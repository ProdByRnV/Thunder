#include "PresetManager.h"

#include "core/presets/FactoryOrder.h"
#include "core/state/UserSettings.h"

#include <cmath>
#include <mutex>

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

void setParameter (juce::AudioProcessorValueTreeState& apvts, const params::ParamDef& def, float value)
{
    if (auto* parameter = apvts.getParameter (toJuce (def.id)))
    {
        const auto target = parameter->convertTo0to1 (value);
        if (std::abs (parameter->getValue() - target) < 1.0e-7f)
            return;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (target);
        parameter->endChangeGesture();
    }
}
} // namespace

// ---- Scanner
// ---------------------------------------------------------------------------------------------------------

// One scan at a time on a thread of its own (ARCHITECTURE §9.5). Starting a new
// scan stops the one running; the result waits until the message thread takes it.
class PresetManager::Scanner final : private juce::Thread
{
public:
    Scanner()
        : juce::Thread ("Thunder preset scan")
    {
    }

    ~Scanner() override { stopThread (4000); }

    void start (const juce::File& folder)
    {
        stopThread (4000);
        {
            const std::lock_guard lock (mutex);
            scanFolder = folder;
            result.reset();
        }
        startThread (juce::Thread::Priority::background);
    }

    // Stops a running scan and forgets any result it had.
    void stop()
    {
        stopThread (4000);
        const std::lock_guard lock (mutex);
        result.reset();
    }

    bool running() const { return isThreadRunning(); }

    std::optional<presets::Listing> take()
    {
        const std::lock_guard lock (mutex);
        auto taken = std::move (result);
        result.reset();
        return taken;
    }

private:
    void run() override
    {
        juce::File folder;
        {
            const std::lock_guard lock (mutex);
            folder = scanFolder;
        }

        auto scanned = presets::scanLibrary (folder, [this] { return threadShouldExit(); });
        if (threadShouldExit())
            return;

        const std::lock_guard lock (mutex);
        result = std::move (scanned);
    }

    std::mutex mutex;
    juce::File scanFolder;
    std::optional<presets::Listing> result;
};

// ---- PresetManager
// ---------------------------------------------------------------------------------------------------

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, SessionAccess access, juce::File settings)
    : apvts (state)
    , session (std::move (access))
    , settingsFile (std::move (settings))
    , userFolder (presets::defaultUserFolder())
    , scanner (std::make_unique<Scanner>())
{
    // A fresh instance is Init.
    snapshot = currentValues();
}

PresetManager::~PresetManager() = default;

void PresetManager::setUserFolder (const juce::File& folder)
{
    userFolder = folder;
    if (scanRequested)
        rescan();
}

// ---- Library
// ---------------------------------------------------------------------------------------------------------

const presets::Listing& PresetManager::getListing()
{
    // Until a scan finishes: the factory presets, read when first needed (not
    // when a host merely creates Thunder to scan it).
    if (!listed)
        setListing (presets::scanLibrary ({}));

    if (!scanRequested)
        rescan();
    collectScan();
    return listing;
}

void PresetManager::rescan()
{
    scanRequested = true;
    favourites.reset();
    scanner->start (userFolder);
}

void PresetManager::rescanNow()
{
    scanRequested = true;
    favourites.reset();
    setListing (presets::scanLibrary (userFolder));
}

bool PresetManager::isScanning() const
{
    return scanner->running();
}

bool PresetManager::collectScan()
{
    if (auto finished = scanner->take())
    {
        setListing (std::move (*finished));
        return true;
    }
    return false;
}

bool PresetManager::waitForScan (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (scanner->running() && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (2);
    collectScan();
    return !scanner->running();
}

void PresetManager::setListing (presets::Listing newListing)
{
    listing = std::move (newListing);
    listed = true;
    ++listingVersion;
}

void PresetManager::libraryChanged()
{
    // A new scan replaces any running one, so the list catches up without the
    // message thread ever waiting on a large library.
    rescan();
}

std::vector<presets::Entry> PresetManager::getFilteredEntries()
{
    const auto& all = getListing();
    std::vector<presets::Entry> list;
    for (const auto index : presets::filtered (all, filter, sort, getFavourites()))
        list.push_back (all.entries[static_cast<std::size_t> (index)]);
    return list;
}

std::optional<presets::Entry> PresetManager::findEntry (const juce::String& key)
{
    for (const auto& e : getListing().entries)
        if (e.key == key)
            return e;
    return std::nullopt;
}

juce::StringArray PresetManager::getFavourites() const
{
    // Read once, then on every rescan (another instance may have changed them),
    // so the preset bar can ask on every frame without reading the file.
    if (!favourites)
        favourites = state::UserSettings::load (settingsFile).favouritePresets;
    return *favourites;
}

bool PresetManager::isFavourite (const juce::String& key) const
{
    return getFavourites().contains (key);
}

void PresetManager::setFavourite (const juce::String& key, bool favourite)
{
    if (key.isEmpty() || key == presets::kInitKey)
        return;

    // Read, change and write the file, so other instances' changes are kept.
    auto settings = state::UserSettings::load (settingsFile);
    if (favourite != settings.favouritePresets.contains (key))
    {
        if (favourite)
            settings.favouritePresets.add (key);
        else
            settings.favouritePresets.removeString (key);
        settings.save (settingsFile);
    }
    favourites = settings.favouritePresets;
}

// ---- Loading
// ---------------------------------------------------------------------------------------------------------

PresetManager::Current PresetManager::getCurrent() const
{
    const auto s = session.get();

    Current c;
    c.key = s.presetKey;
    c.name = c.isInit() && s.presetName.isEmpty() ? juce::String ("Init") : s.presetName;
    c.modified = isModified();
    return c;
}

juce::String PresetManager::getLoadedBank() const
{
    const auto key = session.get().presetKey;
    if (!key.startsWith ("user:"))
        return {};

    const juce::File file (key.fromFirstOccurrenceOf ("user:", false, false));
    const auto folder = file.getParentDirectory();
    if (folder == userFolder || !folder.isAChildOf (userFolder))
        return {};
    return folder.getRelativePathFrom (userFolder).replaceCharacter ('\\', '/');
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
        if (!presets::isPresetParameter (def.param) || (preserve && isSessionParameter (def.param)))
            continue;

        setParameter (apvts, def, preset.valueOr (def.param));
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

    if (onLoaded)
        onLoaded (key);
}

juce::String PresetManager::load (const presets::Entry& entry)
{
    if (entry.isFactory)
    {
        for (const auto& factory : presets::factoryPresets())
            if (presets::factoryKey (factory.preset.meta.name) == entry.key)
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
    const auto entries = getFilteredEntries();
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

std::optional<presets::Preset> PresetManager::readEntry (const presets::Entry& entry) const
{
    if (entry.isFactory)
    {
        for (const auto& factory : presets::factoryPresets())
            if (presets::factoryKey (factory.preset.meta.name) == entry.key)
                return factory.preset;
        return std::nullopt;
    }

    if (entry.key == presets::kInitKey)
        return presets::initPreset();

    presets::Preset preset;
    if (!presets::readFile (entry.file, preset).ok())
        return std::nullopt;
    if (preset.meta.name.isEmpty())
        preset.meta.name = entry.file.getFileNameWithoutExtension();
    return preset;
}

// ---- Revert
// ----------------------------------------------------------------------------------------------------------

PresetManager::RevertPoint PresetManager::makeRevertPoint() const
{
    RevertPoint point;
    point.values = currentValues();
    const auto s = session.get();
    point.presetName = s.presetName;
    point.presetKey = s.presetKey;
    point.modified = isModified();
    point.meta = loadedMeta;
    return point;
}

void PresetManager::revertTo (const RevertPoint& point)
{
    for (const auto& def : params::kParamDefs)
        if (presets::isPresetParameter (def.param))
            setParameter (apvts, def, point.values[params::toIndex (def.param)]);

    loadedMeta = point.meta;
    {
        const juce::ScopedLock sl (snapshotLock);
        snapshot = currentValues();
        snapshotValid = !point.modified;
    }

    auto s = session.get();
    s.presetName = point.presetName;
    s.presetKey = point.presetKey;
    s.presetModified = point.modified;
    session.set (s);

    if (onLoaded)
        onLoaded (point.presetKey);
}

// ---- Saving
// ----------------------------------------------------------------------------------------------------------

std::optional<juce::File> PresetManager::fileForName (const juce::String& name, const juce::String& bank) const
{
    const auto folder = presets::bankFolderFor (userFolder, bank);
    if (!folder)
        return std::nullopt;
    return presets::presetFileFor (*folder, presets::sanitise ({name, {}, {}, {}, {}}).name);
}

bool PresetManager::wouldReplace (const juce::String& name, const juce::String& bank) const
{
    const auto file = fileForName (name, bank);
    return file && file->exists();
}

presets::SaveResult PresetManager::saveAs (const presets::Metadata& meta, const juce::String& bank)
{
    const auto clean = presets::sanitise (meta);
    const auto file = fileForName (clean.name, bank);
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
    libraryChanged();
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
    {
        becomeLoaded (meta, current.key);
        libraryChanged();
    }
    return result;
}

presets::SaveResult PresetManager::rename (const presets::Entry& entry, const juce::String& newName)
{
    if (entry.isFactory || !entry.file.isAChildOf (userFolder))
        return presets::SaveResult::isFactoryPreset;

    juce::File renamed;
    const auto result = presets::renamePresetFile (entry.file, newName, THUNDER_VERSION_STRING, renamed);
    if (result != presets::SaveResult::ok)
        return result;

    const auto newKey = presets::userKey (renamed);
    if (isFavourite (entry.key))
    {
        setFavourite (entry.key, false);
        setFavourite (newKey, true);
    }

    // The loaded preset stays the loaded one, under its new name.
    auto s = session.get();
    if (s.presetKey == entry.key)
    {
        presets::Preset preset;
        if (presets::readFile (renamed, preset).ok())
            loadedMeta = preset.meta;
        s.presetKey = newKey;
        s.presetName = loadedMeta.name;
        session.set (s);
    }

    libraryChanged();
    return presets::SaveResult::ok;
}

bool PresetManager::remove (const presets::Entry& entry)
{
    if (entry.isFactory || !entry.file.isAChildOf (userFolder) || !entry.file.existsAsFile())
        return false;

    // To the bin where there is one.
    const auto removed = entry.file.moveToTrash() || entry.file.deleteFile();
    if (removed)
    {
        setFavourite (entry.key, false);
        libraryChanged();
    }
    return removed;
}

presets::ImportReport PresetManager::importFiles (const juce::Array<juce::File>& files)
{
    auto report = presets::importPresets (files, userFolder, THUNDER_VERSION_STRING);
    if (report.imported > 0)
        libraryChanged();
    return report;
}

presets::SaveResult PresetManager::exportTo (const juce::File& file)
{
    auto meta = loadedMeta;
    if (meta.name.isEmpty())
        meta.name = getCurrent().name;

    const auto target =
        file.hasFileExtension (presets::kFileExtension) ? file : file.withFileExtension (presets::kFileExtension);
    return presets::writePresetFile (target, presets::Preset::complete (meta, currentValues()), THUNDER_VERSION_STRING);
}

bool PresetManager::openFolder()
{
    if (!userFolder.createDirectory())
        return false;
    userFolder.revealToUser();
    return true;
}

// ---- Host programs
// ---------------------------------------------------------------------------------------------------

int PresetManager::numPrograms()
{
    // Init, then the factory presets in their fixed program order.
    return 1 + static_cast<int> (presets::kFactoryProgramOrder.size());
}

juce::String PresetManager::programName (int index)
{
    if (index == 0)
        return "Init";
    if (index > 0 && index < numPrograms())
        return juce::String::fromUTF8 (presets::kFactoryProgramOrder[static_cast<std::size_t> (index - 1)].data(),
                                       static_cast<int> (presets::kFactoryProgramOrder[static_cast<std::size_t> (index - 1)].size()));
    return {};
}

juce::String PresetManager::programKey (int index)
{
    if (index == 0)
        return presets::kInitKey;
    if (index > 0 && index < numPrograms())
        return presets::factoryKey (programName (index));
    return {};
}

int PresetManager::programForKey (const juce::String& key)
{
    if (key.isEmpty() || key == presets::kInitKey)
        return 0;
    for (int i = 1; i < numPrograms(); ++i)
        if (programKey (i) == key)
            return i;
    return -1;
}

juce::String PresetManager::loadProgram (int index)
{
    if (index == 0)
        return loadInit();

    const auto key = programKey (index);
    if (key.isEmpty())
        return {};

    presets::Entry entry;
    entry.name = programName (index);
    entry.key = key;
    entry.isFactory = true;
    return load (entry);
}

// ---- Settings
// --------------------------------------------------------------------------------------------------------

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
        if (isSessionParameter (def.param) || !presets::isPresetParameter (def.param))
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
