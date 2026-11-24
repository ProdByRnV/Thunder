#include "PresetBrowser.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

namespace
{
const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

enum Column
{
    nameColumn = 1,
    categoryColumn,
    bankColumn,
    authorColumn,
    starColumn
};

juce::String bankLabelFor (const juce::String& bankId)
{
    if (bankId == "factory")
        return presets::kFactoryBank;
    const auto bank = bankId.fromFirstOccurrenceOf ("user:", false, false);
    return bank.isEmpty() ? juce::String ("User") : bank;
}

void styleField (juce::TextEditor& field)
{
    field.setFont (fonts::ui (type::body));
    field.setIndents (space::s2, space::s1);
    field.setInputRestrictions (presets::kMaxFieldLength);
}
} // namespace

// ---- Table
// -------------------------------------------------------------------------------------------------------------

class PresetBrowser::Table final
    : public juce::TableListBox
    , private juce::TableListBoxModel
{
public:
    explicit Table (PresetBrowser& o)
        : owner (o)
    {
        setModel (this);
        setTitle ("Presets");
        setRowHeight (size::browserRowHeight);
        setHeaderHeight (size::browserRowHeight);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

        auto& tableHeader = getHeader();
        const auto columnFlags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable;
        tableHeader.addColumn ("Name", nameColumn, size::browserColumnName, 60, -1, columnFlags);
        tableHeader.addColumn ("Category", categoryColumn, size::browserColumnCategory, 40, -1, columnFlags);
        tableHeader.addColumn ("Bank", bankColumn, size::browserColumnBank, 40, -1, columnFlags);
        tableHeader.addColumn ("Author", authorColumn, size::browserColumnAuthor, 40, -1, columnFlags);
        tableHeader.addColumn (juce::String::fromUTF8 ("\xe2\x98\x85"), starColumn, size::browserColumnStar,
                               size::browserColumnStar, size::browserColumnStar, juce::TableHeaderComponent::visible);
        tableHeader.setStretchToFitActive (true);
        tableHeader.setColour (juce::TableHeaderComponent::backgroundColourId, colour::bgPanel);
        tableHeader.setColour (juce::TableHeaderComponent::textColourId, colour::textSecondary);
        tableHeader.setColour (juce::TableHeaderComponent::outlineColourId, colour::borderSubtle);
        tableHeader.setColour (juce::TableHeaderComponent::highlightColourId, colour::bgHover);
        getViewport()->setScrollBarThickness (space::s2);
    }

    int getNumRows() override { return owner.getNumRows(); }

    void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool rowIsSelected) override
    {
        if (row < 0 || row >= owner.getNumRows())
            return;

        if (rowIsSelected)
        {
            g.setColour (colour::bgHover);
            g.fillRect (0, 0, width, height);
            g.setColour (colour::accentPrimary);
            g.fillRect (0.0f, 0.0f, size::menuSelectedBar, static_cast<float> (height));
        }
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= owner.getNumRows())
            return;

        const auto& e = owner.getRow (row);
        const auto loaded = e.key == owner.presets.getCurrent().key;
        const auto area = juce::Rectangle<int> (width, height).reduced (space::s2, 0);

        if (column == starColumn)
        {
            const auto favourite = owner.presets.isFavourite (e.key);
            g.setColour (favourite ? colour::stateWarning : colour::textDisabled);
            icons::draw (g, icons::star (favourite),
                         juce::Rectangle<int> (width, height)
                             .toFloat()
                             .withSizeKeepingCentre (size::icon * 0.75f, size::icon * 0.75f),
                         size::iconStroke);
            return;
        }

        juce::String text;
        switch (column)
        {
            case nameColumn:
                text = e.name;
                break;
            case categoryColumn:
                text = e.category;
                break;
            case bankColumn:
                text = e.bankLabel();
                break;
            case authorColumn:
                text = e.author;
                break;
            default:
                break;
        }

        g.setColour (column == nameColumn ? (loaded ? colour::accentPrimary : colour::textPrimary)
                                          : colour::textSecondary);
        g.setFont (
            fonts::ui (type::body, column == nameColumn && loaded ? fonts::Weight::semiBold : fonts::Weight::regular));
        g.drawText (text, area, juce::Justification::centredLeft, true);
    }

    void cellClicked (int row, int column, const juce::MouseEvent&) override
    {
        if (column == starColumn)
            owner.toggleFavourite (row);
        else
            owner.previewRow (row);
    }

    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override
    {
        if (column != starColumn)
            owner.confirmRow (row);
    }

    void returnKeyPressed (int row) override { owner.confirmRow (row); }

    void selectedRowsChanged (int) override { owner.updateDetail(); }

    void sortOrderChanged (int columnId, bool forwards) override
    {
        const auto by = columnId == nameColumn       ? presets::SortBy::name
                        : columnId == categoryColumn ? presets::SortBy::category
                        : columnId == bankColumn     ? presets::SortBy::bank
                        : columnId == authorColumn   ? presets::SortBy::author
                                                     : presets::SortBy::library;
        owner.sortBy (by, !forwards);
    }

    juce::String getCellTooltip (int row, int) override
    {
        if (row < 0 || row >= owner.getNumRows())
            return {};
        const auto& e = owner.getRow (row);
        return e.description.isNotEmpty() ? e.name + ": " + e.description : e.name;
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            owner.revertAndClose();
            return true;
        }
        return juce::TableListBox::keyPressed (key);
    }

