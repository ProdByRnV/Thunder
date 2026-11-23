// Renders the editor to PNG files for a visual check of the layout (hidden:
// run with "[editorshot]"). Files go to the temp folder: Thunder-editor-*.png.

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "support/TestHost.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using namespace thunder;

namespace
{
void save (ThunderEditor& e, const juce::String& name, float scale)
{
    // The base-size content at `scale`, the way a window at that size shows it.
    auto& c = e.getContent();
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    const auto file =
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder-editor-" + name + ".png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::cout << file.getFullPathName() << "\n";
}
} // namespace

TEST_CASE ("Editor snapshot", "[.][editorshot]")
{
    ThunderProcessor processor;
    processor.setSettingsFile (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder-editorshot-settings.xml"));
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);

    for (int i = 0; i < e.getNumModules(); ++i)
    {
        e.selectModule (i);
        save (e, "module" + juce::String (i), 1.0f);
    }

    e.selectModule (2);
    save (e, "module2-200", 2.0f);

    e.selectModule (0);
    e.openSettings();
    save (e, "settings", 1.0f);
    e.getSettingsPanel()->close();

    e.openAbout();
    save (e, "about", 1.0f);

    e.getToast().show ("That preset couldn't be loaded. Your sound wasn't changed.", true);
    e.openSaveForm (true);
    save (e, "saveform", 1.0f);

    processor.editorBeingDeleted (editor.get());
}

TEST_CASE ("Editor snapshot with the visualiser playing", "[.][editorshot]")
{
    constexpr double rate = 48000.0;
    ThunderProcessor processor;
    processor.setSettingsFile (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder-editorshot-settings.xml"));
    processor.prepareToPlay (rate, 512);
    test::TestPlayHead head;
    head.bpm = 110.0;
    processor.setPlayHead (&head);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);
    e.selectModule (2);

    // Grains spread in pitch and pan, so the sparks spread out.
    auto& apvts = processor.getValueTreeState();
    for (const auto& [id, value] : {std::pair<const char*, float>{"grainPitchSpread", 7.0f},
                                    {"grainPanSpread", 80.0f},
                                    {"grainDensity", 40.0f},
                                    {"grainPosSpread", 60.0f}})
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // As the frame timer would: a frame after every block (~10.7 ms here).
    const auto frame = [&e] { e.updateVisuals (true, 512.0 / rate); };
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 7.0, rate, 512, frame);
    save (e, "visualiser-wave", 1.0f);
    save (e, "visualiser-wave-200", 2.0f);

    e.showSpectrum();
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 1.0, rate, 512, frame);
    save (e, "visualiser-spectrum", 1.0f);

    e.selectModule (5);
    test::playMelody (processor, head, 0.5, rate, 512, frame);
    save (e, "visualiser-tone", 1.0f);

    processor.setPlayHead (nullptr);
    processor.editorBeingDeleted (editor.get());
}

TEST_CASE ("Editor snapshot of the module panels' live displays", "[.][editorshot]")
{
    constexpr double rate = 48000.0;
    ThunderProcessor processor;
    processor.setSettingsFile (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder-editorshot-settings.xml"));
    processor.prepareToPlay (rate, 512);
    test::TestPlayHead head;
    processor.setPlayHead (&head);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);

    // Every module with a display on, and moving.
    auto& apvts = processor.getValueTreeState();
    for (const auto& [id, value] : {std::pair<const char*, float>{"grainPitchSpread", 7.0f},
                                    {"grainPosSpread", 60.0f},
                                    {"staticOn", 1.0f},
                                    {"staticAmount", 80.0f},
                                    {"staticRate", 3.0f},
                                    {"shapeOn", 1.0f},
                                    {"shapeDepth", 80.0f},
                                    {"duckOn", 1.0f},
                                    {"duckAmount", 60.0f},
                                    {"widthAmount", 140.0f}})
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    const auto frame = [&e] { e.updateVisuals (true, 512.0 / rate); };
    for (const auto& [index, name] : {std::pair<int, const char*>{2, "grains"},
                                      {3, "static"},
                                      {4, "space"},
                                      {6, "width"},
                                      {7, "shape"},
                                      {8, "duck"}})
    {
        e.selectModule (index);
        e.updateVisuals (true, 0.0);
        test::playMelody (processor, head, 2.3, rate, 512, frame);
        save (e, juce::String ("display-") + name, 1.0f);
    }

    processor.setPlayHead (nullptr);
    processor.editorBeingDeleted (editor.get());
}

TEST_CASE ("Editor snapshot of lightning", "[.][editorshot]")
{
    constexpr double rate = 48000.0;
    ThunderProcessor processor;
    processor.setSettingsFile (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder-editorshot-settings.xml"));
    processor.prepareToPlay (rate, 512);
    test::TestPlayHead head;
    processor.setPlayHead (&head);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);
    auto& lightning = e.getVisualiser().getLightning();

    // Play until a few bolts have struck, saving the moment just after each
    // of the first three strikes (full brightness).
    int saved = 0;
    auto seen = lightning.getStrikeCount();
    const auto frame = [&]
    {
        e.updateVisuals (true, 512.0 / rate);
        if (lightning.getStrikeCount() != seen && saved < 3)
        {
            seen = lightning.getStrikeCount();
            if (seen > 2)
                save (e, "lightning-" + juce::String (++saved), 1.0f);
        }
    };
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 6.0, rate, 512, frame);
    std::cout << "strikes: " << lightning.getStrikeCount() << "\n";

    processor.setPlayHead (nullptr);
    processor.editorBeingDeleted (editor.get());
}
