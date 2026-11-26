// The audio regression suite (PRD QA-AUDIO-001): every case in
// RegressionSuite.cpp rendered and compared with its stored measurements.
// One test per group, so ctest runs the groups side by side.

#include "RegressionSuite.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <set>

using namespace thunder::test::regression;

namespace
{
void checkGroup (const juce::String& group)
{
    const auto references = readReferences();
    REQUIRE (references.isObject());

    Drift drift;
    int checked = 0;
    for (const auto& c : allCases())
    {
        if (c.group != group)
            continue;

        INFO (c.name.toStdString());
        Metrics reference;
        if (!Metrics::fromVar (references[juce::Identifier (c.name)], reference))
        {
            FAIL_CHECK ("No stored reference: run ThunderTests \"[regression-write]\" and commit the file");
            continue;
        }

        const auto actual = render (c);
        const auto problems = compare (reference, actual);
        INFO (problems.joinIntoString ("\n").toStdString());
        CHECK (problems.isEmpty());
        drift.add (reference, actual, c.name);
        ++checked;
    }

    REQUIRE (checked > 0);
    // How close the group came to its tolerances (1.0 = at the limit), for the log.
    std::cout << group << ": " << checked << " cases, largest drift " << juce::String (drift.worst, 3)
              << " of the tolerance (" << drift.where << ")\n";
}
} // namespace

TEST_CASE ("Regression: engine settings", "[regression][audio]")
{
    checkGroup ("Engine settings");
}

TEST_CASE ("Regression: performance controls", "[regression][audio]")
{
    checkGroup ("Performance controls");
}

TEST_CASE ("Regression: Atmos presets", "[regression][audio]")
{
    checkGroup ("Atmos presets");
}

TEST_CASE ("Regression: Harmony presets", "[regression][audio]")
{
    checkGroup ("Harmony presets");
}

TEST_CASE ("Regression: Grit presets", "[regression][audio]")
{
    checkGroup ("Grit presets");
}

TEST_CASE ("Regression: Shimmer presets", "[regression][audio]")
{
    checkGroup ("Shimmer presets");
}

TEST_CASE ("Regression: Rhythmic presets", "[regression][audio]")
{
    checkGroup ("Rhythmic presets");
}

TEST_CASE ("Regression: Lo-Fi presets", "[regression][audio]")
{
    checkGroup ("Lo-Fi presets");
}

TEST_CASE ("Regression: Experimental presets", "[regression][audio]")
{
    checkGroup ("Experimental presets");
}

TEST_CASE ("Every regression case has a test and a stored reference, and nothing else is stored", "[regression]")
{
    const std::set<juce::String> groups{"Engine settings",  "Performance controls", "Atmos presets",
                                        "Harmony presets",  "Grit presets",         "Shimmer presets",
                                        "Rhythmic presets", "Lo-Fi presets",        "Experimental presets"};
    const auto references = readReferences();
    REQUIRE (references.isObject());

    std::set<juce::String> names;
    for (const auto& c : allCases())
    {
        INFO (c.name.toStdString());
        CHECK (groups.count (c.group) == 1);
        CHECK (names.insert (c.name).second);
        CHECK (references.hasProperty (juce::Identifier (c.name)));
    }

    for (const auto& property : references.getDynamicObject()->getProperties())
    {
        INFO (property.name.toString().toStdString());
        CHECK (names.count (property.name.toString()) == 1);
    }
}

// Rewrites the stored references from this build. Hidden: run it only after
// an intended change to the sound, then commit the file with the change.
TEST_CASE ("Write the regression references", "[.][regression-write]")
{
    std::vector<std::pair<juce::String, Metrics>> results;
    for (const auto& c : allCases())
        results.emplace_back (c.name, render (c));
    REQUIRE (writeReferences (results));
    std::cout << "Wrote " << results.size() << " references to " << outputFile().getFullPathName() << "\n";
}
