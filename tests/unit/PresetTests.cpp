// Presets (PRD FR-PRESET-001/002/005/006/007/009/010/011/014, QA-STATE-002/003):
// the file format and its validated reader, the factory presets, safe saving,
// and loading on a live processor.

#include "support/TestSignals.h"

#include "core/engine/ThunderEngine.h"
#include "core/presets/PresetDocument.h"
#include "core/presets/PresetLibrary.h"
#include "core/state/UserSettings.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <set>

using namespace thunder;
using namespace thunder::presets;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderPresetTests-" + juce::Uuid().toString());

    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

juce::String presetText (const juce::String& params, const juce::String& version = "1")
{
    return "<ThunderPreset stateVersion=\"" + version +
           "\" createdWith=\"0.5.0\">"
           "<Meta name=\"Test\"/><Params>" +
           params + "</Params></ThunderPreset>";
}

ReadReport readText (const juce::String& text, Preset& out)
{
    return read (text, out);
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

// A processor whose presets and settings live in a temporary folder.
struct TestPlugin
{
    TempFolder temp;
    ThunderProcessor processor;

    TestPlugin()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
    }

    PresetManager& presets() { return processor.getPresetManager(); }
};

ParamSnapshot snapshotOf (const Preset& preset)
{
    auto s = ParamSnapshot::defaults();
    for (const auto& def : params::kParamDefs)
        s.set (def.param, preset.valueOr (def.param));
    return s;
}
} // namespace

// ---------------------------------------------------------------------------
// The format and its reader (FR-PRESET-002/009, QA-STATE-002)
// ---------------------------------------------------------------------------

TEST_CASE ("A preset survives writing and reading back", "[unit][presets]")
{
    std::array<float, params::kNumParams> values{};
    for (const auto& def : params::kParamDefs)
    {
        // Somewhere inside each range, off the default.
        auto v = def.min + 0.37f * (def.max - def.min);
        if (def.kind != params::Kind::Float)
            v = std::round (v);
        values[params::toIndex (def.param)] = v;
    }

    Metadata meta;
    meta.name = "  Night Static  ";
    meta.author = "Someone";
    meta.category = "Atmos";
    meta.tags = "dark,wide";
    meta.description = "Line one. \"Quoted\" & <angled>.";

    const auto text = write (Preset::complete (meta, values), "0.5.0");
    CHECK (text.contains ("<P id=\"spaceMode\" v=\"")); // choices by name
    const auto choiceByName =
        text.contains ("v=\"Plate\"") || text.contains ("v=\"Room\"") || text.contains ("v=\"Cloud\"");
    CHECK (choiceByName);

    Preset back;
    const auto report = read (text, back);
    REQUIRE (report.isStrictlyValid());
    CHECK (report.createdWith == "0.5.0");
    CHECK (back.meta.name == "Night Static"); // trimmed
    CHECK (back.meta.description == meta.description);

    for (const auto& def : params::kParamDefs)
    {
        INFO (def.id);

        // Morph's amount belongs to the project's sound B, not to a preset.
        if (!isPresetParameter (def.param))
        {
            CHECK_FALSE (back.values[params::toIndex (def.param)].has_value());
            CHECK_FALSE (text.contains (juce::String (std::string (def.id))));
            continue;
        }

        REQUIRE (back.values[params::toIndex (def.param)].has_value());
        CHECK_THAT (*back.values[params::toIndex (def.param)],
                    WithinAbs (values[params::toIndex (def.param)], 1.0e-4 * (def.max - def.min)));
    }
}

