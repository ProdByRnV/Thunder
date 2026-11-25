// The full preset library (PRD FR-PRESET-002–005, 012, 013, 015; UI.md §9.11,
// §9.12; ROADMAP T-0.9-06, T-0.9-12): the scan with banks and limits, search
// and filters, ◄ ► through the filtered list, favourites, rename, delete,
// import, export, the browser, and the host's preset menu.

#include "core/presets/PresetFilter.h"
#include "core/presets/PresetLibrary.h"
#include "core/state/UserSettings.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "support/TestSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::presets;
using Catch::Matchers::WithinAbs;
using params::ParamId;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderLibraryTests-" + juce::Uuid().toString());

    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

// Writes a small user preset.
juce::File addPreset (const juce::File& folder, const juce::String& name, const juce::String& category = {},
                      const juce::String& tags = {}, float grainAmount = 50.0f, const juce::String& author = {})
{
    Preset p;
    p.meta.name = name;
    p.meta.category = category;
    p.meta.tags = tags;
    p.meta.author = author;
    p.values[params::toIndex (ParamId::grainAmount)] = grainAmount;
    const auto file = folder.getChildFile (name + kFileExtension);
    REQUIRE (writePresetFile (file, p, "0.9.0") == SaveResult::ok);
    return file;
}

juce::StringArray namesOf (const Listing& listing, bool userOnly = true)
{
    juce::StringArray names;
    for (const auto& e : listing.entries)
        if (!userOnly || !e.isFactory)
            names.add (e.name);
    return names;
}

float valueOf (ThunderProcessor& p, ParamId id)
{
    const auto& def = params::paramDef (id);
    return p.getValueTreeState()
        .getRawParameterValue (juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size())))
        ->load();
}

void setValue (ThunderProcessor& p, ParamId id, float value)
{
    const auto& def = params::paramDef (id);
    auto* parameter =
        p.getValueTreeState().getParameter (juce::String::fromUTF8 (def.id.data(), static_cast<int> (def.id.size())));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct TestPlugin
{
    TempFolder temp;
    ThunderProcessor processor;
    juce::File library = temp.folder.getChildFile ("Presets");

    TestPlugin()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        test::chooseMotion (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (library);
        library.createDirectory();
    }

    PresetManager& presets() { return processor.getPresetManager(); }
};
} // namespace

// ---- The scan (FR-PRESET-012) -------------------------------------------------------------------------------------

TEST_CASE ("The library lists factory presets, then the top level, then each bank", "[unit][presets][library]")
{
    TempFolder temp;
    const auto root = temp.folder;
    addPreset (root, "Zed");
    addPreset (root, "Alpha");
    addPreset (root.getChildFile ("Trap"), "Late Haze", "Atmos", "dark, trap");
    addPreset (root.getChildFile ("Trap").getChildFile ("Dark"), "Deep");
    root.getChildFile ("broken" + juce::String (kFileExtension)).replaceWithText ("not a preset");
    root.getChildFile (".hidden" + juce::String (kFileExtension)).replaceWithText ("ignored");

    const auto listing = scanLibrary (root);
    CHECK (listing.entries.size() == factoryPresets().size() + 4);
    CHECK (listing.entries.front().isFactory);
    CHECK (namesOf (listing) == juce::StringArray{"Alpha", "Zed", "Late Haze", "Deep"});
    CHECK (listing.unreadableFiles == 1);
    CHECK_FALSE (listing.incomplete);
    CHECK_FALSE (listing.userFolderMissing);

    const auto& late = listing.entries[factoryPresets().size() + 2];
    CHECK (late.bank == "Trap");
    CHECK (late.bankLabel() == "Trap");
    CHECK (late.tagList() == juce::StringArray{"dark", "trap"});
    CHECK (listing.entries[factoryPresets().size() + 3].bank == "Trap/Dark");
    CHECK (listing.entries[factoryPresets().size()].bankLabel() == "User");
    CHECK (listing.entries.front().bankLabel() == "Factory");

    // A missing folder is normal.
    const auto missing = scanLibrary (root.getChildFile ("nothing here"));
    CHECK (missing.userFolderMissing);
    CHECK (missing.entries.size() == factoryPresets().size());
}

