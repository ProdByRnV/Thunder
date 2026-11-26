// Saved state: round trip, versioning and migrations (PRD NFR-REL-002,
// QA-STATE-001).

#include "core/source/SourceRef.h"
#include "core/state/StateIds.h"
#include "core/state/StateManager.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder::state;

namespace
{
juce::ValueTree makeParameters()
{
    juce::ValueTree parameters{ids::parameters};
    juce::ValueTree mix{"PARAM"};
    mix.setProperty ("id", "mix", nullptr);
    mix.setProperty ("value", 72.0, nullptr);
    parameters.appendChild (mix, nullptr);
    return parameters;
}

SessionState makeSession()
{
    SessionState s;
    s.presetName = "Night Static";
    s.presetModified = true;
    s.captureBars = 8;
    s.captureSource = CaptureSource::FullMix;
    return s;
}
} // namespace

TEST_CASE ("State survives a save and load unchanged", "[unit][state]")
{
    const auto saved = createState (makeParameters(), makeSession(), "0.1.0");

    CHECK (static_cast<int> (saved.getProperty (ids::stateVersion)) == kCurrentStateVersion);
    CHECK (saved.getProperty (ids::pluginVersion).toString() == "0.1.0");

    // Through XML text, as a host stores it.
    const auto reread = juce::ValueTree::fromXml (saved.toXmlString());
    const auto loaded = loadState (reread);

    REQUIRE (loaded.has_value());
    CHECK (loaded->savedVersion == kCurrentStateVersion);
    CHECK_FALSE (loaded->fromNewerVersion);
    CHECK (loaded->parameters.isEquivalentTo (makeParameters()));
    CHECK (loaded->session == makeSession());
}

TEST_CASE ("Anything that isn't Thunder state is refused", "[unit][state]")
{
    CHECK_FALSE (loadState ({}).has_value());
    CHECK_FALSE (loadState (juce::ValueTree{"SomethingElse"}).has_value());

    // No version.
    juce::ValueTree unversioned{ids::root};
    unversioned.appendChild (makeParameters(), nullptr);
    CHECK_FALSE (loadState (unversioned).has_value());

    // No parameters.
    auto noParameters = createState (makeParameters(), {}, "0.1.0");
    noParameters.removeChild (noParameters.getChildWithName (ids::parameters), nullptr);
    CHECK_FALSE (loadState (noParameters).has_value());
}

TEST_CASE ("State from a newer Thunder loads what this version knows", "[unit][state]")
{
    auto newer = createState (makeParameters(), makeSession(), "9.0.0");
    newer.setProperty (ids::stateVersion, kCurrentStateVersion + 1, nullptr);
    newer.appendChild (juce::ValueTree{"SomethingFromTheFuture"}, nullptr);

    const auto loaded = loadState (newer);

    REQUIRE (loaded.has_value());
    CHECK (loaded->fromNewerVersion);
    CHECK (loaded->parameters.isEquivalentTo (makeParameters()));
    CHECK (loaded->session == makeSession());
}

TEST_CASE ("Migrations run in order, one version at a time", "[unit][state]")
{
    const Migration testMigrations[] = {
        [] (juce::ValueTree tree)
        {
            tree.setProperty ("steps", tree.getProperty ("steps").toString() + "a", nullptr);
            return tree;
        },
        [] (juce::ValueTree tree)
        {
            tree.setProperty ("steps", tree.getProperty ("steps").toString() + "b", nullptr);
            return tree;
        },
    };

    const auto original = createState (makeParameters(), {}, "0.1.0");
    const auto migrated = migrate (original, 1, 3, testMigrations);

    CHECK (migrated.getProperty ("steps").toString() == "ab");
    CHECK (static_cast<int> (migrated.getProperty (ids::stateVersion)) == 3);
    CHECK_FALSE (original.hasProperty ("steps")); // the input is not modified

    CHECK (migrate (original, 2, 3, testMigrations).getProperty ("steps").toString() == "b");
}

TEST_CASE ("Every state version has a migration to the next", "[unit][state]")
{
    const auto migrations = stateMigrations();
    CHECK (static_cast<int> (migrations.size()) == kCurrentStateVersion - 1);

    for (const auto m : migrations)
        CHECK (m != nullptr);
}