TEST_CASE ("Preset metadata is trimmed and limited both ways", "[unit][presets]")
{
    Metadata m;
    m.name = juce::String::repeatedString ("n", 300);
    m.description = juce::String::repeatedString ("d", 3000);
    const auto clean = sanitise (m);
    CHECK (clean.name.length() == kMaxFieldLength);
    CHECK (clean.description.length() == kMaxDescriptionLength);

    // A hand-made file with oversized fields is cut on the way in.
    Preset p;
    const auto text = "<ThunderPreset stateVersion=\"1\"><Meta name=\"" + juce::String::repeatedString ("x", 500) +
                      "\"/><Params/></ThunderPreset>";
    REQUIRE (read (text, p).ok());
    CHECK (p.meta.name.length() == kMaxFieldLength);
}

TEST_CASE ("A preset's missing parameters take their defaults; unknown ones are ignored", "[unit][presets]")
{
    Preset p;
    const auto report = readText (presetText ("<P id=\"grainAmount\" v=\"70\"/>"
                                              "<P id=\"notAParameter\" v=\"1\"/>"
                                              "<P id=\"spaceMode\" v=\"Cathedral\"/>"
                                              "<P id=\"pitchSemitones\" v=\"99\"/>"
                                              "<P id=\"pitchFormant\" v=\"false\"/>"),
                                  p);
    REQUIRE (report.ok());
    CHECK (report.unknownParameters == 1);
    CHECK (report.unknownChoices == 1);
    CHECK_FALSE (report.isStrictlyValid());

    CHECK_THAT (p.valueOr (ParamId::grainAmount), WithinAbs (70.0, 1e-6));
    CHECK_THAT (p.valueOr (ParamId::spaceMode), WithinAbs (0.0, 1e-6));       // unknown choice → default (Cloud)
    CHECK_THAT (p.valueOr (ParamId::pitchSemitones), WithinAbs (24.0, 1e-6)); // clamped to its range
    CHECK_THAT (p.valueOr (ParamId::pitchFormant), WithinAbs (0.0, 1e-6));
    CHECK_THAT (p.valueOr (ParamId::mix), WithinAbs (50.0, 1e-6)); // missing → default
    CHECK_FALSE (p.values[params::toIndex (ParamId::mix)].has_value());
}

TEST_CASE ("A damaged or foreign file is refused and changes nothing", "[unit][presets]")
{
    Preset untouched;
    untouched.meta.name = "Keep me";

    auto refused = [&untouched] (const juce::String& text)
    {
        auto p = untouched;
        const auto report = read (text, p);
        CHECK_FALSE (report.ok());
        CHECK (p.meta.name == "Keep me");
        return report.result;
    };

    CHECK (refused ("") == ReadResult::notAPreset);
    CHECK (refused ("this is not xml") == ReadResult::notAPreset);
    CHECK (refused ("<SomethingElse stateVersion=\"1\"/>") == ReadResult::notAPreset);
    CHECK (refused (presetText ("<P id=\"mix\" v=\"50\"/>").substring (0, 60)) == ReadResult::notAPreset); // truncated
    CHECK (refused ("<ThunderPreset><Params/></ThunderPreset>") == ReadResult::badVersion);
    CHECK (refused (presetText ("", "abc")) == ReadResult::badVersion);
    CHECK (refused (presetText ("", "0")) == ReadResult::badVersion);
    CHECK (refused (presetText ("<P id=\"mix\" v=\"12abc\"/>")) == ReadResult::badValue);
    CHECK (refused (presetText ("<P id=\"mix\" v=\"nan\"/>")) == ReadResult::badValue);
    CHECK (refused (presetText ("<P id=\"mix\" v=\"1e999\"/>")) == ReadResult::badValue);
    CHECK (refused (presetText ("<P id=\"mix\"/>")) == ReadResult::badValue);
    CHECK (refused (presetText ("<P id=\"autoGain\" v=\"maybe\"/>")) == ReadResult::badValue);

    const auto huge = presetText ("<P id=\"mix\" v=\"50\"/>") + juce::String::repeatedString (" ", 1100 * 1024);
    CHECK (refused (huge) == ReadResult::tooLarge);

    // The message names no path and quotes nothing.
    for (const auto r : {ReadResult::tooLarge, ReadResult::notAPreset, ReadResult::badVersion, ReadResult::badValue,
                         ReadResult::unreadable})
        CHECK (describe (r).contains ("wasn't changed"));
}

