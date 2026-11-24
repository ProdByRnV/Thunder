#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Buttons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace thunder::ui
{

// C-PRESETBROWSER (UI.md §9.12, PRD FR-PRESET-004/012/013): a 640 × 440
// popover under the preset bar.
//
//   🔍 Search presets…            [★ Favourites] [Rescan] [Open folder]
//   CATEGORIES / BANKS / TAGS │ NAME  CATEGORY  BANK  AUTHOR  ★
//                             │ …
//                             │ Tags · description
//                             │ [Save as…] [Rename] [Import…] [Export…] [Delete]
//                             │ footer: unreadable files, an incomplete list, an empty library
//
// - A click previews (loads); Enter or a double-click loads and closes; Esc
//   goes back to the sound from before the browser opened and closes. A click
//   outside closes, keeping what is loaded.
// - Arrow keys move the highlight without loading.
// - The search, filters and sort are the preset manager's, so ◄ ► step
//   through the same list after the browser closes.
// - Rename and Delete ask in place (factory presets can't be changed).
class PresetBrowser final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit PresetBrowser (PresetManager& presets);
    ~PresetBrowser() override;

    // Where the popover sits (the editor passes the area under the preset bar).
    void setAnchor (juce::Point<int> topLeft);

    std::function<void()> onClose;
    std::function<void()> onSaveAs;
    std::function<void (const juce::String& message, bool isError)> onMessage;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    // ---- What the controls do (also for tests) --------------------------------------------------------------------

    void setSearch (const juce::String& text);
    void setCategory (const juce::String& category); // empty = all
    void setBank (const juce::String& bankId);       // empty = all
    void setTag (const juce::String& tag);           // empty = all
    void setFavouritesOnly (bool only);
    void sortBy (presets::SortBy by, bool descending);

    int getNumRows() const noexcept { return static_cast<int> (rows.size()); }
    const presets::Entry& getRow (int row) const { return rows[static_cast<std::size_t> (row)]; }
    int getHighlightedRow() const;

    void highlightRow (int row);    // arrow keys: no loading
    void previewRow (int row);      // a click: loads
    void confirmRow (int row);      // Enter or double-click: loads and closes
    void toggleFavourite (int row); // the ★ column
    void revertAndClose();          // Esc
    void close();                   // a click outside: keeps what is loaded

    void rescan();
    void openFolder();
    void startRename();
    bool confirmRename (const juce::String& newName);
    void startDelete();
    bool confirmDelete();
    void cancelPrompt();
    void importFiles (const juce::Array<juce::File>& files); // the Import… dialog's result
    void exportTo (const juce::File& file);                  // the Export… dialog's result

    juce::String getFooterText() const;
    juce::String getDetailText() const; // tags and description of the highlighted preset
    juce::String getPromptText() const { return promptText; }
    bool isRenaming() const noexcept { return prompt == Prompt::rename; }
    bool isAskingToDelete() const noexcept { return prompt == Prompt::remove; }
    ThunderButton& getRescanButton() noexcept { return rescanButton; }
    ThunderButton& getRenameButton() noexcept { return renameButton; }
    ThunderButton& getDeleteButton() noexcept { return deleteButton; }
    juce::TextEditor& getSearchField() noexcept { return search; }

    // The sidebar's rows (headers and choices), for tests.
    struct SidebarRow
    {
        enum class Kind
        {
            header,
            category,
            bank,
            tag
        };
        Kind kind = Kind::header;
        juce::String value; // empty = "All"
        juce::String label;
    };
    const std::vector<SidebarRow>& getSidebarRows() const noexcept { return sidebarRows; }
    void chooseSidebarRow (int index);

    // [MSG] Re-reads the library (after a scan) and the filter.
    void refreshList();

private:
    class Table;
    class Sidebar;

    enum class Prompt
    {
        none,
        rename,
        remove
    };

    void timerCallback() override;
    juce::Rectangle<int> panelBounds() const;
    void layoutPanel();
    void updateDetail();
    void updateButtons();
    void buildSidebar();
    void setPrompt (Prompt p, const juce::String& text = {});
    std::optional<presets::Entry> highlighted() const;
    void report (const juce::String& message, bool isError);
    void launchImport();
    void launchExport();

    PresetManager& presets;
    PresetManager::RevertPoint revertPoint;
    juce::Point<int> anchor;

    std::vector<presets::Entry> rows;
    std::vector<SidebarRow> sidebarRows;
    int seenListingVersion = -1;
    bool wasScanning = false;

    juce::TextEditor search;
    ThunderButton favouritesButton{juce::String::fromUTF8 ("\xe2\x98\x85 Favourites")};
    ThunderButton rescanButton{"Rescan", ThunderButton::Variant::ghost};
    ThunderButton folderButton{"Open folder", ThunderButton::Variant::ghost};
    std::unique_ptr<Table> table;
    std::unique_ptr<Sidebar> sidebar;
    juce::Label detail;
    ThunderButton saveAsButton{juce::String::fromUTF8 ("Save as\xe2\x80\xa6")};
    ThunderButton renameButton{"Rename"};
    ThunderButton importButton{juce::String::fromUTF8 ("Import\xe2\x80\xa6")};
    ThunderButton exportButton{juce::String::fromUTF8 ("Export\xe2\x80\xa6")};
    ThunderButton deleteButton{"Delete", ThunderButton::Variant::danger};

    // Rename / Delete, asked in place of the action buttons.
    Prompt prompt = Prompt::none;
    juce::String promptText;
    juce::TextEditor renameField;
    ThunderButton promptCancel{"Cancel"};
    ThunderButton promptConfirm{"Rename", ThunderButton::Variant::primary};
    std::optional<presets::Entry> promptEntry;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace thunder::ui
