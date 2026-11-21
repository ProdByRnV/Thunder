// Renders the editor to PNG files for a visual check of the layout (hidden:
// run with "[editorshot]"). Files go to the temp folder: Thunder-editor-*.png.

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"

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

    e.selectModule (1);
    save (e, "module1-200", 2.0f);

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
