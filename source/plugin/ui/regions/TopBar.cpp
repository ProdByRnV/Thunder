#include "TopBar.h"

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

namespace
{
const juce::String modifiedMark (" *");
} // namespace

// ---- NameField
// -------------------------------------------------------------------------------------------------------

PresetBar::NameField::NameField()
    : juce::Button ("Preset")
{
    setTitle ("Preset");
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
}

void PresetBar::NameField::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    paint::fillRaised (g, bounds, radius::sm, highlighted || down ? colour::bgHover : colour::bgRaised,
                       down ? colour::borderStrong : colour::borderSubtle);
    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);

    auto area = getLocalBounds().reduced (space::s2, 0);
    const auto chevronArea = area.removeFromRight (getHeight() / 2)
                                 .toFloat()
                                 .withSizeKeepingCentre (static_cast<float> (space::s2) + border::width,
                                                         static_cast<float> (space::s1) + border::width);
    juce::Path chevron;
    chevron.startNewSubPath (chevronArea.getTopLeft());
    chevron.lineTo (chevronArea.getCentreX(), chevronArea.getBottom());
    chevron.lineTo (chevronArea.getTopRight());
    g.setColour (colour::textSecondary);
    g.strokePath (chevron,
                  juce::PathStrokeType (size::iconStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The name, then "*" in the secondary colour when the sound is modified.
    auto name = getButtonText();
    const auto modified = name.endsWith (modifiedMark);
    if (modified)
        name = name.dropLastCharacters (modifiedMark.length());

    const auto font = fonts::ui (type::body);
    juce::AttributedString s;
    s.append (name, font, colour::textPrimary);
    if (modified)
        s.append (modifiedMark, font, colour::textSecondary);
    s.setJustification (juce::Justification::centredLeft);
    s.setWordWrap (juce::AttributedString::none);
    s.draw (g, area.reduced (space::s1, 0).toFloat());
}

// ---- PresetBar
// -------------------------------------------------------------------------------------------------------

PresetBar::PresetBar (PresetManager& manager)
    : presets (manager)
    , previous ("Previous preset", icons::step (false))
    , next ("Next preset", icons::step (true))
    , favourite ("Favourite", icons::star (false))
    , save ("Save preset", icons::save())
    , more ("More preset options", icons::fromPath (icons::moreDots()))
{
    setTitle ("Presets");
    previous.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (1); };
    favourite.onClick = [this] { toggleFavourite(); };
    save.onClick = [this] { requestSave(); };
    more.onClick = [this] { showMoreMenu(); };
    nameField.onClick = [this]
    {
        if (onOpenBrowser)
            onOpenBrowser();
    };

    for (auto* c : std::initializer_list<juce::Component*>{&previous, &nameField, &next, &favourite, &save, &more})
        addAndMakeVisible (c);

    refresh();
    startTimerHz (timing::uiRefreshHz);
}

PresetBar::~PresetBar() = default;

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous.setBounds (area.removeFromLeft (size::iconButton));
    more.setBounds (area.removeFromRight (size::iconButton));
    save.setBounds (area.removeFromRight (size::iconButton));
    favourite.setBounds (area.removeFromRight (size::iconButton));
    next.setBounds (area.removeFromRight (size::iconButton));
    nameField.setBounds (area.reduced (space::s1, 0));
}

void PresetBar::timerCallback()
{
    refresh();
}

void PresetBar::refresh()
{
    const auto current = presets.getCurrent();
    const auto text = current.name + (current.modified ? modifiedMark : juce::String());
    if (nameField.getButtonText() != text)
    {
        nameField.setButtonText (text);
        nameField.setDescription (current.modified ? "Modified" : juce::String());
    }

    const auto& meta = presets.getLoadedMetadata();
    nameField.setTooltip (meta.description.isNotEmpty() ? current.name + ": " + meta.description
                                                        : current.name + ": click to browse presets");

    // ☆ for the loaded preset (Init can't be a favourite).
    const auto isFavourite = !current.isInit() && presets.isFavourite (current.key);
    if (favourite.getToggleState() != isFavourite)
    {
        favourite.setToggleState (isFavourite, juce::dontSendNotification);
        favourite.setIcon (icons::star (isFavourite));
        favourite.setTooltip (isFavourite ? "Remove from favourites" : "Add to favourites");
    }
    favourite.setEnabled (!current.isInit());
}