TEST_CASE ("A scan that hits a limit says the list is incomplete", "[unit][presets][library]")
{
    TempFolder temp;
    for (int i = 0; i < 5; ++i)
        addPreset (temp.folder, "Preset " + juce::String (i));
    addPreset (temp.folder.getChildFile ("A").getChildFile ("B"), "Deep");

    const auto limited = scanLibrary (temp.folder, {}, 3);
    CHECK (limited.numUserPresets() == 3);
    CHECK (limited.incomplete);

    // Banks deeper than the level limit aren't entered.
    const auto shallow = scanLibrary (temp.folder, {}, kMaxLibraryPresets, 1);
    CHECK_FALSE (namesOf (shallow).contains ("Deep"));
    CHECK (shallow.incomplete);
    CHECK (namesOf (scanLibrary (temp.folder)).contains ("Deep"));

    // A cancelled scan stops at once.
    const auto cancelled = scanLibrary (temp.folder, [] { return true; });
    CHECK (cancelled.numUserPresets() == 0);
    CHECK (cancelled.incomplete);
}

TEST_CASE ("The library is scanned in the background", "[unit][presets][library]")
{
    TestPlugin plugin;
    addPreset (plugin.library, "First");

    auto& presets = plugin.presets();
    presets.getListing(); // starts the scan
    REQUIRE (presets.waitForScan());
    CHECK (namesOf (presets.getListing()).contains ("First"));

    addPreset (plugin.library.getChildFile ("Bank"), "Second");
    const auto version = presets.getListingVersion();
    presets.rescan();
    REQUIRE (presets.waitForScan());
    CHECK (presets.getListingVersion() > version);
    CHECK (namesOf (presets.getListing()).contains ("Second"));
}

// ---- Search, filters and ◄ ► (FR-PRESET-004) ---------------------------------------------------------------------

TEST_CASE ("Search and filters combine, and sorting keeps ties in library order", "[unit][presets][library]")
{
    TempFolder temp;
    addPreset (temp.folder, "Late Haze", "Atmos", "dark,trap", 50.0f, "Mira");
    addPreset (temp.folder.getChildFile ("Trap"), "Bright Rain", "Shimmer", "trap", 50.0f, "Abe");
    const auto listing = scanLibrary (temp.folder);
    const juce::StringArray none;

    const auto names = [&listing] (const std::vector<int>& indices)
    {
        juce::StringArray list;
        for (const auto i : indices)
            list.add (listing.entries[static_cast<std::size_t> (i)].name);
        return list;
    };

    Filter f;
    f.search = "haze";
    CHECK (names (filtered (listing, f, {}, none)).contains ("Late Haze"));
    CHECK (names (filtered (listing, f, {}, none)).contains ("Low Haze")); // a factory preset

    f.search = "TRAP haze"; // every word, any field, any case
    CHECK (names (filtered (listing, f, {}, none)) == juce::StringArray{"Late Haze"});

    f = {};
    f.bankId = "user:Trap";
    CHECK (names (filtered (listing, f, {}, none)) == juce::StringArray{"Bright Rain"});

    f = {};
    f.tag = "dark";
    CHECK (names (filtered (listing, f, {}, none)) == juce::StringArray{"Late Haze"});

    f = {};
    f.category = "Shimmer";
    CHECK (names (filtered (listing, f, {}, none)).contains ("Bright Rain"));
    CHECK (names (filtered (listing, f, {}, none)).contains ("Glass Choir"));
    CHECK_FALSE (names (filtered (listing, f, {}, none)).contains ("Late Haze"));

    f = {};
    f.favouritesOnly = true;
    CHECK (filtered (listing, f, {}, none).empty());
    CHECK (names (filtered (listing, f, {}, {userKey (temp.folder.getChildFile ("Late Haze.thunderpreset"))})) ==
           juce::StringArray{"Late Haze"});

    // Sorted by author, descending: Thunder's factory presets first, then Mira, then Abe.
    const auto byAuthor = names (filtered (listing, {}, {SortBy::author, true}, none));
    CHECK (byAuthor.indexOf ("Late Haze") < byAuthor.indexOf ("Bright Rain"));
    CHECK (byAuthor.indexOf ("Night Static") < byAuthor.indexOf ("Late Haze"));

    // What the sidebar offers.
    CHECK (categoriesIn (listing).contains ("Rhythmic")); // an initial category, though unused
    CHECK (bankIdsIn (listing) == juce::StringArray{"factory", "user:", "user:Trap"});
    CHECK (tagsIn (listing) == juce::StringArray{"dark", "trap"});
}