private:
    PresetBrowser& owner;
};

// ---- Sidebar
// -----------------------------------------------------------------------------------------------------------

class PresetBrowser::Sidebar final
    : public juce::ListBox
    , private juce::ListBoxModel
{
public:
    explicit Sidebar (PresetBrowser& o)
        : owner (o)
    {
        setModel (this);
        setTitle ("Filters");
        setRowHeight (size::browserRowHeight);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        getViewport()->setScrollBarThickness (space::s2);
    }

    int getNumRows() override { return static_cast<int> (owner.sidebarRows.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto& r = owner.sidebarRows[static_cast<std::size_t> (row)];
        auto area = juce::Rectangle<int> (width, height).reduced (space::s2, 0);

        if (r.kind == SidebarRow::Kind::header)
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
            g.drawText (r.label, area, juce::Justification::bottomLeft);
            return;
        }

        const auto& f = owner.presets.getFilter();
        const auto chosen = (r.kind == SidebarRow::Kind::category && f.category == r.value) ||
                            (r.kind == SidebarRow::Kind::bank && f.bankId == r.value) ||
                            (r.kind == SidebarRow::Kind::tag && f.tag == r.value);
        if (chosen)
        {
            g.setColour (colour::bgHover);
            g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (2.0f, 1.0f), radius::sm);
        }

        g.setColour (chosen ? colour::accentPrimary : colour::textPrimary);
        g.setFont (fonts::ui (type::body));
        g.drawText (r.label, area, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner.chooseSidebarRow (row); }
    void returnKeyPressed (int row) override { owner.chooseSidebarRow (row); }

private:
    PresetBrowser& owner;
};

// ---- PresetBrowser
// -----------------------------------------------------------------------------------------------------

