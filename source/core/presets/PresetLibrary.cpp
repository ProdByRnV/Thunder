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

// A name as a file or folder name every OS accepts, or nothing.
std::optional<juce::String> cleanFileName (const juce::String& name)
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

    return clean;
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

Entry entryFor (const Metadata& meta)
{
    Entry e;
    e.name = meta.name;
    e.author = meta.author;
    e.category = meta.category;
    e.tags = meta.tags;
    e.description = meta.description;
    return e;
}

// Walks one folder of the library: its presets, then its banks.
struct Scan
{
    const juce::File& root;
    const std::function<bool()>& shouldStop;
    int maxPresets;
    int maxDepth;
    Listing& listing;
    std::vector<Entry>& user;
    int seen = 0;

    bool stopped() const { return shouldStop && shouldStop(); }

    void folder (const juce::File& dir, int depth)
    {
        constexpr auto files = juce::File::findFiles | juce::File::ignoreHiddenFiles;
        for (const auto& file : dir.findChildFiles (files, false, kFileWildcard))
        {
            // Dot files are hidden everywhere (and half-written saves are named so).
            if (file.getFileName().startsWithChar ('.'))
                continue;

            if (stopped() || seen >= maxPresets)
            {
                listing.incomplete = true;
                return;
            }
            ++seen;

            Preset preset;
            if (!readFile (file, preset).ok())
            {
                ++listing.unreadableFiles;
                continue;
            }

            auto e = entryFor (preset.meta);
            if (e.name.isEmpty())
                e.name = file.getFileNameWithoutExtension();
            e.bank = dir == root ? juce::String() : dir.getRelativePathFrom (root).replaceCharacter ('\\', '/');
            e.key = userKey (file);
            e.file = file;
            user.push_back (std::move (e));
        }

        constexpr auto dirs = juce::File::findDirectories | juce::File::ignoreHiddenFiles;
        auto children = dir.findChildFiles (dirs, false);
        children.sort();
        for (const auto& child : children)
        {
            if (child.getFileName().startsWithChar ('.'))
                continue;
            if (stopped())
            {
                listing.incomplete = true;
                return;
            }
            if (depth + 1 > maxDepth)
            {
                listing.incomplete = true; // deeper banks aren't listed
                continue;
            }
            folder (child, depth + 1);
        }
    }
};

void appendNumber (juce::String& name, int number)
{
    name << " (" << number << ")";
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

juce::StringArray Entry::tagList() const
{
    auto list = juce::StringArray::fromTokens (tags, ",", "");
    list.trim();
    list.removeEmptyStrings();
    return list;
}

juce::String Entry::bankLabel() const
{
    if (isFactory)
        return kFactoryBank;
    return bank.isEmpty() ? juce::String ("User") : bank;
}

juce::String Entry::bankId() const
{
    return isFactory ? juce::String ("factory") : "user:" + bank;
}

int Listing::numUserPresets() const noexcept
{
    return static_cast<int> (
        std::count_if (entries.begin(), entries.end(), [] (const Entry& e) { return !e.isFactory; }));
}

juce::File defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Thunder")
        .getChildFile ("Presets");
}

Listing scanLibrary (const juce::File& userFolder, const std::function<bool()>& shouldStop, int maxPresets,
                     int maxDepth)
{
    Listing listing;

    for (const auto& factory : factoryPresets())
    {
        auto e = entryFor (factory.preset.meta);
        e.key = factoryKey (e.name);
        e.isFactory = true;
        listing.entries.push_back (e);
    }

    if (!userFolder.isDirectory())
    {
        listing.userFolderMissing = true; // created on the first save: missing is normal
        return listing;
    }

    std::vector<Entry> user;
    Scan scan{userFolder, shouldStop, maxPresets, maxDepth, listing, user};
    scan.folder (userFolder, 0);

    // The top level first, then each bank, each by name.
    std::stable_sort (user.begin(), user.end(),
                      [] (const Entry& a, const Entry& b)
                      {
                          if (a.bank != b.bank)
                              return a.bank.isEmpty() || (!b.bank.isEmpty() && byName (a.bank, b.bank));
                          return byName (a.name, b.name);
                      });
    listing.entries.insert (listing.entries.end(), user.begin(), user.end());
    return listing;
}

std::optional<juce::File> presetFileFor (const juce::File& folder, const juce::String& name)
{
    const auto clean = cleanFileName (name);
    if (!clean)
        return std::nullopt;

    const auto file = folder.getChildFile (*clean + kFileExtension);
    if (!file.isAChildOf (folder))
        return std::nullopt;

    return file;
}

