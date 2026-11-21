#include "ThunderEditor.h"

#include "ThunderProcessor.h"
#include "ui/theme/ThunderTheme.h"

#include <algorithm>

namespace thunder
{

ThunderEditor::ThunderEditor (ThunderProcessor& p)
    : AudioProcessorEditor (p)
    , parameterList (p)
    , captureWidget (p)
{
    addAndMakeVisible (parameterList);
    addAndMakeVisible (captureWidget);

    using namespace ui::theme;
    const auto width = std::max (size::minEditorWidth, parameterList.getWidth());
    setResizable (true, false);
    setResizeLimits (width, size::bottomBarHeight * 4, width * 3, size::paramListHeight * 4);
    setSize (width, size::paramListHeight + size::bottomBarHeight);
}

ThunderEditor::~ThunderEditor() = default;

void ThunderEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void ThunderEditor::resized()
{
    auto area = getLocalBounds();
    captureWidget.setBounds (area.removeFromBottom (ui::theme::size::bottomBarHeight));
    parameterList.setBounds (area);
}

} // namespace thunder
