#pragma once

#include "core/capture/CaptureTray.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace thunder
{
class ThunderProcessor;
}

namespace thunder::ui
{

// The capture tray (UI.md §9.18, PRD FR-CAPT-011): opened by the "+N" button
// beside the capture chip, a popover above the bottom bar listing the
// project's last captures, newest at the top. Drag any of them into the DAW
// like a fresh capture; right-click for Show in folder and Remove from tray.
// A click outside or Esc closes it.
class CaptureTrayView final : public juce::Component
{
public:
    explicit CaptureTrayView (ThunderProcessor& processor);
    ~CaptureTrayView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    // Where the popover sits: its bottom-left corner above `anchor` (the "+N" button).
    void setAnchor (juce::Rectangle<int> anchor);

    std::function<void()> onClose;
    void close();

    // [MSG] Rereads the tray (missing files drop out).
    void refresh();

    juce::Rectangle<int> boxBounds() const;
    int getNumRows() const noexcept { return static_cast<int> (rows.size()); }
    const capture::TrayEntry& getEntry (int row) const { return entries[static_cast<std::size_t> (row)]; }

    // [MSG] The row menu's actions.
    static constexpr int kMenuShowInFolder = 1;
    static constexpr int kMenuRemove = 2;
    void chooseMenuItem (int row, int itemId);

private:
    class Row;

    ThunderProcessor& processor;
    std::vector<capture::TrayEntry> entries;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Rectangle<int> anchorBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureTrayView)
};

} // namespace thunder::ui
