#include "CaptureTrayView.h"

#include "plugin/ThunderProcessor.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// One capture: ⠿, what it is ("4 bars · Layer") and its file name. A drag
// source exactly like the capture chip.
class CaptureTrayView::Row final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    Row (CaptureTrayView& o, int index)
        : owner (o)
        , row (index)
    {
        const auto& e = owner.getEntry (row);
        setTitle (e.describe() + ", " + e.file.getFileName());
        setTooltip ("Drag into your DAW. Right-click for Show in folder and Remove from tray.");
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& e = owner.getEntry (row);
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isMouseOver() ? colour::bgHover : colour::bgRaised);
        g.fillRoundedRectangle (bounds, radius::sm);
        g.setColour (colour::borderSubtle);
        g.drawRoundedRectangle (bounds, radius::sm, border::width);

        auto text = getLocalBounds().reduced (space::s2, 0);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::body));
        g.drawText (juce::String::fromUTF8 ("\xe2\xa0\xbf"), text.removeFromLeft (space::s3),
                    juce::Justification::centredLeft);
        text.removeFromLeft (space::s1);

        g.setColour (colour::textPrimary);
        g.drawText (e.describe(), text.removeFromLeft (size::trayDescribeWidth), juce::Justification::centredLeft);
        g.setColour (colour::textSecondary);
        g.drawFittedText (e.file.getFileName(), text, juce::Justification::centredLeft, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (e.mods.isPopupMenu())
            showMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.mods.isPopupMenu() || e.getDistanceFromDragStart() <= size::dragThreshold)
            return;

        const auto file = owner.getEntry (row).file;
        if (!file.existsAsFile())
            return;
        dragStarted = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({file.getFullPathName()}, false, this);
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::DraggingHandCursor; }

private:
    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (kMenuShowInFolder, "Show in folder");
        menu.addItem (kMenuRemove, "Remove from tray");
        juce::Component::SafePointer<CaptureTrayView> safe (&owner);
        const auto index = row;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe, index] (int id)
                            {
                                if (safe != nullptr && id != 0)
                                    safe->chooseMenuItem (index, id);
                            });
    }

    CaptureTrayView& owner;
    int row;
    bool dragStarted = false;
};

CaptureTrayView::CaptureTrayView (ThunderProcessor& p)
    : processor (p)
{
    setTitle ("Capture tray");
    setWantsKeyboardFocus (true);
    refresh();
}

CaptureTrayView::~CaptureTrayView() = default;

void CaptureTrayView::refresh()
{
    entries = processor.getCaptureTray().getEntries();
    rows.clear();
    for (int i = 0; i < static_cast<int> (entries.size()); ++i)
    {
        rows.push_back (std::make_unique<Row> (*this, i));
        addAndMakeVisible (*rows.back());
    }
    resized();
    repaint();
}

void CaptureTrayView::setAnchor (juce::Rectangle<int> anchor)
{
    anchorBounds = anchor;
    resized();
}

juce::Rectangle<int> CaptureTrayView::boxBounds() const
{
    const auto numRows = std::max (1, static_cast<int> (rows.size()));
    const auto height = 2 * space::s3 + size::labelHeight + space::s2 + numRows * size::controlHeight +
                        (numRows - 1) * space::s1;
    const auto x = juce::jlimit (space::s3, std::max (space::s3, getWidth() - size::trayWidth - space::s3),
                                 anchorBounds.getRight() - size::trayWidth);
    return {x, anchorBounds.getY() - space::s2 - height, size::trayWidth, height};
}

void CaptureTrayView::paint (juce::Graphics& g)
{
    const auto box = boxBounds();
    paint::drawShadow (g, box, elevation::popover);
    g.setColour (colour::bgPanel);
    g.fillRoundedRectangle (box.toFloat(), radius::md);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (box.toFloat().reduced (border::width * 0.5f), radius::md, border::width);

    auto title = box.reduced (space::s3).removeFromTop (size::labelHeight);
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText (rows.empty() ? juce::String ("NO CAPTURES YET") : juce::String ("RECENT CAPTURES"), title,
                juce::Justification::centredLeft);
}

void CaptureTrayView::resized()
{
    auto area = boxBounds().reduced (space::s3);
    area.removeFromTop (size::labelHeight + space::s2);
    for (auto& r : rows)
    {
        r->setBounds (area.removeFromTop (size::controlHeight));
        area.removeFromTop (space::s1);
    }
}

void CaptureTrayView::mouseUp (const juce::MouseEvent& e)
{
    if (!boxBounds().contains (e.getPosition()))
        close();
}

bool CaptureTrayView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void CaptureTrayView::close()
{
    if (onClose)
        onClose();
}

void CaptureTrayView::chooseMenuItem (int row, int itemId)
{
    if (row < 0 || row >= static_cast<int> (entries.size()))
        return;

    const auto file = entries[static_cast<std::size_t> (row)].file;
    if (itemId == kMenuShowInFolder)
        file.revealToUser();
    else if (itemId == kMenuRemove)
    {
        processor.removeFromCaptureTray (file);
        juce::Component::SafePointer<CaptureTrayView> safe (this);
        juce::MessageManager::callAsync ([safe]
                                         {
                                             if (safe != nullptr)
                                                 safe->refresh();
                                         });
    }
}

} // namespace thunder::ui
