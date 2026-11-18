#include "ThunderProcessor.h"

// Entry point used by every plugin format wrapper. Kept apart from the
// processor so the tests can build the processor without it.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new thunder::ThunderProcessor();
}