TEST_CASE ("A preset from a newer Thunder loads what this version knows", "[unit][presets]")
{
    Preset p;
    const auto report = read (presetText ("<P id=\"grainAmount\" v=\"30\"/><P id=\"futureKnob\" v=\"3\"/>", "99"), p);
    REQUIRE (report.ok());
    CHECK (report.fromNewerVersion);
    CHECK_THAT (p.valueOr (ParamId::grainAmount), WithinAbs (30.0, 1e-6));
}

TEST_CASE ("A preset saved before v1.0 keeps its sound", "[unit][presets][state]")
{
    // Version 1 → 2 (PRD DEC-050): Scale Lock (new, on by default) is off for
    // it, and a reverb mode that played Cloud plays Cloud.
    Preset old;
    REQUIRE (read (presetText ("<P id=\"spaceMode\" v=\"Plate\"/><P id=\"grainPitchSpread\" v=\"4\"/>"), old).ok());
    CHECK_THAT (old.valueOr (ParamId::spaceMode), WithinAbs (0.0, 1e-6));
    CHECK_THAT (old.valueOr (ParamId::grainScaleLock), WithinAbs (0.0, 1e-6));
    CHECK_THAT (old.valueOr (ParamId::grainPitchSpread), WithinAbs (4.0, 1e-6));
    CHECK_THAT (old.valueOr (ParamId::timeOn), WithinAbs (0.0, 1e-6)); // new modules: off

    // A version 2 preset means what it says.
    Preset current;
    REQUIRE (read (presetText ("<P id=\"spaceMode\" v=\"Plate\"/>", "2"), current).ok());
    CHECK_THAT (current.valueOr (ParamId::spaceMode), WithinAbs (1.0, 1e-6));
    CHECK_THAT (current.valueOr (ParamId::grainScaleLock), WithinAbs (1.0, 1e-6));

    // What Thunder writes now is version 2.
    CHECK (write (current, "1.0.0").contains ("stateVersion=\"2\""));
}

TEST_CASE ("Oversized preset files are refused before they are read", "[unit][presets]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("Big.thunderpreset");
    juce::MemoryBlock block (static_cast<std::size_t> (kMaxFileBytes + 10), true);
    REQUIRE (file.replaceWithData (block.getData(), block.getSize()));

    Preset p;
    CHECK (readFile (file, p).result == ReadResult::tooLarge);
    CHECK (readFile (temp.folder.getChildFile ("Missing.thunderpreset"), p).result == ReadResult::unreadable);
}

// ---------------------------------------------------------------------------
// Factory presets and Init (FR-PRESET-001/011, QA-STATE-003)
// ---------------------------------------------------------------------------

TEST_CASE ("Factory presets name only real parameters and choices", "[unit][presets]")
{
    const auto& factory = factoryPresets();
    REQUIRE (factory.size() >= 10);

    std::set<juce::String> names;
    for (const auto& f : factory)
    {
        INFO (f.resourceName);
        CHECK (f.report.isStrictlyValid());
        CHECK_FALSE (f.report.fromNewerVersion);
        CHECK (f.preset.meta.name.isNotEmpty());
        CHECK (f.preset.meta.category.isNotEmpty());
        CHECK (f.preset.meta.description.isNotEmpty());
        CHECK (names.insert (f.preset.meta.name.toLowerCase()).second); // unique

        // Only what they change: nothing set to its own default.
        for (const auto& def : params::kParamDefs)
            if (const auto& v = f.preset.values[params::toIndex (def.param)])
                CHECK_FALSE (juce::exactlyEqual (*v, def.def));

        // They carry the sound, never the session.
        for (const auto id : {ParamId::keyRoot, ParamId::keyScale, ParamId::outputMode})
            CHECK_FALSE (f.preset.values[params::toIndex (id)].has_value());
    }

    // Sorted by name.
    for (std::size_t i = 1; i < factory.size(); ++i)
        CHECK (factory[i - 1].preset.meta.name.compareNatural (factory[i].preset.meta.name) < 0);
}