PresetBrowser::PresetBrowser (PresetManager& manager)
    : presets (manager)
    , revertPoint (manager.makeRevertPoint())
    , table (std::make_unique<Table> (*this))
    , sidebar (std::make_unique<Sidebar> (*this))
{
    setTitle ("Preset browser");
    setWantsKeyboardFocus (true);

    styleField (search);
    search.setTitle ("Search presets");
    search.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Search presets\xe2\x80\xa6"), colour::textDisabled);
    search.setText (presets.getFilter().search, false);
    search.onTextChange = [this] { setSearch (search.getText()); };
    search.onEscapeKey = [this] { revertAndClose(); };
    search.onReturnKey = [this]
    {
        if (getNumRows() > 0)
            confirmRow (std::max (0, getHighlightedRow()));
    };

    favouritesButton.setClickingTogglesState (true);
    favouritesButton.setToggleState (presets.getFilter().favouritesOnly, juce::dontSendNotification);
    favouritesButton.onClick = [this] { setFavouritesOnly (favouritesButton.getToggleState()); };
    favouritesButton.setTooltip ("Show only your favourite presets");
    rescanButton.onClick = [this] { rescan(); };
    rescanButton.setTooltip ("Read the preset folder again");
    folderButton.onClick = [this] { openFolder(); };
    folderButton.setTooltip ("Show your preset folder");

    detail.setFont (fonts::ui (type::body));
    detail.setColour (juce::Label::textColourId, colour::textSecondary);
    detail.setJustificationType (juce::Justification::topLeft);
    detail.setMinimumHorizontalScale (1.0f);
    detail.setTitle ("Preset details");

    saveAsButton.onClick = [this]
    {
        if (onSaveAs)
            onSaveAs();
    };
    renameButton.onClick = [this] { startRename(); };
    deleteButton.onClick = [this] { startDelete(); };
    importButton.onClick = [this] { launchImport(); };
    exportButton.onClick = [this] { launchExport(); };
    importButton.setTooltip ("Add presets someone sent you. Nothing in your library is replaced.");
    exportButton.setTooltip ("Save the current sound as a preset file anywhere");

    styleField (renameField);
    renameField.setTitle ("New name");
    renameField.onReturnKey = [this] { confirmRename (renameField.getText()); };
    renameField.onEscapeKey = [this] { cancelPrompt(); };
    promptCancel.onClick = [this] { cancelPrompt(); };
    promptConfirm.onClick = [this]
    {
        if (prompt == Prompt::rename)
            confirmRename (renameField.getText());
        else if (prompt == Prompt::remove)
            confirmDelete();
    };

    for (auto* c : std::initializer_list<juce::Component*>{&search, &favouritesButton, &rescanButton, &folderButton,
                                                           sidebar.get(), table.get(), &detail, &saveAsButton,
                                                           &renameButton, &importButton, &exportButton, &deleteButton})
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*>{&renameField, &promptCancel, &promptConfirm})
        addChildComponent (c);

    // The sort shown in the header is the manager's.
    const auto& sort = presets.getSort();
    const auto column = sort.by == presets::SortBy::name       ? nameColumn
                        : sort.by == presets::SortBy::category ? categoryColumn
                        : sort.by == presets::SortBy::bank     ? bankColumn
                        : sort.by == presets::SortBy::author   ? authorColumn
                                                               : 0;
    if (column != 0)
        table->getHeader().setSortColumnId (column, !sort.descending);

    refreshList();

    // Open on the loaded preset.
    const auto key = presets.getCurrent().key;
    for (int i = 0; i < getNumRows(); ++i)
        if (getRow (i).key == key)
        {
            highlightRow (i);
            table->scrollToEnsureRowIsOnscreen (i);
        }

    startTimerHz (timing::uiRefreshHz);
}

PresetBrowser::~PresetBrowser()
{
    stopTimer();
    table->setModel (nullptr);
    sidebar->setModel (nullptr);
}

void PresetBrowser::setAnchor (juce::Point<int> topLeft)
{
    anchor = topLeft;
    layoutPanel();
    repaint();
}

juce::Rectangle<int> PresetBrowser::panelBounds() const
{
    return juce::Rectangle<int> (anchor.x, anchor.y, size::browserWidth, size::browserHeight)
        .constrainedWithin (getLocalBounds());
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto panel = panelBounds();
    paint::drawShadow (g, panel, elevation::popover);
    g.setColour (colour::bgPanel);
    g.fillRoundedRectangle (panel.toFloat(), radius::md);
    g.setColour (colour::borderStrong);
    g.drawRoundedRectangle (panel.toFloat().reduced (border::width * 0.5f), radius::md, border::width);

    // The sidebar's divider and the detail area's top line.
    g.setColour (colour::borderSubtle);
    g.drawVerticalLine (sidebar->getRight() + space::s1, static_cast<float> (sidebar->getY()),
                        static_cast<float> (sidebar->getBottom()));
    g.drawHorizontalLine (detail.getY() - space::s1, static_cast<float> (table->getX()),
                          static_cast<float> (table->getRight()));

    // Delete asks here, in place of the action buttons.
    if (prompt == Prompt::remove)
    {
        g.setColour (colour::textPrimary);
        g.setFont (fonts::ui (type::body));
        g.drawText (promptText,
                    saveAsButton.getBounds().withRight (promptCancel.getX() - space::s2).withX (saveAsButton.getX()),
                    juce::Justification::centredLeft, true);
    }

    // Footer: only when there is something to say.
    const auto footer = getFooterText();
    if (footer.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText (footer, panel.reduced (space::s3).removeFromBottom (size::labelHeight),
                    juce::Justification::centredLeft, true);
    }
}

void PresetBrowser::resized()
{
    layoutPanel();
}

