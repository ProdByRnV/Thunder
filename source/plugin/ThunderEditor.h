#pragma once

#include "ui/components/CaptureWidget.h"
#include "ui/overlays/SaveForm.h"
#include "ui/regions/ChainRail.h"
#include "ui/regions/ModulePanels.h"
#include "ui/regions/OutputPanel.h"
#include "ui/regions/TopBar.h"
#include "ui/theme/ThunderLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace thunder
{

class ThunderProcessor;

// Thunder's editor, the v0.5 "Functional UI" (UI.md §7, §15; PRD UI-GEN-004/006):
//
//   top bar      logo · presets · key
//   visualiser   (placeholder until the visualiser)
//   chain        IN · Pitch · Grains · Space · Tone · Width · OUT
//   module       the selected module's controls      │ output
//   bottom bar   capture                              status
//
// Everything is laid out at the base size (1040 × 680) and scaled as one
// (75–200%, fixed aspect ratio; UI.md §7.1). The editor only talks to the
// processor through parameters, the preset manager and the capture API.
class ThunderEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ThunderEditor (ThunderProcessor& processor);
    ~ThunderEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // [MSG] For the regions and for tests.
    void selectModule (int index);
    int getSelectedModule() const noexcept { return chain.getSelected(); }
    ui::ModulePanel& getModulePanel (int index) { return *modules[static_cast<std::size_t> (index)]; }
    int getNumModules() const noexcept { return static_cast<int> (modules.size()); }
    ui::ChainRail& getChain() noexcept { return chain; }
    ui::TopBar& getTopBar() noexcept { return topBar; }
    ui::CaptureWidget& getCaptureWidget() noexcept { return captureWidget; }
    ui::SaveForm* getSaveForm() noexcept { return saveForm.get(); }
    ui::Toast& getToast() noexcept { return toast; }

    void openSaveForm (bool saveAs);
    void showMessage (const juce::String& text, bool isError);

    float getScale() const noexcept { return scale; }

private:
    // Everything at base size; the editor scales it.
    class Content final : public juce::Component
    {
    public:
        explicit Content (ThunderEditor& owner);
        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        ThunderEditor& owner;
    };

    ThunderProcessor& thunderProcessor;
    ui::ThunderLookAndFeel lookAndFeel;

    Content content;
    ui::TopBar topBar;
    ui::ChainRail chain;
    std::vector<std::unique_ptr<ui::ModulePanel>> modules;
    ui::OutputPanel output;
    ui::CaptureWidget captureWidget;
    juce::Label status;
    ui::Toast toast;
    std::unique_ptr<ui::SaveForm> saveForm;
    juce::TooltipWindow tooltips;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderEditor)
};

} // namespace thunder
