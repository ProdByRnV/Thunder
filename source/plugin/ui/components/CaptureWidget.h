#pragma once

#include "core/capture/CaptureStatus.h"
#include "plugin/ui/components/Buttons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace thunder
{
class ThunderProcessor;
}

namespace thunder::ui
{

// C-CAPTURE (UI.md §9.10, PRD FR-CAPT-001…008): length and source, the Arm
// button, the capture chip and the captures folder.
//
// - Arm: recording starts at the next bar once the host plays; press again to
//   disarm, or to stop a recording (nothing is saved).
// - The chip shows what is happening, then the new file. Drag it into the DAW;
//   right-click it for Save as…, Show in folder and Delete capture.
// - The folder button opens the captures folder. The format and the folder
//   are chosen in Settings.
//
// Message thread only. It polls the processor's capture status on a timer.
class CaptureWidget final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit CaptureWidget (ThunderProcessor& processor);
    ~CaptureWidget() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // [MSG] Reads the processor's status and updates every control. The timer
    // calls it; tests call it directly.
    void refresh();

    const capture::CaptureStatus& getStatus() const noexcept { return status; }
    juce::String getChipText() const;
    juce::String getArmButtonText() const { return armButton.getButtonText(); }

    // The file the chip offers for drag-out, Save as… and Show in folder: the
    // last complete capture, or nothing while a new one is under way.
    juce::File getDraggableFile() const;

    // [MSG] The actions behind the buttons and menus.
    void toggleArm();
    void showInFolder();
    void saveAs();
    void deleteCapture();
    void openCapturesFolder();

private:
    class Chip;

    void timerCallback() override;
    void writeSessionChoices();
    void showChipMenu();

    ThunderProcessor& processor;
    capture::CaptureStatus status;
    std::uint32_t seenCompleted = 0;
    juce::uint32 successUntil = 0;

    juce::Label title;
    juce::ComboBox barsBox;
    juce::ComboBox sourceBox;
    ThunderButton armButton;
    std::unique_ptr<Chip> chip;
    IconButton folderButton;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureWidget)
};

} // namespace thunder::ui