TEST_CASE ("Version 1 to 2: older sounds keep their reverb and free grain pitches", "[unit][state]")
{
    const auto parameterValue = [] (const juce::ValueTree& tree, const juce::String& id)
    {
        const auto p = tree.getChildWithName (ids::parameters).getChildWithProperty (ids::parameterId, id);
        return p.isValid() ? static_cast<double> (p.getProperty (ids::parameterValue)) : -1.0;
    };

    for (const auto mode : {0.0, 1.0, 2.0, 3.0})
    {
        INFO ("spaceMode " << mode);
        juce::ValueTree parameters{ids::parameters};
        juce::ValueTree space{ids::parameter};
        space.setProperty (ids::parameterId, "spaceMode", nullptr);
        space.setProperty (ids::parameterValue, mode, nullptr);
        parameters.appendChild (space, nullptr);

        auto v1 = createState (parameters, {}, "0.6.0");
        v1.setProperty (ids::stateVersion, 1, nullptr);

        const auto loaded = loadState (v1);
        REQUIRE (loaded.has_value());
        CHECK (loaded->savedVersion == 1);

        juce::ValueTree migrated{ids::root};
        migrated.appendChild (loaded->parameters.createCopy(), nullptr);
        CHECK_THAT (parameterValue (migrated, "spaceMode"),
                    Catch::Matchers::WithinAbs (0.0, 1e-9)); // Plate, Room, Shimmer played Cloud
        CHECK_THAT (parameterValue (migrated, "grainScaleLock"), Catch::Matchers::WithinAbs (0.0, 1e-9)); // added, off
    }

    // Without parameters there is nothing to change.
    juce::ValueTree bare{ids::root};
    CHECK (stateMigrations()[0](bare).isEquivalentTo (bare));
}

TEST_CASE ("Damaged session settings fall back to defaults", "[unit][state]")
{
    juce::ValueTree tree{ids::session};
    tree.setProperty (ids::captureBars, 3, nullptr); // not 1/2/4/8/16
    tree.setProperty (ids::captureSource, "elsewhere", nullptr);

    const auto session = SessionState::fromValueTree (tree);
    CHECK (session.captureBars == 4);
    CHECK (session.captureSource == CaptureSource::Layer);
    CHECK (session.presetName.isEmpty());

    CHECK (SessionState::fromValueTree ({}) == SessionState{});
}

TEST_CASE ("The project state is binary and keeps a source copy as raw bytes", "[unit][state]")
{
    // A 1 MB source copy (any bytes: it is stored, not decoded).
    thunder::source::SourceRef ref;
    ref.path = "C:/melodies/lead.wav";
    auto copy = std::make_shared<juce::MemoryBlock> (1024 * 1024);
    for (std::size_t i = 0; i < copy->getSize(); ++i)
        static_cast<char*> (copy->getData())[i] = static_cast<char> ((i * 7919u) >> 3);
    ref.embedded = copy;

    const auto tree = createState (juce::ValueTree ("PARAMETERS"), SessionState{}, "1.0.0", ref.toValueTree(), {}, {});

    juce::MemoryBlock saved;
    writeBinaryState (tree, saved);
    CHECK (saved.getSize() < copy->getSize() + 4096); // as XML it was a third larger

    const auto read = readBinaryState (saved.getData(), saved.getSize());
    REQUIRE (read.isValid());
    CHECK (read.isEquivalentTo (tree));
    const auto restored =
        thunder::source::SourceRef::fromValueTree (read.getChildWithName (ids::session).getChildWithName (ids::source));
    REQUIRE (restored.embedded != nullptr);
    CHECK (*restored.embedded == *copy);

    // The same tree through XML (how projects saved before v1.0 hold it).
    const auto viaXml = juce::ValueTree::fromXml (*tree.createXml());
    const auto fromXml = thunder::source::SourceRef::fromValueTree (
        viaXml.getChildWithName (ids::session).getChildWithName (ids::source));
    REQUIRE (fromXml.embedded != nullptr);
    CHECK (*fromXml.embedded == *copy);

    // Anything else is not read as a binary state.
    const juce::String text ("<ThunderState/>");
    CHECK_FALSE (readBinaryState (text.toRawUTF8(), text.getNumBytesAsUTF8()).isValid());
    CHECK_FALSE (readBinaryState (nullptr, 0).isValid());
}
