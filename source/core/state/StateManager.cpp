#include "StateManager.h"

#include "StateIds.h"

#include <array>

namespace thunder::state
{

namespace
{
// Element i migrates version i + 1 → i + 2. Add to the end, never edit an entry
// that has shipped, and add a fixture test for the old version (QA-STATE-001).
constexpr std::array<Migration, kCurrentStateVersion - 1> kMigrations{};
} // namespace

std::span<const Migration> stateMigrations() noexcept
{
    return kMigrations;
}

juce::ValueTree createState (const juce::ValueTree& parameters, const SessionState& session,
                             const juce::String& pluginVersion)
{
    juce::ValueTree root{ids::root};
    root.setProperty (ids::stateVersion, kCurrentStateVersion, nullptr);
    root.setProperty (ids::pluginVersion, pluginVersion, nullptr);
    root.appendChild (parameters.createCopy(), nullptr);
    root.appendChild (session.toValueTree(), nullptr);
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

    loaded.session = SessionState::fromValueTree (current.getChildWithName (ids::session));
    return loaded;
}

} // namespace thunder::state
