// Saved state: round trip, versioning and migrations (PRD NFR-REL-002,
// QA-STATE-001).

#include "core/state/StateIds.h"
#include "core/state/StateManager.h"

#include <catch2/catch_test_macros.hpp>

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
