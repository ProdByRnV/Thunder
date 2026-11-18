// Starts JUCE's message system once for the whole test run, the way a host
// does before it creates a plugin. The processor tests need it: parameter
// state uses JUCE timers, which belong to the message thread.

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

#include <memory>

namespace
{
class JuceEnvironment final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting (const Catch::TestRunInfo&) override
    {
        juce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    }

    void testRunEnded (const Catch::TestRunStats&) override { juce.reset(); }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce;
};
} // namespace

CATCH_REGISTER_LISTENER (JuceEnvironment)
