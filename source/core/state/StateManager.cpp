#include "StateManager.h"

#include "StateIds.h"

#include <array>

namespace thunder::state
{

namespace
{
// The value of parameter `id` in a PARAMETERS tree, or nullptr if it isn't there.
juce::ValueTree findParameter (const juce::ValueTree& parameters, const juce::String& id)
{
    return parameters.getChildWithProperty (ids::parameterId, id);
}

void setParameter (juce::ValueTree& parameters, const juce::String& id, double value)
{
    auto entry = findParameter (parameters, id);
    if (!entry.isValid())
    {
        entry = juce::ValueTree{ids::parameter};
        entry.setProperty (ids::parameterId, id, nullptr);
        parameters.appendChild (entry, nullptr);
    }
    entry.setProperty (ids::parameterValue, value, nullptr);
}

// Version 1 → 2 (v1.0's parameters, PRD DEC-050). State saved before v1.0 must
// sound the same afterwards:
// - Grains' Scale Lock arrives switched on, but grain pitches were never
//   quantised before, so older sounds get it switched off.
// - Space's Plate, Room and Shimmer were listed from the start but played the
//   Cloud reverb (DEC-035); they now have engines of their own, so older
//   sounds that chose one get Cloud, which is what they played.
// Every other new parameter's default reproduces the old sound.
juce::ValueTree migrateV1ToV2 (juce::ValueTree tree)
{
    auto parameters = tree.getChildWithName (ids::parameters);
    if (!parameters.isValid())
        return tree;

    if (!findParameter (parameters, "grainScaleLock").isValid())
        setParameter (parameters, "grainScaleLock", 0.0);

    if (const auto mode = findParameter (parameters, "spaceMode"); mode.isValid())
        if (static_cast<double> (mode.getProperty (ids::parameterValue)) >= 0.5)
            setParameter (parameters, "spaceMode", 0.0);

    return tree;
}

// Element i migrates version i + 1 → i + 2. Add to the end, never edit an entry
// that has shipped, and add a fixture test for the old version (QA-STATE-001).
constexpr std::array<Migration, kCurrentStateVersion - 1> kMigrations{migrateV1ToV2};
} // namespace

std::span<const Migration> stateMigrations() noexcept
{
    return kMigrations;
}

juce::ValueTree createState (const juce::ValueTree& parameters, const SessionState& session,
                             const juce::String& pluginVersion, const juce::ValueTree& source,
                             const juce::ValueTree& morphSoundB)
{
    juce::ValueTree root{ids::root};
    root.setProperty (ids::stateVersion, kCurrentStateVersion, nullptr);
    root.setProperty (ids::pluginVersion, pluginVersion, nullptr);
    root.appendChild (parameters.createCopy(), nullptr);
    auto sessionTree = session.toValueTree();
    if (source.isValid())
        sessionTree.appendChild (source.createCopy(), nullptr);
    if (morphSoundB.isValid())
        sessionTree.appendChild (morphSoundB.createCopy(), nullptr);
    root.appendChild (sessionTree, nullptr);
    return root;
}

juce::ValueTree migrate (const juce::ValueTree& tree, int fromVersion, int toVersion,
                         std::span<const Migration> migrations)
{
    auto migrated = tree.createCopy();

    for (auto version = fromVersion; version < toVersion; ++version)
    {
        const auto index = static_cast<std::size_t> (version - 1);
        jassert (version >= 1 && index < migrations.size() && migrations[index] != nullptr);

        if (version < 1 || index >= migrations.size() || migrations[index] == nullptr)
            break;

        migrated = migrations[index](migrated);
        migrated.setProperty (ids::stateVersion, version + 1, nullptr);
    }

    return migrated;
}

std::optional<LoadedState> loadState (const juce::ValueTree& saved)
{
    if (!saved.hasType (ids::root))
        return std::nullopt;

    const auto version = static_cast<int> (saved.getProperty (ids::stateVersion, 0));
    if (version < 1)
        return std::nullopt;

    LoadedState loaded;
    loaded.savedVersion = version;
    loaded.fromNewerVersion = version > kCurrentStateVersion;

    // A newer state is read as-is: everything this version knows about loads,
    // unknown parameters and properties are ignored (PRD NFR-REL-002).
    const auto current =
        loaded.fromNewerVersion ? saved : migrate (saved, version, kCurrentStateVersion, stateMigrations());

    loaded.parameters = current.getChildWithName (ids::parameters).createCopy();
    if (!loaded.parameters.isValid())
        return std::nullopt;

    const auto sessionTree = current.getChildWithName (ids::session);
    loaded.session = SessionState::fromValueTree (sessionTree);
    loaded.source = sessionTree.getChildWithName (ids::source).createCopy();
    loaded.morphSoundB = sessionTree.getChildWithName (ids::morphSoundB).createCopy();
    return loaded;
}

} // namespace thunder::state