TEST_CASE ("Init is every parameter at its default", "[unit][presets]")
{
    const auto init = initPreset();
    CHECK (init.meta.name == "Init");
    for (const auto& def : params::kParamDefs)
        CHECK (juce::exactlyEqual (init.valueOr (def.param), def.def));
}

TEST_CASE ("Every factory preset makes sound and stays below full scale", "[unit][presets][audio]")
{
    // A short synthetic melody, each preset rendered from a fresh engine.
    std::vector<float> melody;
    for (const auto hz : {220.0, 261.63, 329.63, 392.0, 329.63, 261.63})
    {
        const auto note = test::sine (hz, 48000.0, 0.4, 0.5f);
        melody.insert (melody.end(), note.begin(), note.end());
    }

    for (const auto& f : factoryPresets())
    {
        INFO (f.preset.meta.name);
        ThunderEngine engine;
        engine.prepare (48000.0);

        auto left = melody, right = melody;
        const auto snapshot = snapshotOf (f.preset);
        for (std::size_t start = 0; start < left.size(); start += 512)
        {
            const auto n = std::min<std::size_t> (512, left.size() - start);
            float* channels[] = {left.data() + start, right.data() + start};
            engine.process (channels, static_cast<int> (n), {}, snapshot);
        }

        float peak = 0.0f;
        for (std::size_t i = 0; i < left.size(); ++i)
            peak = std::max ({peak, std::abs (left[i]), std::abs (right[i])});

        CHECK (test::energy (left, 24000, left.size()) > 10.0);
        CHECK (peak < 1.0f);
        CHECK (test::allFinite (left));
    }
}

// ---------------------------------------------------------------------------
// Safe file names and saving (FR-PRESET-010)
// ---------------------------------------------------------------------------