TEST_CASE ("Previous and next step through the list as it is filtered", "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();
    presets.rescanNow();

    Filter f;
    f.category = "Harmony";
    presets.setFilter (f);
    const auto harmony = presets.getFilteredEntries();
    REQUIRE (harmony.size() == 4);

    // From Init (not in the list), ► goes to the first, then on, wrapping at the end.
    REQUIRE (presets.step (1).isEmpty());
    CHECK (presets.getCurrent().name == harmony[0].name);
    for (std::size_t i = 1; i < harmony.size(); ++i)
        presets.step (1);
    CHECK (presets.getCurrent().name == harmony.back().name);
    presets.step (1);
    CHECK (presets.getCurrent().name == harmony.front().name);
    presets.step (-1);
    CHECK (presets.getCurrent().name == harmony.back().name);
}

// ---- Favourites ---------------------------------------------------------------------------------------------------

TEST_CASE ("Favourites are global and follow a renamed preset", "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();
    const auto file = addPreset (plugin.library, "Keeper");
    presets.rescanNow();

    const auto key = userKey (file);
    presets.setFavourite (key, true);
    presets.setFavourite (factoryKey ("Night Static"), true);
    presets.setFavourite (kInitKey, true); // Init can't be one
    CHECK (presets.isFavourite (key));
    CHECK_FALSE (presets.isFavourite (kInitKey));
    CHECK (state::UserSettings::load (plugin.processor.getSettingsFile()).favouritePresets.size() == 2);

    const auto entry = presets.findEntry (key);
    REQUIRE (entry.has_value());
    REQUIRE (presets.rename (*entry, "Kept") == SaveResult::ok);
    CHECK_FALSE (presets.isFavourite (key));
    CHECK (presets.isFavourite (userKey (plugin.library.getChildFile ("Kept.thunderpreset"))));

    presets.setFavourite (factoryKey ("Night Static"), false);
    CHECK_FALSE (presets.isFavourite (factoryKey ("Night Static")));
}

// ---- Saving into banks, rename, delete (FR-PRESET-005) -----------------------------------------------------------

TEST_CASE ("Saving into a bank makes its folder, and bank names are kept inside the library",
           "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();

    Metadata meta;
    meta.name = "Night Rain";
    meta.tags = "dark,wide";
    REQUIRE (presets.saveAs (meta, "Trap/Dark") == SaveResult::ok);
    const auto file =
        plugin.library.getChildFile ("Trap").getChildFile ("Dark").getChildFile ("Night Rain.thunderpreset");
    CHECK (file.existsAsFile());
    CHECK (presets.getLoadedBank() == "Trap/Dark");
    CHECK (presets.wouldReplace ("Night Rain", "Trap/Dark"));
    CHECK_FALSE (presets.wouldReplace ("Night Rain"));

    REQUIRE (presets.waitForScan());
    const auto entry = presets.findEntry (userKey (file));
    REQUIRE (entry.has_value());
    CHECK (entry->tags == "dark,wide");

    // Banks can't leave the library or go deeper than the limit.
    CHECK_FALSE (bankFolderFor (plugin.library, "..").has_value());
    CHECK_FALSE (bankFolderFor (plugin.library, "../Escape").has_value());
    CHECK_FALSE (bankFolderFor (plugin.library, "a/b/c/d/e/f/g/h/i").has_value());
    CHECK_FALSE (bankFolderFor (plugin.library, "CON").has_value());
    CHECK (bankFolderFor (plugin.library, "").value_or (juce::File()) == plugin.library);
}