void PresetBrowser::layoutPanel()
{
    auto area = panelBounds().reduced (space::s3);

    auto top = area.removeFromTop (size::controlHeight);
    search.setBounds (top.removeFromLeft (size::browserSearchWidth));
    folderButton.setBounds (top.removeFromRight (size::browserButtonWidth));
    top.removeFromRight (space::s2);
    rescanButton.setBounds (top.removeFromRight (size::browserButtonWidth));
    top.removeFromRight (space::s2);
    favouritesButton.setBounds (top.removeFromRight (size::browserButtonWidth + space::s4));
    area.removeFromTop (space::s2);

    area.removeFromBottom (size::labelHeight + space::s1); // the footer
    sidebar->setBounds (area.removeFromLeft (size::browserSidebarWidth));
    area.removeFromLeft (space::s3);

    auto details = area.removeFromBottom (size::browserDetailHeight);
    area.removeFromBottom (space::s2);
    table->setBounds (area);

    auto buttons = details.removeFromBottom (size::controlHeight);
    details.removeFromBottom (space::s1);
    detail.setBounds (details.withTrimmedTop (space::s1));

    auto actions = buttons;
    for (auto* b : {&saveAsButton, &renameButton, &importButton, &exportButton})
    {
        b->setBounds (actions.removeFromLeft (size::browserActionWidth));
        actions.removeFromLeft (space::s1);
    }
    deleteButton.setBounds (actions.removeFromRight (size::browserActionWidth));

    // The prompt row.
    auto promptRow = buttons;
    promptConfirm.setBounds (promptRow.removeFromRight (size::browserActionWidth));
    promptRow.removeFromRight (space::s1);
    promptCancel.setBounds (promptRow.removeFromRight (size::browserActionWidth));
    promptRow.removeFromRight (space::s2);
    renameField.setBounds (promptRow);
}

void PresetBrowser::mouseUp (const juce::MouseEvent& e)
{
    // A click outside the popover closes it, keeping what is loaded.
    if (e.mouseWasClicked() && !panelBounds().contains (e.getPosition()))
        close();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (prompt != Prompt::none)
            cancelPrompt();
        else
            revertAndClose();
        return true;
    }
    return false;
}

void PresetBrowser::timerCallback()
{
    const auto scanning = presets.isScanning();
    if (presets.collectScan() || presets.getListingVersion() != seenListingVersion)
        refreshList();

    if (scanning != wasScanning)
    {
        wasScanning = scanning;
        rescanButton.setButtonText (scanning ? juce::String::fromUTF8 ("Scanning\xe2\x80\xa6")
                                             : juce::String ("Rescan"));
        repaint();
    }
}

// ---- The list
// ----------------------------------------------------------------------------------------------------------

void PresetBrowser::refreshList()
{
    const auto keepKey = highlighted() ? highlighted()->key : presets.getCurrent().key;

    rows = presets.getFilteredEntries();
    seenListingVersion = presets.getListingVersion();
    buildSidebar();

    table->updateContent();
    sidebar->updateContent();

    int row = -1;
    for (int i = 0; i < getNumRows(); ++i)
        if (getRow (i).key == keepKey)
            row = i;
    if (row >= 0)
        table->selectRow (row, true, true);
    else
        table->deselectAllRows();

    table->repaint();
    sidebar->repaint();
    updateDetail();
    repaint();
}

void PresetBrowser::buildSidebar()
{
    const auto& listing = presets.getListing();
    sidebarRows.clear();

    sidebarRows.push_back ({SidebarRow::Kind::header, {}, "CATEGORIES"});
    sidebarRows.push_back ({SidebarRow::Kind::category, {}, "All"});
    for (const auto& c : presets::categoriesIn (listing))
        sidebarRows.push_back ({SidebarRow::Kind::category, c, c});

    sidebarRows.push_back ({SidebarRow::Kind::header, {}, "BANKS"});
    sidebarRows.push_back ({SidebarRow::Kind::bank, {}, "All"});
    for (const auto& b : presets::bankIdsIn (listing))
        sidebarRows.push_back ({SidebarRow::Kind::bank, b, bankLabelFor (b)});

    const auto tags = presets::tagsIn (listing);
    if (!tags.isEmpty())
    {
        sidebarRows.push_back ({SidebarRow::Kind::header, {}, "TAGS"});
        sidebarRows.push_back ({SidebarRow::Kind::tag, {}, "All"});
        for (const auto& t : tags)
            sidebarRows.push_back ({SidebarRow::Kind::tag, t, t});
    }
}

