// How long the editor takes to paint (hidden: run with "[uimeasure]").
//
// Paints the whole editor into an image at 100% and 200%, once per module,
// and prints the average time per frame: with JUCE's software renderer (every
// pixel on the CPU) and with the platform's native image type (Direct2D on
// Windows, Core Graphics on macOS).

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using namespace thunder;

namespace
{
double averagePaintMs (juce::Component& c, const juce::ImageType& type, float scale, int frames)
{
    const auto w = juce::roundToInt (static_cast<float> (c.getWidth()) * scale);
    const auto h = juce::roundToInt (static_cast<float> (c.getHeight()) * scale);
    juce::Image image (type.create (juce::Image::ARGB, w, h, true));

    const auto paintOnce = [&]
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        c.paintEntireComponent (g, true);
    };

    paintOnce(); // untimed: fonts and caches load on first use

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < frames; ++i)
        paintOnce();
    return (juce::Time::getMillisecondCounterHiRes() - start) / frames;
}
} // namespace

TEST_CASE ("Editor paint cost", "[.][uimeasure]")
{
    ThunderProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);

    constexpr int frames = 30;
    const juce::SoftwareImageType software;
    const juce::NativeImageType native;

    for (const auto& [name, type] :
         {std::pair<const char*, const juce::ImageType*>{"software", &software}, {"native", &native}})
    {
        for (const auto scale : {1.0f, 2.0f})
        {
            double total = 0.0;
            for (int i = 0; i < e.getNumModules(); ++i)
            {
                e.selectModule (i);
                total += averagePaintMs (e, *type, scale, frames);
            }
            std::cout << "Full editor repaint, " << name << ", " << juce::roundToInt (scale * 100.0f)
                      << "%: " << juce::String (total / e.getNumModules(), 2) << " ms per frame\n";
        }
    }

    processor.editorBeingDeleted (editor.get());
}