TEST_CASE ("Rename never replaces another preset, and the loaded preset follows it", "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();
    const auto first = addPreset (plugin.library, "First", {}, {}, 11.0f);
    addPreset (plugin.library, "Second", {}, {}, 22.0f);
    presets.rescanNow();

    REQUIRE (presets.loadFile (first).isEmpty());
    const auto entry = presets.findEntry (userKey (first));
    REQUIRE (entry.has_value());

    CHECK (presets.rename (*entry, "Second") == SaveResult::nameTaken);
    CHECK (first.existsAsFile());
    CHECK (presets.rename (*entry, "CON") == SaveResult::invalidName);

    REQUIRE (presets.rename (*entry, "Renamed") == SaveResult::ok);
    CHECK_FALSE (first.existsAsFile());
    const auto renamed = plugin.library.getChildFile ("Renamed.thunderpreset");
    REQUIRE (renamed.existsAsFile());
    CHECK (presets.getCurrent().name == "Renamed");
    CHECK (presets.getCurrent().key == userKey (renamed));
    CHECK_FALSE (presets.getCurrent().modified);

    Preset back;
    REQUIRE (readFile (renamed, back).ok());
    CHECK (back.meta.name == "Renamed");
    CHECK_THAT (back.valueOr (ParamId::grainAmount), WithinAbs (11.0, 1e-4));

    // Only the capitals change: still allowed.
    REQUIRE (presets.waitForScan());
    const auto again = presets.findEntry (userKey (renamed));
    REQUIRE (again.has_value());
    REQUIRE (presets.rename (*again, "RENAMED") == SaveResult::ok);
    CHECK (presets.getCurrent().name == "RENAMED");

    // Factory presets can't be renamed or deleted.
    Entry factory;
    factory.isFactory = true;
    factory.key = factoryKey ("Night Static");
    CHECK (presets.rename (factory, "Mine") == SaveResult::isFactoryPreset);
    CHECK_FALSE (presets.remove (factory));
}

TEST_CASE ("Delete removes a user preset and its favourite", "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();
    const auto file = addPreset (plugin.library, "Gone");
    presets.rescanNow();
    presets.setFavourite (userKey (file), true);

    const auto entry = presets.findEntry (userKey (file));
    REQUIRE (entry.has_value());
    REQUIRE (presets.remove (*entry));
    CHECK_FALSE (file.existsAsFile());
    CHECK_FALSE (presets.isFavourite (userKey (file)));
    REQUIRE (presets.waitForScan());
    CHECK_FALSE (namesOf (presets.getListing()).contains ("Gone"));
}

// ---- Import and export (FR-PRESET-013) ----------------------------------------------------------------------------

TEST_CASE ("Import never replaces anything", "[unit][presets][library]")
{
    TestPlugin plugin;
    TempFolder sent;
    auto& presets = plugin.presets();

    const auto existing = addPreset (plugin.library, "Rain", {}, {}, 30.0f);
    const auto same = addPreset (sent.folder, "Rain", {}, {}, 30.0f);                         // identical
    const auto different = addPreset (sent.folder.getChildFile ("x"), "Rain", {}, {}, 70.0f); // same name
    const auto fresh = addPreset (sent.folder, "Snow", {}, {}, 40.0f);
    const auto junk = sent.folder.getChildFile ("junk.thunderpreset");
    junk.replaceWithText ("<NotAPreset/>");

    const auto report = presets.importFiles ({same, different, fresh, junk});
    CHECK (report.imported == 2);
    CHECK (report.alreadyInLibrary == 1);
    CHECK (report.notPresets == 1);
    CHECK (report.describe() ==
           juce::String::fromUTF8 (
               "Imported 2 presets \xc2\xb7 1 already in your library \xc2\xb7 1 isn't a Thunder preset"));

    // The original is untouched; the different one became "Rain (2)".
    Preset back;
    REQUIRE (readFile (existing, back).ok());
    CHECK_THAT (back.valueOr (ParamId::grainAmount), WithinAbs (30.0, 1e-4));
    const auto second = plugin.library.getChildFile ("Rain (2).thunderpreset");
    REQUIRE (readFile (second, back).ok());
    CHECK (back.meta.name == "Rain (2)");
    CHECK_THAT (back.valueOr (ParamId::grainAmount), WithinAbs (70.0, 1e-4));
    CHECK (plugin.library.getChildFile ("Snow.thunderpreset").existsAsFile());

    // Importing the same files again adds nothing.
    const auto again = presets.importFiles ({same, different, fresh});
    CHECK (again.imported == 0);
    CHECK (again.alreadyInLibrary == 3);
}