void PresetBrowser::chooseSidebarRow (int index)
{
    if (index < 0 || index >= static_cast<int> (sidebarRows.size()))
        return;

    const auto r = sidebarRows[static_cast<std::size_t> (index)];
    switch (r.kind)
    {
        case SidebarRow::Kind::category:
            setCategory (r.value);
            break;
        case SidebarRow::Kind::bank:
            setBank (r.value);
            break;
        case SidebarRow::Kind::tag:
            setTag (r.value);
            break;
        case SidebarRow::Kind::header:
            break;
    }
}

void PresetBrowser::setSearch (const juce::String& text)
{
    auto f = presets.getFilter();
    f.search = text;
    presets.setFilter (f);
    if (search.getText() != text)
        search.setText (text, false);
    refreshList();
}

void PresetBrowser::setCategory (const juce::String& category)
{
    auto f = presets.getFilter();
    f.category = category;
    presets.setFilter (f);
    refreshList();
}

void PresetBrowser::setBank (const juce::String& bankId)
{
    auto f = presets.getFilter();
    f.bankId = bankId;
    presets.setFilter (f);
    refreshList();
}

void PresetBrowser::setTag (const juce::String& tag)
{
    auto f = presets.getFilter();
    f.tag = tag;
    presets.setFilter (f);
    refreshList();
}

void PresetBrowser::setFavouritesOnly (bool only)
{
    auto f = presets.getFilter();
    f.favouritesOnly = only;
    presets.setFilter (f);
    favouritesButton.setToggleState (only, juce::dontSendNotification);
    refreshList();
}

void PresetBrowser::sortBy (presets::SortBy by, bool descending)
{
    presets.setSort ({by, descending});
    refreshList();
}

int PresetBrowser::getHighlightedRow() const
{
    return table->getSelectedRow();
}

std::optional<presets::Entry> PresetBrowser::highlighted() const
{
    const auto row = table->getSelectedRow();
    if (row < 0 || row >= getNumRows())
        return std::nullopt;
    return getRow (row);
}

void PresetBrowser::highlightRow (int row)
{
    if (row < 0 || row >= getNumRows())
        return;
    table->selectRow (row, false, true);
    updateDetail();
}

void PresetBrowser::previewRow (int row)
{
    if (row < 0 || row >= getNumRows())
        return;

    highlightRow (row);
    report (presets.load (getRow (row)), true);
    table->repaint();
}

void PresetBrowser::confirmRow (int row)
{
    if (row < 0 || row >= getNumRows())
        return;

    const auto message = presets.load (getRow (row));
    if (message.isNotEmpty())
    {
        report (message, true);
        return;
    }
    close();
}

void PresetBrowser::toggleFavourite (int row)
{
    if (row < 0 || row >= getNumRows())
        return;

    const auto key = getRow (row).key;
    presets.setFavourite (key, !presets.isFavourite (key));
    refreshList();
}

void PresetBrowser::revertAndClose()
{
    presets.revertTo (revertPoint);
    close();
}

void PresetBrowser::close()
{
    stopTimer();
    if (onClose)
        onClose();
}

// ---- Details and actions
// -----------------------------------------------------------------------------------------------

void PresetBrowser::updateDetail()
{
    detail.setText (getDetailText(), juce::dontSendNotification);
    updateButtons();
}

juce::String PresetBrowser::getDetailText() const
{
    const auto e = highlighted();
    if (!e)
        return getNumRows() == 0 ? juce::String ("No presets match.") : juce::String();

    juce::String text;
    const auto tags = e->tagList();
    if (!tags.isEmpty())
        text << "Tags: " << tags.joinIntoString (dot) << "\n";
    text << (e->description.isNotEmpty() ? e->description : juce::String ("No description."));
    return text;
}

void PresetBrowser::updateButtons()
{
    const auto e = highlighted();
    const auto editable = e && !e->isFactory;
    renameButton.setEnabled (editable);
    deleteButton.setEnabled (editable);

    const auto asking = prompt != Prompt::none;
    for (auto* b : {&saveAsButton, &renameButton, &importButton, &exportButton, &deleteButton})
        b->setVisible (!asking);
    promptCancel.setVisible (asking);
    promptConfirm.setVisible (asking);
    renameField.setVisible (prompt == Prompt::rename);
}

