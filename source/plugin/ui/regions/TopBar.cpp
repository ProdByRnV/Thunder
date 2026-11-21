#include "TopBar.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// C-ICONBUTTON (UI.md §9.7) with one of Thunder's glyphs.
class PresetBar::IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path glyph)
        : juce::Button (name)
        , icon (std::move (glyph))
    {
        setTooltip (name);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down)
        {
            g.setColour (colour::bgHover);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), radius::sm);
        }

        g.setColour (highlighted ? colour::textPrimary : colour::textSecondary);
        g.fillPath (
            icons::fitted (icon, getLocalBounds().toFloat().reduced (static_cast<float> (getHeight()) * 0.32f)));
    }

private:
    juce::Path icon;
};

PresetBar::PresetBar (PresetManager& manager)
    : presets (manager)
    , previous (std::make_unique<IconButton> ("Previous preset", icons::arrow (false)))
    , next (std::make_unique<IconButton> ("Next preset", icons::arrow (true)))
    , more (std::make_unique<IconButton> ("More preset options", icons::moreDots()))
{
    setTitle ("Presets");
    previous->onClick = [this] { stepPreset (-1); };
    next->onClick = [this] { stepPreset (1); };
    more->onClick = [this] { showMoreMenu(); };

    nameButton.onClick = [this] { showPresetList(); };
    nameButton.setTitle ("Preset");

    saveButton.setButtonText ("Save");
    saveButton.setTooltip ("Save this sound as a preset");
    saveButton.onClick = [this] { requestSave(); };

    for (auto* c :
         std::initializer_list<juce::Component*>{previous.get(), &nameButton, next.get(), &saveButton, more.get()})
        addAndMakeVisible (c);

    refresh();
    startTimerHz (timing::uiRefreshHz);
}

PresetBar::~PresetBar() = default;

void PresetBar::paint (juce::Graphics&) {}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous->setBounds (area.removeFromLeft (size::iconButton));
    more->setBounds (area.removeFromRight (size::iconButton));
    area.removeFromRight (space::s1);
    saveButton.setBounds (area.removeFromRight (size::iconButtonWidth));
    area.removeFromRight (space::s1);
    next->setBounds (area.removeFromRight (size::iconButton));
    nameButton.setBounds (area.reduced (space::s1, 0));
}

void PresetBar::timerCallback()
{
    refresh();
}

void PresetBar::refresh()
{
    const auto current = presets.getCurrent();
    const auto text = current.name + (current.modified ? " *" : "");
    if (nameButton.getButtonText() != text)
        nameButton.setButtonText (text);

    const auto& meta = presets.getLoadedMetadata();
    nameButton.setTooltip (meta.description.isNotEmpty() ? current.name + "\n" + meta.description : current.name);
}

void PresetBar::report (const juce::String& message)
{
    if (message.isNotEmpty() && onError)
        onError (message);
    refresh();
}

void PresetBar::stepPreset (int direction)
{
    presets.refresh();
    report (presets.step (direction));
}

void PresetBar::showPresetList()
{
    presets.refresh();
    const auto& listing = presets.getListing();
    const auto currentKey = presets.getCurrent().key;

    juce::PopupMenu menu;
    menu.addItem ("Init", true, presets.getCurrent().isInit(), [this] { report (presets.loadInit()); });

    bool userHeader = false;
    menu.addSectionHeader ("Factory");
    for (const auto& entry : listing.entries)
    {
        if (!entry.isFactory && !userHeader)
        {
            menu.addSectionHeader ("Your presets");
            userHeader = true;
        }

        menu.addItem (entry.name, true, entry.key == currentKey, [this, entry] { report (presets.load (entry)); });
    }

    if (listing.unreadableFiles > 0)
        menu.addItem (juce::String (listing.unreadableFiles) + (listing.unreadableFiles == 1 ? " file" : " files") +
                          " could not be read",
                      false, false, nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton));
}

void PresetBar::showMoreMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Init", [this] { report (presets.loadInit()); });
    menu.addItem (juce::String::fromUTF8 ("Save as\xe2\x80\xa6"),
                  [this]
                  {
                      if (onSave)
                          onSave (true);
                  });
    menu.addSeparator();
    const auto preserve = presets.getPreserveSession();
    menu.addItem ("Keep Key, Scale and Output Mode when loading", true, preserve,
                  [this, preserve] { presets.setPreserveSession (!preserve); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (more.get()));
}

void PresetBar::requestSave()
{
    // Factory presets and Init can't be changed: Save becomes Save as.
    if (onSave)
        onSave (!presets.getCurrent().isUser());
}

// ---- TopBar
// ------------------------------------------------------------------------------------------------------------

TopBar::TopBar (juce::AudioProcessorValueTreeState& apvts, PresetManager& presets)
    : presetBar (presets)
    , keyRoot (apvts, params::ParamId::keyRoot)
    , keyScale (apvts, params::ParamId::keyScale)
{
    keyRoot.setTooltip ("Key");
    keyScale.setTooltip ("Scale");
    addAndMakeVisible (presetBar);
    addAndMakeVisible (keyRoot);
    addAndMakeVisible (keyScale);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgPanel);
    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    // Logo: bolt glyph and wordmark (UI.md §2.1).
    auto area = getLocalBounds().reduced (space::s4, 0);
    const auto boltArea = area.removeFromLeft (size::bolt).withSizeKeepingCentre (size::bolt, size::bolt).toFloat();
    g.setColour (colour::accentPrimary);
    g.fillPath (icons::fitted (icons::bolt(), boltArea));

    area.removeFromLeft (space::s2);
    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::wordmark, fonts::Weight::semiBold).withExtraKerningFactor (type::wordmarkTracking));
    g.drawText ("THUNDER", area.removeFromLeft (size::wordmarkWidth), juce::Justification::centredLeft);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText ("KEY", keyRoot.getBounds().withX (keyRoot.getX() - space::s6).withWidth (space::s6 - space::s2),
                juce::Justification::centredRight);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    area.removeFromLeft (size::bolt + space::s2 + size::wordmarkWidth + space::s4);
    presetBar.setBounds (
        area.removeFromLeft (size::presetBarWidth).withSizeKeepingCentre (size::presetBarWidth, size::controlHeight));

    auto key = area.removeFromRight (size::keyRootWidth + space::s2 + size::keyScaleWidth);
    keyScale.setBounds (
        key.removeFromRight (size::keyScaleWidth).withSizeKeepingCentre (size::keyScaleWidth, size::controlHeight));
    key.removeFromRight (space::s2);
    keyRoot.setBounds (key.withSizeKeepingCentre (size::keyRootWidth, size::controlHeight));
}

} // namespace thunder::ui