TEST_CASE ("Export writes the current sound anywhere, safely", "[unit][presets][library]")
{
    TestPlugin plugin;
    TempFolder elsewhere;
    auto& presets = plugin.presets();
    setValue (plugin.processor, ParamId::grainAmount, 64.0f);

    const auto target = elsewhere.folder.getChildFile ("Shared");
    REQUIRE (presets.exportTo (target) == SaveResult::ok);
    Preset back;
    REQUIRE (readFile (target.withFileExtension (kFileExtension), back).ok());
    CHECK_THAT (back.valueOr (ParamId::grainAmount), WithinAbs (64.0, 1e-3));
    CHECK (back.meta.name == "Init");
}

// ---- Previews and Esc ---------------------------------------------------------------------------------------------

TEST_CASE ("Going back after previews restores the exact sound and preset", "[unit][presets][library]")
{
    TestPlugin plugin;
    auto& presets = plugin.presets();
    presets.rescanNow();
    setValue (plugin.processor, ParamId::grainAmount, 12.0f); // a modified Init
    const auto point = presets.makeRevertPoint();

    presets.load (presets.getListing().entries[3]);
    presets.load (presets.getListing().entries[5]);
    CHECK (presets.getCurrent().name == presets.getListing().entries[5].name);

    presets.revertTo (point);
    CHECK_THAT (valueOf (plugin.processor, ParamId::grainAmount), WithinAbs (12.0, 1e-3));
    CHECK (presets.getCurrent().isInit());
    CHECK (presets.getCurrent().modified);
}

// ---- The host's preset menu (FR-PRESET-015) -----------------------------------------------------------------------

TEST_CASE ("The DAW's preset menu lists Init and the factory presets and loads them", "[unit][presets][programs]")
{
    TestPlugin plugin;
    auto& processor = plugin.processor;

    CHECK (processor.getNumPrograms() == 1 + static_cast<int> (factoryPresets().size()));
    CHECK (processor.getProgramName (0) == "Init");
    CHECK (processor.getProgramName (1) == factoryPresets().front().preset.meta.name);
    CHECK (processor.getCurrentProgram() == 0);

    processor.setCurrentProgram (3);
    CHECK (processor.getCurrentProgram() == 3);
    CHECK (plugin.presets().getCurrent().name == processor.getProgramName (3));
    CHECK (plugin.presets().getCurrent().isFactory());

    // A preset loaded in Thunder moves the host's program along.
    plugin.presets().loadProgram (5);
    CHECK (processor.getCurrentProgram() == 5);
    plugin.presets().loadInit();
    CHECK (processor.getCurrentProgram() == 0);

    // Out of range: nothing happens.
    processor.setCurrentProgram (999);
    CHECK (processor.getCurrentProgram() == 0);
}

TEST_CASE ("A host sending the project's program again never loads over an edited sound", "[unit][presets][programs]")
{
    TestPlugin plugin;
    auto& processor = plugin.processor;

    processor.setCurrentProgram (2);
    setValue (processor, ParamId::grainAmount, 3.0f); // edited
    REQUIRE (plugin.presets().getCurrent().modified);

    juce::MemoryBlock data;
    processor.getStateInformation (data);

    // The project reopens; the host sends the program it remembers.
    TestPlugin reopened;
    reopened.processor.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    CHECK (reopened.processor.getCurrentProgram() == 2);
    reopened.processor.setCurrentProgram (2);
    CHECK_THAT (valueOf (reopened.processor, ParamId::grainAmount), WithinAbs (3.0, 1e-3));
    CHECK (reopened.presets().getCurrent().modified);

    // Choosing another program still loads it.
    reopened.processor.setCurrentProgram (4);
    CHECK (reopened.presets().getCurrent().name == reopened.processor.getProgramName (4));
}