void PresetBar::toggleFavourite()
{
    const auto current = presets.getCurrent();
    if (!current.isInit())
        presets.setFavourite (current.key, !presets.isFavourite (current.key));
    refresh();
}

void PresetBar::report (const juce::String& message)
{
    if (message.isNotEmpty() && onError)
        onError (message);
    refresh();
}

void PresetBar::stepPreset (int direction)
{
    report (presets.step (direction));
}

juce::PopupMenu PresetBar::buildMoreMenu() const
{
    juce::PopupMenu menu;
    menu.addItem (kMenuInit, "Init");
    menu.addItem (kMenuSaveAs, juce::String::fromUTF8 ("Save as\xe2\x80\xa6"));
    menu.addSeparator();
    menu.addItem (kMenuImport, juce::String::fromUTF8 ("Import\xe2\x80\xa6"));
    menu.addItem (kMenuExport, juce::String::fromUTF8 ("Export\xe2\x80\xa6"));
    menu.addItem (kMenuOpenFolder, "Open preset folder");
    return menu;
}

void PresetBar::chooseMoreMenuItem (int itemId)
{
    switch (itemId)
    {
        case kMenuInit:
            report (presets.loadInit());
            break;
        case kMenuSaveAs:
            if (onSave)
                onSave (true);
            break;
        case kMenuImport:
            launchImport();
            break;
        case kMenuExport:
            launchExport();
            break;
        case kMenuOpenFolder:
            if (!presets.openFolder())
                report ("The preset folder couldn't be created.");
            break;
        default:
            break;
    }
}

void PresetBar::showMoreMenu()
{
    buildMoreMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more),
                                   [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                                   {
                                       if (safe != nullptr && result != 0)
                                           safe->chooseMoreMenuItem (result);
                                   });
}

void PresetBar::launchImport()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import presets", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), presets::kFileWildcard);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<PresetBar> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResults().isEmpty())
                                  return;
                              const auto result = safe->presets.importFiles (fc.getResults());
                              if (safe->onInfo)
                                  safe->onInfo (result.describe());
                          });
}

void PresetBar::launchExport()
{
    const auto name = presets.getCurrent().name;
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (name + presets::kFileExtension);
    chooser = std::make_unique<juce::FileChooser> ("Export preset", start, presets::kFileWildcard);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<PresetBar> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File())
                                  return;
                              const auto result = safe->presets.exportTo (fc.getResult());
                              if (result != presets::SaveResult::ok)
                                  safe->report (presets::describe (result));
                              else if (safe->onInfo)
                                  safe->onInfo ("Preset exported");
                          });
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
    , morph (apvts)
    , keyRoot (apvts, params::ParamId::keyRoot)
    , keyScale (apvts, params::ParamId::keyScale)
    , state (apvts)
    , settingsButton ("Settings", icons::gear())
    , aboutButton ("About Thunder", icons::info())
{
    settingsButton.onClick = [this]
    {
        if (onSettings)
            onSettings();
    };
    aboutButton.onClick = [this]
    {
        if (onAbout)
            onAbout();
    };

    keyBadge.onClick = [this] { showKeyMenu(); };

    for (auto* c : std::initializer_list<juce::Component*>{&presetBar, &morph, &keyRoot, &keyScale, &keyBadge,
                                                           &settingsButton, &aboutButton})
        addAndMakeVisible (c);
}

juce::Rectangle<int> TopBar::logoBounds() const
{
    return getLocalBounds().reduced (space::s4, 0).withWidth (size::bolt + space::s2 + size::wordmarkWidth);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgPanel);
    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    // Logo: bolt glyph and wordmark (UI.md §2.1).
    auto area = logoBounds();
    const auto boltArea = area.removeFromLeft (size::bolt).withSizeKeepingCentre (size::bolt, size::bolt).toFloat();
    g.setColour (colour::accentPrimary);
    g.fillPath (icons::fitted (icons::bolt(), boltArea));

    area.removeFromLeft (space::s2);
    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::wordmark, fonts::Weight::semiBold).withExtraKerningFactor (type::wordmarkTracking));
    g.drawText ("THUNDER", area, juce::Justification::centredLeft);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText ("KEY", keyRoot.getBounds().withX (keyRoot.getX() - space::s6).withWidth (space::s6 - space::s2),
                juce::Justification::centredRight);
}