juce::String PresetBrowser::getFooterText() const
{
    if (promptText.isNotEmpty() && prompt == Prompt::none)
        return promptText; // the last refusal

    const auto& listing = const_cast<PresetManager&> (presets).getListing();
    juce::StringArray parts;
    if (listing.unreadableFiles > 0)
        parts.add (juce::String (listing.unreadableFiles) +
                   (listing.unreadableFiles == 1 ? " file could not be read" : " files could not be read"));
    if (listing.incomplete)
        parts.add (juce::String::fromUTF8 (
            "Your library is larger than Thunder will list \xe2\x80\x94 this list is incomplete"));
    if (listing.numUserPresets() == 0 && !presets.isScanning())
        parts.add (juce::String::fromUTF8 (
            "No user presets yet \xe2\x80\x94 Save one, or Import\xe2\x80\xa6 presets someone sent you"));
    return parts.joinIntoString (dot);
}

void PresetBrowser::setPrompt (Prompt p, const juce::String& text)
{
    prompt = p;
    promptText = text;
    if (p == Prompt::rename)
    {
        promptConfirm.setButtonText ("Rename");
        promptConfirm.setVariant (ThunderButton::Variant::primary);
    }
    else if (p == Prompt::remove)
    {
        promptConfirm.setButtonText ("Delete");
        promptConfirm.setVariant (ThunderButton::Variant::danger);
    }
    updateButtons();
    repaint();
}

void PresetBrowser::startRename()
{
    const auto e = highlighted();
    if (!e || e->isFactory)
        return;

    promptEntry = e;
    renameField.setText (e->name, false);
    setPrompt (Prompt::rename);
    renameField.grabKeyboardFocus();
    renameField.selectAll();
}

bool PresetBrowser::confirmRename (const juce::String& newName)
{
    if (prompt != Prompt::rename || !promptEntry)
        return false;

    const auto result = presets.rename (*promptEntry, newName);
    promptEntry.reset();
    setPrompt (Prompt::none, presets::describe (result));
    if (result == presets::SaveResult::ok)
        presets.waitForScan();
    refreshList();
    return result == presets::SaveResult::ok;
}

void PresetBrowser::startDelete()
{
    const auto e = highlighted();
    if (!e || e->isFactory)
        return;

    promptEntry = e;
    setPrompt (Prompt::remove, "Delete " + juce::String::fromUTF8 ("\xe2\x80\x9c") + e->name +
                                   juce::String::fromUTF8 ("\xe2\x80\x9d?"));
}

bool PresetBrowser::confirmDelete()
{
    if (prompt != Prompt::remove || !promptEntry)
        return false;

    const auto removed = presets.remove (*promptEntry);
    promptEntry.reset();
    setPrompt (Prompt::none, removed ? juce::String() : juce::String ("That preset couldn't be deleted."));
    if (removed)
        presets.waitForScan();
    refreshList();
    return removed;
}

void PresetBrowser::cancelPrompt()
{
    promptEntry.reset();
    setPrompt (Prompt::none);
    table->grabKeyboardFocus();
}

void PresetBrowser::rescan()
{
    presets.rescan();
    wasScanning = false; // the timer shows "Scanning…"
}

void PresetBrowser::openFolder()
{
    if (!presets.openFolder())
        report ("The preset folder couldn't be created.", true);
}

void PresetBrowser::importFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    const auto result = presets.importFiles (files);
    if (result.imported > 0)
        presets.waitForScan();
    refreshList();
    report (result.describe(), result.imported == 0 && (result.notPresets > 0 || result.couldNotSave > 0));
}

void PresetBrowser::exportTo (const juce::File& file)
{
    if (file == juce::File())
        return;

    const auto result = presets.exportTo (file);
    report (result == presets::SaveResult::ok ? juce::String ("Preset exported") : presets::describe (result),
            result != presets::SaveResult::ok);
}

void PresetBrowser::launchImport()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import presets", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), presets::kFileWildcard);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<PresetBrowser> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr)
                                  safe->importFiles (fc.getResults());
                          });
}

void PresetBrowser::launchExport()
{
    const auto name = presets.getCurrent().name;
    const auto start =
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ((name.isNotEmpty() ? name : juce::String ("Thunder preset")) + presets::kFileExtension);
    chooser = std::make_unique<juce::FileChooser> ("Export preset", start, presets::kFileWildcard);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<PresetBrowser> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr)
                                  safe->exportTo (fc.getResult());
                          });
}

void PresetBrowser::report (const juce::String& message, bool isError)
{
    if (message.isNotEmpty() && onMessage)
        onMessage (message, isError);
}

} // namespace thunder::ui