TEST_CASE ("After one of your presets, choosing the DAW's program loads it again", "[unit][presets][programs]")
{
    TestPlugin plugin;
    auto& processor = plugin.processor;
    const auto mineFile = addPreset (plugin.library, "Mine", "Atmos", {}, 7.0f);
    plugin.presets().rescanNow();

    processor.setCurrentProgram (2);
    REQUIRE (plugin.presets().getCurrent().name == processor.getProgramName (2));

    const auto mine = plugin.presets().findEntry (thunder::presets::userKey (mineFile));
    REQUIRE (mine.has_value());
    REQUIRE (plugin.presets().load (*mine).isEmpty());
    REQUIRE (plugin.presets().getCurrent().name == "Mine");

    processor.setCurrentProgram (2);
    CHECK (plugin.presets().getCurrent().name == processor.getProgramName (2));
    CHECK_FALSE (plugin.presets().getCurrent().modified);
}

// ---- The browser (UI.md §9.12) ------------------------------------------------------------------------------------

TEST_CASE ("The browser previews on click, keeps arrows from loading, and Esc goes back", "[unit][presets][ui]")
{
    TestPlugin plugin;
    addPreset (plugin.library, "Mine", "Atmos", "dark");
    plugin.presets().rescanNow();
    auto editor = std::make_unique<ThunderEditor> (plugin.processor);

    editor->openPresetBrowser();
    auto* browser = editor->getPresetBrowser();
    REQUIRE (browser != nullptr);
    plugin.presets().waitForScan();
    browser->refreshList();
    CHECK (browser->getNumRows() == static_cast<int> (factoryPresets().size()) + 1);

    // Arrow keys: the highlight moves, the sound doesn't.
    browser->highlightRow (2);
    CHECK (plugin.presets().getCurrent().isInit());
    CHECK (browser->getDetailText().isNotEmpty());

    // A click previews.
    browser->previewRow (2);
    CHECK (plugin.presets().getCurrent().name == browser->getRow (2).name);

    // Esc: back to Init.
    browser->revertAndClose();
    CHECK (plugin.presets().getCurrent().isInit());

    editor.reset();
}

TEST_CASE ("The browser searches, filters and marks favourites", "[unit][presets][ui]")
{
    TestPlugin plugin;
    addPreset (plugin.library, "Mine", "Atmos", "dark,trap");
    addPreset (plugin.library.getChildFile ("Trap"), "Banked", "Grit", "trap");
    plugin.presets().rescanNow();
    auto editor = std::make_unique<ThunderEditor> (plugin.processor);
    editor->openPresetBrowser();
    auto* browser = editor->getPresetBrowser();
    REQUIRE (browser != nullptr);
    plugin.presets().waitForScan();
    browser->refreshList();

    browser->setSearch ("mine");
    REQUIRE (browser->getNumRows() == 1);
    CHECK (browser->getRow (0).name == "Mine");
    browser->highlightRow (0);
    CHECK (browser->getDetailText().contains ("Tags: dark"));

    browser->setSearch ({});
    browser->setBank ("user:Trap");
    REQUIRE (browser->getNumRows() == 1);
    CHECK (browser->getRow (0).name == "Banked");

    // The sidebar offers the bank and the tags.
    bool hasTrapBank = false, hasDarkTag = false;
    for (const auto& r : browser->getSidebarRows())
    {
        hasTrapBank = hasTrapBank || (r.kind == ui::PresetBrowser::SidebarRow::Kind::bank && r.label == "Trap");
        hasDarkTag = hasDarkTag || (r.kind == ui::PresetBrowser::SidebarRow::Kind::tag && r.value == "dark");
    }
    CHECK (hasTrapBank);
    CHECK (hasDarkTag);

    browser->setBank ({});
    browser->setFavouritesOnly (true);
    CHECK (browser->getNumRows() == 0);
    CHECK (browser->getDetailText() == "No presets match.");
    browser->setFavouritesOnly (false);

    const auto row = browser->getNumRows() - 1;
    browser->toggleFavourite (row);
    browser->setFavouritesOnly (true);
    CHECK (browser->getNumRows() == 1);

    // ◄ ► now step through the favourites only.
    browser->close();
    plugin.presets().step (1);
    plugin.presets().step (1);
    CHECK (plugin.presets().isFavourite (plugin.presets().getCurrent().key));

    editor.reset();
}

