// Load time and memory per instance (hidden: run with "[resourcemeasure]").
//
// Times what a host does when it adds Thunder to a track: create the plugin,
// prepare it (48 kHz, 256 samples) and open the editor, painted once. Memory
// is the process's private memory before and after, so the second instance
// shows the cost of one more Thunder without the one-off costs (fonts, the
// factory preset list) that the first one also carries. Windows only for the
// memory figures; the times are printed everywhere.

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "support/TestHost.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <memory>
#include <vector>

#if JUCE_WINDOWS
// windows.h first: psapi.h needs its types.
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#pragma comment(lib, "psapi.lib")
#endif

using namespace thunder;

namespace
{
double privateMegabytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof (counters);
    if (GetProcessMemoryInfo (GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&counters),
                              sizeof (counters)))
        return static_cast<double> (counters.PrivateUsage) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

struct Instance
{
    std::unique_ptr<ThunderProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};
} // namespace

TEST_CASE ("Load time and memory per instance", "[.][measure][resourcemeasure]")
{
    std::vector<Instance> instances;
    for (int n = 1; n <= 3; ++n)
    {
        const auto before = privateMegabytes();
        const auto start = juce::Time::getMillisecondCounterHiRes();

        Instance instance;
        instance.processor = std::make_unique<ThunderProcessor>();
        const auto created = juce::Time::getMillisecondCounterHiRes();
        instance.processor->prepareToPlay (48000.0, 256);
        const auto prepared = juce::Time::getMillisecondCounterHiRes();
        const auto afterPrepare = privateMegabytes();

        instance.editor.reset (instance.processor->createEditorAndMakeActive());
        {
            juce::Image image (juce::Image::ARGB, instance.editor->getWidth(), instance.editor->getHeight(), true);
            juce::Graphics g (image);
            instance.editor->paintEntireComponent (g, true);
        }
        const auto opened = juce::Time::getMillisecondCounterHiRes();
        const auto afterEditor = privateMegabytes();

        std::cout << "Instance " << n << ": create " << juce::String (created - start, 1) << " ms, prepare "
                  << juce::String (prepared - created, 1) << " ms, editor open + first paint "
                  << juce::String (opened - prepared, 1) << " ms, total " << juce::String (opened - start, 1) << " ms\n"
                  << "  memory: plugin " << juce::String (afterPrepare - before, 1) << " MB, editor "
                  << juce::String (afterEditor - afterPrepare, 1) << " MB, total "
                  << juce::String (afterEditor - before, 1) << " MB\n";
        instances.push_back (std::move (instance));
    }

    for (auto& instance : instances)
    {
        instance.processor->editorBeingDeleted (instance.editor.get());
        instance.editor.reset();
    }
}
