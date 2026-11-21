#include "PresetLibrary.h"

#include <ThunderPresetData.h>

#include <algorithm>

namespace thunder::presets
{

namespace
{
bool byName (const juce::String& a, const juce::String& b)
{
    return a.compareNatural (b) < 0;
}

bool isReservedWindowsName (const juce::String& name)
{
    // Reserved with or without an extension ("CON", "con.txt").
    const auto stem = name.upToFirstOccurrenceOf (".", false, false).trim().toUpperCase();
    static const juce::StringArray reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                            "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                            "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    return reserved.contains (stem);
}

std::vector<FactoryPreset> loadFactoryPresets()
{
    std::vector<FactoryPreset> list;

    for (int i = 0; i < ThunderPresetData::namedResourceListSize; ++i)
    {
        const auto* resource = ThunderPresetData::namedResourceList[i];
        const auto* fileName = ThunderPresetData::getNamedResourceOriginalFilename (resource);
        if (fileName == nullptr || !juce::String (fileName).endsWithIgnoreCase (kFileExtension))
            continue;

        int size = 0;
        const auto* data = ThunderPresetData::getNamedResource (resource, size);

        FactoryPreset factory;
        factory.resourceName = fileName;
        factory.report = read (juce::String::fromUTF8 (data, size), factory.preset);

        // A broken factory file is a build error, caught by the factory preset
        // tests; in a release it is simply left out.
        jassert (factory.report.isStrictlyValid());
        if (!factory.report.ok())
            continue;

        if (factory.preset.meta.name.isEmpty())
            factory.preset.meta.name = juce::File (fileName).getFileNameWithoutExtension();

        list.push_back (std::move (factory));
    }

    std::sort (list.begin(), list.end(), [] (const FactoryPreset& a, const FactoryPreset& b)
               { return byName (a.preset.meta.name, b.preset.meta.name); });
    return list;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const auto list = loadFactoryPresets();
    return list;
}

Preset initPreset()
{
    Preset init;
    init.meta.name = "Init";
    return init;
}

juce::String factoryKey (const juce::String& name)
{
    return "factory:" + name;
}

juce::String userKey (const juce::File& file)
{
    return "user:" + file.getFullPathName();
}

juce::File defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Thunder")
        .getChildFile ("Presets");
}

Listing listPresets (const juce::File& userFolder)
{
    Listing listing;

    for (const auto& factory : factoryPresets())
    {
        Entry e;
        e.name = factory.preset.meta.name;
        e.category = factory.preset.meta.category;
        e.key = factoryKey (e.name);
        e.isFactory = true;
        listing.entries.push_back (e);
    }

    if (!userFolder.isDirectory())
        return listing; // created on the first save: missing is normal

    std::vector<Entry> user;
    for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, kFileWildcard))
    {
        Preset preset;
        if (!readFile (file, preset).ok())
        {
            ++listing.unreadableFiles;
            continue;
        }

        Entry e;
        e.name = preset.meta.name.isNotEmpty() ? preset.meta.name : file.getFileNameWithoutExtension();
        e.category = preset.meta.category;
        e.key = userKey (file);
        e.file = file;
        user.push_back (e);
    }

    std::sort (user.begin(), user.end(), [] (const Entry& a, const Entry& b) { return byName (a.name, b.name); });
    listing.entries.insert (listing.entries.end(), user.begin(), user.end());
    return listing;
}

std::optional<juce::File> presetFileFor (const juce::File& folder, const juce::String& name)
{
    juce::String clean;
    for (auto p = name.getCharPointer(); !p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c < 32 || c == 127 || juce::String ("<>:\"/\\|?*").containsChar (c))
            continue;
        clean << juce::String::charToString (c);
    }

    clean = clean.trim().trimCharactersAtStart (". ").trimCharactersAtEnd (". ");
    if (clean.length() > kMaxFieldLength)
        clean = clean.substring (0, kMaxFieldLength).trimCharactersAtEnd (". ");

    if (clean.isEmpty() || isReservedWindowsName (clean))
        return std::nullopt;

    const auto file = folder.getChildFile (clean + kFileExtension);
    if (!file.isAChildOf (folder))
        return std::nullopt;

    return file;
}

SaveResult writePresetFile (const juce::File& file, const Preset& preset, const juce::String& thunderVersion)
{
    if (!file.getParentDirectory().createDirectory())
        return SaveResult::couldNotCreateFolder;

    // juce::TemporaryFile writes beside the target and replaces it only when
    // asked, after the new contents are complete.
    juce::TemporaryFile temp (file, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream out (temp.getFile());
        if (!out.openedOk() || !out.writeText (write (preset, thunderVersion), false, false, nullptr))
            return SaveResult::couldNotWrite;
        out.flush();
        if (out.getStatus().failed())
            return SaveResult::couldNotWrite;
    }

    return temp.overwriteTargetFileWithTemporary() ? SaveResult::ok : SaveResult::couldNotWrite;
}

juce::String describe (SaveResult result)
{
    switch (result)
    {
        case SaveResult::ok:
            return {};
        case SaveResult::invalidName:
        case SaveResult::outsideLibrary:
            return "That name can't be used as a file name.";
        case SaveResult::isFactoryPreset:
            return juce::String::fromUTF8 ("Factory presets can't be changed \xe2\x80\x94 save under a new name.");
        case SaveResult::couldNotCreateFolder:
        case SaveResult::couldNotWrite:
            return "Couldn't save. Your existing preset wasn't changed.";
    }

    return "Couldn't save.";
}

} // namespace thunder::presets
