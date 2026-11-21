// Renders the editor to PNG files for a visual check of the layout (hidden:
// run with "[editorshot]"). Files go to the temp folder: Thunder-editor-*.png.

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace thunder;

namespace
{
void save (juce::Component& c, const juce::String& name, float scale)
{
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
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);

    for (int i = 0; i < e.getNumModules(); ++i)
    {
        e.selectModule (i);
        save (e, "module" + juce::String (i), 1.0f);
    }

    e.selectModule (0);
    e.getToast().show ("That preset couldn't be loaded. Your sound wasn't changed.", true);
    e.openSaveForm (true);
    save (e, "saveform", 1.0f);
}