TEST_CASE ("Preset names become safe file names inside the folder", "[unit][presets]")
{
    TempFolder temp;
    const auto& folder = temp.folder;

    auto nameOf = [&folder] (const juce::String& name) -> juce::String
    {
        const auto f = presetFileFor (folder, name);
        return f ? f->getFileName() : juce::String ("<refused>");
    };

    CHECK (nameOf ("Night Static") == "Night Static.thunderpreset");
    CHECK (nameOf ("../../evil") == "evil.thunderpreset");
    CHECK (nameOf ("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij.thunderpreset");
    CHECK (nameOf ("Trailing dots... ") == "Trailing dots.thunderpreset");
    CHECK (nameOf ("CON") == "<refused>");
    CHECK (nameOf ("con.txt") == "<refused>");
    CHECK (nameOf ("LPT9") == "<refused>");
    CHECK (nameOf ("Console") == "Console.thunderpreset"); // only exact reserved names
    CHECK (nameOf ("  ...  ") == "<refused>");
    CHECK (nameOf ("") == "<refused>");
    CHECK (nameOf ("//") == "<refused>");
    CHECK (nameOf (juce::String::repeatedString ("x", 400)).length() == kMaxFieldLength + 14);

    if (const auto f = presetFileFor (folder, "Ok"))
        CHECK (f->isAChildOf (folder));
}

TEST_CASE ("Saving writes the whole file or nothing", "[unit][presets]")
{
    TempFolder temp;
    const auto folder = temp.folder.getChildFile ("Presets"); // created on demand
    const auto file = *presetFileFor (folder, "Mine");

    Preset a;
    a.meta.name = "Mine";
    a.values[params::toIndex (ParamId::grainAmount)] = 10.0f;
    REQUIRE (writePresetFile (file, a, "0.5.0") == SaveResult::ok);

    Preset b = a;
    b.values[params::toIndex (ParamId::grainAmount)] = 90.0f;
    REQUIRE (writePresetFile (file, b, "0.5.0") == SaveResult::ok); // replaces

    Preset back;
    REQUIRE (readFile (file, back).ok());
    CHECK_THAT (back.valueOr (ParamId::grainAmount), WithinAbs (90.0, 1e-4));
    CHECK (folder.findChildFiles (juce::File::findFiles, false, "*").size() == 1); // no temporary left behind

    // A folder that can't exist (its parent is a file).
    const auto blocker = temp.folder.getChildFile ("blocker");
    REQUIRE (blocker.create());
    CHECK (writePresetFile (blocker.getChildFile ("x.thunderpreset"), a, "0.5.0") == SaveResult::couldNotCreateFolder);
}

// ---------------------------------------------------------------------------
// Loading and saving on the plugin (FR-PRESET-005/006/007/011/014)
// ---------------------------------------------------------------------------

TEST_CASE ("A fresh plugin is Init, unmodified", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    const auto current = plugin.presets().getCurrent();
    CHECK (current.isInit());
    CHECK (current.name == "Init");
    CHECK_FALSE (current.modified);

    setValue (plugin.processor, ParamId::grainAmount, 80.0f);
    CHECK (plugin.presets().getCurrent().modified);

    setValue (plugin.processor, ParamId::grainAmount, 50.0f); // back to the loaded sound
    CHECK_FALSE (plugin.presets().getCurrent().modified);

    // Key, Scale and Output Mode aren't part of the sound.
    setValue (plugin.processor, ParamId::keyRoot, 5.0f);
    CHECK_FALSE (plugin.presets().getCurrent().modified);
}

TEST_CASE ("Loading a factory preset sets its sound and remembers it", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    setValue (plugin.processor, ParamId::widthAmount, 33.0f); // something the preset doesn't name

    const auto& listing = plugin.presets().getListing();
    REQUIRE (listing.entries.size() >= 10);
    const auto& entry = listing.entries.front();
    REQUIRE (entry.isFactory);

    REQUIRE (plugin.presets().load (entry).isEmpty());

    const auto* factory = &factoryPresets().front();
    for (const auto& def : params::kParamDefs)
    {
        if (def.param == ParamId::keyRoot || def.param == ParamId::keyScale || def.param == ParamId::outputMode)
            continue;
        INFO (def.id);
        CHECK_THAT (valueOf (plugin.processor, def.param),
                    WithinAbs (factory->preset.valueOr (def.param), 1.0e-3 * (def.max - def.min)));
    }

    const auto current = plugin.presets().getCurrent();
    CHECK (current.name == entry.name);
    CHECK (current.isFactory());
    CHECK_FALSE (current.modified);
    CHECK (plugin.processor.getSessionState().presetKey == factoryKey (entry.name));

    // Saving over a factory preset is refused.
    CHECK (plugin.presets().save() == SaveResult::isFactoryPreset);

    // Init puts everything back.
    plugin.presets().loadInit();
    CHECK_THAT (valueOf (plugin.processor, ParamId::widthAmount), WithinAbs (100.0, 1e-3));
    CHECK (plugin.presets().getCurrent().isInit());
}

TEST_CASE ("Save as writes a user preset that loads back exactly", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    setValue (plugin.processor, ParamId::grainSize, 321.0f);
    setValue (plugin.processor, ParamId::spaceMode, 0.0f);
    setValue (plugin.processor, ParamId::pitchMode, 2.0f);

    Metadata meta;
    meta.name = "My Sound";
    meta.author = "Me";
    meta.category = "Atmos";
    CHECK_FALSE (plugin.presets().wouldReplace ("My Sound"));
    REQUIRE (plugin.presets().saveAs (meta) == SaveResult::ok);
    CHECK (plugin.presets().wouldReplace ("My Sound"));
    CHECK (plugin.presets().getRememberedAuthor() == "Me");

    auto current = plugin.presets().getCurrent();
    CHECK (current.isUser());
    CHECK (current.name == "My Sound");
    CHECK_FALSE (current.modified);

    // It is listed after the factory presets.
    plugin.presets().rescanNow();
    const auto& entries = plugin.presets().getListing().entries;
    REQUIRE_FALSE (entries.empty());
    CHECK (entries.back().name == "My Sound");
    CHECK_FALSE (entries.back().isFactory);

    // Change, save over it, load Init, load it back.
    setValue (plugin.processor, ParamId::grainSize, 123.0f);
    CHECK (plugin.presets().getCurrent().modified);
    REQUIRE (plugin.presets().save() == SaveResult::ok);
    CHECK_FALSE (plugin.presets().getCurrent().modified);

    plugin.presets().loadInit();
    CHECK_THAT (valueOf (plugin.processor, ParamId::grainSize), WithinAbs (80.0, 0.01));
    REQUIRE (plugin.presets().load (entries.back()).isEmpty());
    CHECK_THAT (valueOf (plugin.processor, ParamId::grainSize), WithinAbs (123.0, 0.05));
    CHECK_THAT (valueOf (plugin.processor, ParamId::pitchMode), WithinAbs (2.0, 1e-6));
    CHECK (plugin.presets().getLoadedMetadata().author == "Me");

    CHECK (plugin.presets().saveAs (Metadata{"CON", {}, {}, {}, {}}) == SaveResult::invalidName);
}

TEST_CASE ("Loading keeps Key, Scale and Output Mode unless told otherwise", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    setValue (plugin.processor, ParamId::keyRoot, 4.0f);    // E
    setValue (plugin.processor, ParamId::outputMode, 1.0f); // Layer Only
    REQUIRE (plugin.presets().saveAs (Metadata{"In E", {}, {}, {}, {}}) == SaveResult::ok);
    const auto file = *plugin.presets().fileForName ("In E");

    setValue (plugin.processor, ParamId::keyRoot, 7.0f); // G
    setValue (plugin.processor, ParamId::outputMode, 0.0f);

    CHECK (plugin.presets().getPreserveSession());
    REQUIRE (plugin.presets().loadFile (file).isEmpty());
    CHECK_THAT (valueOf (plugin.processor, ParamId::keyRoot), WithinAbs (7.0, 1e-6));
    CHECK_THAT (valueOf (plugin.processor, ParamId::outputMode), WithinAbs (0.0, 1e-6));

    plugin.presets().setPreserveSession (false);
    CHECK_FALSE (plugin.presets().getPreserveSession());
    REQUIRE (plugin.presets().loadFile (file).isEmpty());
    CHECK_THAT (valueOf (plugin.processor, ParamId::keyRoot), WithinAbs (4.0, 1e-6));
    CHECK_THAT (valueOf (plugin.processor, ParamId::outputMode), WithinAbs (1.0, 1e-6));
}

TEST_CASE ("A preset that can't be read changes nothing", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    REQUIRE (plugin.presets().load (plugin.presets().getListing().entries.front()).isEmpty());
    setValue (plugin.processor, ParamId::grainAmount, 77.0f);
    const auto before = plugin.processor.getSessionState();

    const auto bad = plugin.temp.folder.getChildFile ("Broken.thunderpreset");
    REQUIRE (bad.replaceWithText ("<ThunderPreset stateVersion=\"1\"><Params><P id=\"mix\" v=\"oops\"/></Params>"
                                  "</ThunderPreset>"));

    const auto message = plugin.presets().loadFile (bad);
    CHECK (message.isNotEmpty());
    CHECK_FALSE (message.contains ("Broken"));
    CHECK_THAT (valueOf (plugin.processor, ParamId::grainAmount), WithinAbs (77.0, 1e-3));
    CHECK (plugin.processor.getSessionState() == before);
}

TEST_CASE ("The preset arrows step through the list and wrap", "[unit][presets][plugin]")
{
    TestPlugin plugin;
    REQUIRE (plugin.presets().saveAs (Metadata{"Zz Last", {}, {}, {}, {}}) == SaveResult::ok);
    plugin.presets().loadInit();
    plugin.presets().rescanNow();
    const auto& entries = plugin.presets().getListing().entries;

    // From Init (not in the list): ► goes to the first, ◄ to the last.
    REQUIRE (plugin.presets().step (1).isEmpty());
    CHECK (plugin.presets().getCurrent().key == entries.front().key);
    REQUIRE (plugin.presets().step (-1).isEmpty());
    CHECK (plugin.presets().getCurrent().key == entries.back().key); // wrapped
    CHECK (plugin.presets().getCurrent().name == "Zz Last");
    REQUIRE (plugin.presets().step (1).isEmpty());
    CHECK (plugin.presets().getCurrent().key == entries.front().key); // wrapped back

    plugin.presets().loadInit();
    REQUIRE (plugin.presets().step (-1).isEmpty());
    CHECK (plugin.presets().getCurrent().key == entries.back().key);
}

TEST_CASE ("The loaded preset is remembered in the project", "[unit][presets][plugin][state]")
{
    juce::MemoryBlock clean, edited;
    juce::String name;
    {
        TestPlugin plugin;
        const auto entry = plugin.presets().getListing().entries[2];
        name = entry.name;
        REQUIRE (plugin.presets().load (entry).isEmpty());
        plugin.processor.getStateInformation (clean);

        setValue (plugin.processor, ParamId::grainAmount, 3.0f);
        plugin.processor.getStateInformation (edited);
    }

    {
        ThunderProcessor reopened;
        reopened.setStateInformation (clean.getData(), static_cast<int> (clean.getSize()));
        const auto current = reopened.getPresetManager().getCurrent();
        CHECK (current.name == name);
        CHECK (current.isFactory());
        CHECK_FALSE (current.modified);
        CHECK (reopened.getPresetManager().getLoadedMetadata().category.isNotEmpty());
    }

    {
        ThunderProcessor reopened;
        reopened.setStateInformation (edited.getData(), static_cast<int> (edited.getSize()));
        CHECK (reopened.getPresetManager().getCurrent().name == name);
        CHECK (reopened.getPresetManager().getCurrent().modified);
    }
}

TEST_CASE ("Switching presets rapidly while playing doesn't click", "[unit][presets][plugin][audio]")
{
    // A low sine through the plugin, switching preset every two blocks. Each
    // switch must be no sharper than the presets' own steady output.
    TestPlugin plugin;
    plugin.processor.prepareToPlay (48000.0, 512);
    const auto entries = plugin.presets().getListing().entries;

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double phase = 0.0;
    float previous = 0.0f;

    auto render = [&] (int blocks)
    {
        float worstStep = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto s = 0.3f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / 48000.0;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            plugin.processor.processBlock (buffer, midi);
            for (int i = 0; i < 512; ++i)
            {
                const auto y = buffer.getSample (0, i);
                worstStep = std::max (worstStep, std::abs (y - previous));
                previous = y;
            }
        }
        return worstStep;
    };

    // Each preset's own sharpest step, once it has settled.
    float steady = 0.0f;
    for (const auto& e : entries)
    {
        REQUIRE (plugin.presets().load (e).isEmpty());
        render (60);
        steady = std::max (steady, render (30));
    }

    float switching = 0.0f;
    for (int round = 0; round < 2; ++round)
        for (const auto& e : entries)
        {
            REQUIRE (plugin.presets().load (e).isEmpty());
            switching = std::max (switching, render (2));
        }

    INFO ("steady " << steady << ", switching " << switching);
    CHECK (switching <= steady * 1.5f + 0.02f);
}