std::optional<juce::File> bankFolderFor (const juce::File& userFolder, const juce::String& bank)
{
    auto levels = juce::StringArray::fromTokens (bank, "/\\", "");
    levels.trim();
    levels.removeEmptyStrings();
    if (levels.size() > kMaxBankDepth)
        return std::nullopt;

    auto folder = userFolder;
    for (const auto& level : levels)
    {
        const auto clean = cleanFileName (level);
        if (!clean)
            return std::nullopt;
        folder = folder.getChildFile (*clean);
    }

    if (folder != userFolder && !folder.isAChildOf (userFolder))
        return std::nullopt;

    return folder;
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
        case SaveResult::nameTaken:
            return "A preset with that name already exists.";
    }

    return "Couldn't save.";
}

SaveResult renamePresetFile (const juce::File& file, const juce::String& newName, const juce::String& thunderVersion,
                             juce::File& renamed)
{
    Preset preset;
    if (!readFile (file, preset).ok())
        return SaveResult::couldNotWrite;

    const auto name = sanitise ({newName, {}, {}, {}, {}}).name;
    const auto target = presetFileFor (file.getParentDirectory(), name);
    if (!target)
        return SaveResult::invalidName;

    // The same file with different capitals is still this preset (Windows and
    // macOS file names ignore case).
    const auto sameFile = target->getFullPathName().equalsIgnoreCase (file.getFullPathName());
    if (target->exists() && !sameFile)
        return SaveResult::nameTaken;

    preset.meta.name = name;

    if (sameFile)
    {
        // Only the capitals change: the new name goes in, then the file moves.
        const auto result = writePresetFile (file, preset, thunderVersion);
        if (result != SaveResult::ok)
            return result;
        if (target->getFileName() != file.getFileName())
            file.moveFileTo (*target);
        renamed = *target;
        return SaveResult::ok;
    }

    const auto result = writePresetFile (*target, preset, thunderVersion);
    if (result != SaveResult::ok)
        return result;

    file.deleteFile();
    renamed = *target;
    return SaveResult::ok;
}

bool sameDocument (const Preset& a, const Preset& b)
{
    if (!(sanitise (a.meta) == sanitise (b.meta)))
        return false;

    for (const auto& def : params::kParamDefs)
        if (isPresetParameter (def.param) && !juce::exactlyEqual (a.valueOr (def.param), b.valueOr (def.param)))
            return false;

    return true;
}

ImportReport importPresets (const juce::Array<juce::File>& files, const juce::File& userFolder,
                            const juce::String& thunderVersion)
{
    ImportReport report;

    for (const auto& file : files)
    {
        Preset preset;
        if (!readFile (file, preset).ok())
        {
            ++report.notPresets;
            continue;
        }

        // The sender's file name, or the preset's own name.
        auto base = file.getFileNameWithoutExtension();
        if (!presetFileFor (userFolder, base))
            base = preset.meta.name;
        if (!presetFileFor (userFolder, base))
            base = "Imported preset";

        const auto originalName = preset.meta.name.isNotEmpty() ? preset.meta.name : base;
        bool already = false;
        std::optional<juce::File> target;

        for (int number = 1; number < 1000; ++number)
        {
            auto candidate = base;
            if (number > 1)
                appendNumber (candidate, number);

            const auto f = presetFileFor (userFolder, candidate);
            if (!f)
                break;

            if (!f->exists())
            {
                target = f;
                if (number > 1)
                {
                    preset.meta.name = originalName;
                    appendNumber (preset.meta.name, number);
                }
                break;
            }

            Preset existing;
            auto renamedPreset = preset;
            if (number > 1)
            {
                renamedPreset.meta.name = originalName;
                appendNumber (renamedPreset.meta.name, number);
            }
            if (readFile (*f, existing).ok() &&
                (sameDocument (existing, preset) || sameDocument (existing, renamedPreset)))
            {
                already = true;
                break;
            }
        }

        if (already)
        {
            ++report.alreadyInLibrary;
            continue;
        }

        if (!target || writePresetFile (*target, preset, thunderVersion) != SaveResult::ok)
        {
            ++report.couldNotSave;
            continue;
        }

        ++report.imported;
        report.written.add (*target);
    }

    return report;
}

juce::String ImportReport::describe() const
{
    juce::StringArray parts;
    const auto count = [] (int n, const char* one, const char* many)
    { return juce::String (n) + " " + (n == 1 ? one : many); };

    if (imported > 0 || (alreadyInLibrary == 0 && notPresets == 0 && couldNotSave == 0))
        parts.add ("Imported " + count (imported, "preset", "presets"));
    if (alreadyInLibrary > 0)
        parts.add (juce::String (alreadyInLibrary) + " already in your library");
    if (notPresets > 0)
        parts.add (notPresets == 1 ? juce::String ("1 isn't a Thunder preset")
                                   : juce::String (notPresets) + " aren't Thunder presets");
    if (couldNotSave > 0)
        parts.add (juce::String (couldNotSave) + " couldn't be saved");

    return parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

} // namespace thunder::presets
