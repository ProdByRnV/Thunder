#pragma once

#include "ui/components/CaptureWidget.h"
#include "ui/theme/ThunderTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace thunder
{

class ThunderProcessor;

// Thunder's editor: every parameter in a list, and the capture bar along the
// bottom (UI.md R-BOTTOMBAR).
//
// TODO(PRD): the parameter list is JUCE's generic one until Thunder's own
// module panels arrive (ROADMAP T-0.5-06).
class ThunderEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ThunderEditor (ThunderProcessor& processor);
    ~ThunderEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    ui::CaptureWidget& getCaptureWidget() noexcept { return captureWidget; }

private:
    juce::GenericAudioProcessorEditor parameterList;
    ui::CaptureWidget captureWidget;
    juce::TooltipWindow tooltips{this, ui::theme::timing::tooltipDelayMs};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderEditor)
};

} // namespace thunder