void TopBar::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && logoBounds().contains (e.getPosition()) && onAbout)
        onAbout();
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    area.removeFromLeft (size::bolt + space::s2 + size::wordmarkWidth + space::s4);
    presetBar.setBounds (
        area.removeFromLeft (size::presetBarWidth).withSizeKeepingCentre (size::presetBarWidth, size::controlHeight));
    area.removeFromLeft (space::s3);
    morph.setBounds (
        area.removeFromLeft (size::morphWidth).withSizeKeepingCentre (size::morphWidth, size::controlHeight));

    const auto centred = [] (juce::Rectangle<int> r, int w, int h) { return r.withSizeKeepingCentre (w, h); };
    aboutButton.setBounds (centred (area.removeFromRight (size::iconButton), size::iconButton, size::iconButton));
    area.removeFromRight (space::s1);
    settingsButton.setBounds (centred (area.removeFromRight (size::iconButton), size::iconButton, size::iconButton));
    area.removeFromRight (space::s4);

    keyBadge.setBounds (centred (area.removeFromRight (size::keyBadgeWidth), size::keyBadgeWidth, size::controlHeight));
    area.removeFromRight (space::s2);
    keyScale.setBounds (centred (area.removeFromRight (size::keyScaleWidth), size::keyScaleWidth, size::controlHeight));
    area.removeFromRight (space::s2);
    keyRoot.setBounds (centred (area.removeFromRight (size::keyRootWidth), size::keyRootWidth, size::controlHeight));
}

namespace
{
juce::String currentKeyName (juce::AudioProcessorValueTreeState& apvts)
{
    const auto root = static_cast<int> (apvts.getRawParameterValue (paramIdString (params::ParamId::keyRoot))->load());
    const auto scale =
        static_cast<int> (apvts.getRawParameterValue (paramIdString (params::ParamId::keyScale))->load());
    return juce::String (std::string (params::kKeyRootNames[static_cast<std::size_t> (juce::jlimit (0, 11, root))])) +
           " " +
           juce::String (std::string (params::kScaleNames[static_cast<std::size_t> (
               juce::jlimit (0, static_cast<int> (params::kScaleNames.size()) - 1, scale))]));
}
} // namespace

juce::PopupMenu TopBar::buildKeyMenu() const
{
    const auto& shown = keyBadge.getShown();
    juce::PopupMenu menu;

    if (shown.estimate.valid)
    {
        menu.addSectionHeader ("Detected from " + shown.from + ": " + KeyBadge::keyName (shown.estimate) + " (" +
                               keyBadge.getBadgeText() + ")");
        menu.addItem (kMenuUseDetected, "Use " + KeyBadge::keyName (shown.estimate));
        menu.addItem (kMenuKeepManual, "Keep " + currentKeyName (state));
    }
    else
    {
        menu.addSectionHeader (shown.listening ? "Listening for the key" : "No key detected");
    }

    menu.addSeparator();
    menu.addItem (kMenuAutoDetect, "Detect the key of the input", true, isAutoDetectOn && isAutoDetectOn());
    return menu;
}

void TopBar::chooseKeyMenuItem (int itemId)
{
    switch (itemId)
    {
        case kMenuUseDetected:
            useDetectedKey();
            break;
        case kMenuAutoDetect:
            if (setAutoDetect && isAutoDetectOn)
                setAutoDetect (!isAutoDetectOn());
            break;
        case kMenuKeepManual:
        default:
            break;
    }
}

void TopBar::showKeyMenu()
{
    buildKeyMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyBadge),
                                  [safe = juce::Component::SafePointer<TopBar> (this)] (int result)
                                  {
                                      if (safe != nullptr && result != 0)
                                          safe->chooseKeyMenuItem (result);
                                  });
}

void TopBar::useDetectedKey()
{
    const auto& e = keyBadge.getShown().estimate;
    if (!e.valid)
        return;

    const auto set = [this] (params::ParamId id, int index)
    {
        auto* p = state.getParameter (paramIdString (id));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (index)));
        p->endChangeGesture();
    };
    set (params::ParamId::keyRoot, static_cast<int> (params::enumIndex (e.root)));
    set (params::ParamId::keyScale, static_cast<int> (params::enumIndex (e.scale)));
}

} // namespace thunder::ui