TEST_CASE ("The browser renames and deletes in place, and says what it couldn't read", "[unit][presets][ui]")
{
    TestPlugin plugin;
    addPreset (plugin.library, "Old Name");
    plugin.library.getChildFile ("broken.thunderpreset").replaceWithText ("nope");
    plugin.presets().rescanNow();
    auto editor = std::make_unique<ThunderEditor> (plugin.processor);
    editor->openPresetBrowser();
    auto* browser = editor->getPresetBrowser();
    REQUIRE (browser != nullptr);
    plugin.presets().waitForScan();
    browser->refreshList();
    CHECK (browser->getFooterText().contains ("1 file could not be read"));

    browser->setSearch ("old name");
    REQUIRE (browser->getNumRows() == 1);
    browser->highlightRow (0);
    CHECK (browser->getRenameButton().isEnabled());

    browser->startRename();
    CHECK (browser->isRenaming());
    REQUIRE (browser->confirmRename ("New Name"));
    CHECK (plugin.library.getChildFile ("New Name.thunderpreset").existsAsFile());

    browser->setSearch ("new name");
    REQUIRE (browser->getNumRows() == 1);
    browser->highlightRow (0);
    browser->startDelete();
    CHECK (browser->isAskingToDelete());
    CHECK (browser->getPromptText().contains ("New Name"));
    REQUIRE (browser->confirmDelete());
    CHECK_FALSE (plugin.library.getChildFile ("New Name.thunderpreset").existsAsFile());
    CHECK (browser->getNumRows() == 0);

    // Factory presets can't be renamed or deleted.
    browser->setSearch ({});
    browser->highlightRow (0);
    CHECK_FALSE (browser->getRenameButton().isEnabled());
    CHECK_FALSE (browser->getDeleteButton().isEnabled());

    editor.reset();
}

TEST_CASE ("An empty library says how to fill it", "[unit][presets][ui]")
{
    TestPlugin plugin;
    plugin.presets().rescanNow();
    auto editor = std::make_unique<ThunderEditor> (plugin.processor);
    editor->openPresetBrowser();
    auto* browser = editor->getPresetBrowser();
    REQUIRE (browser != nullptr);
    plugin.presets().waitForScan();
    browser->refreshList();
    CHECK (browser->getFooterText().contains ("No user presets yet"));
    editor.reset();
}

TEST_CASE ("The Save form saves tags into a bank", "[unit][presets][ui]")
{
    TestPlugin plugin;
    auto editor = std::make_unique<ThunderEditor> (plugin.processor);
    editor->openSaveForm (true);
    auto* form = editor->getSaveForm();
    REQUIRE (form != nullptr);

    form->getNameField().setText ("Tagged", false);
    form->getTagsField().setText (" dark , wide,dark ", false);
    form->getBankField().setText ("Lo-Fi Bank", juce::sendNotificationSync);
    form->submit();

    const auto file = plugin.library.getChildFile ("Lo-Fi Bank").getChildFile ("Tagged.thunderpreset");
    REQUIRE (file.existsAsFile());
    Preset back;
    REQUIRE (readFile (file, back).ok());
    CHECK (back.meta.tags == "dark,wide");
    editor.reset();
}

TEST_CASE ("About shows the version and the build", "[unit][ui]")
{
    ui::AboutPanel::Build build;
    build.format = "VST3";
    build.host = "FL Studio";
    ui::AboutPanel about (build);

    CHECK (about.versionLine().startsWith ("Version " + juce::String (THUNDER_VERSION_STRING)));
    CHECK (about.buildLine().startsWith ("VST3"));
    CHECK (about.buildLine().contains ("FL Studio"));
    CHECK (about.detailsText().contains ("Format: VST3"));
    CHECK (about.detailsText().contains ("Host: FL Studio"));
    CHECK (about.detailsText().contains ("System: "));
}
