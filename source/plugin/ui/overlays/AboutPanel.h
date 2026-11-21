#pragma once

#include "plugin/ui/components/Buttons.h"

#include <functional>

namespace thunder::ui
{

// The About panel (UI.md §10.4): the logo, the version and build date, what
// Thunder does, and a link for reporting an issue. Opened by the logo or the
// ⓘ button; Close, Esc, Enter or a click outside closes it.
class AboutPanel final : public juce::Component
{
public:
    AboutPanel();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void()> onClose;

    void close();
    juce::Rectangle<int> dialogBounds() const;

    // "Version 0.6.0 · built 21 Nov 2026"
    static juce::String versionLine();
    static inline const juce::String issuesUrl{"https://github.com/ProdByRnV/Thunder/issues"};

private:
    ThunderButton reportButton{"Report an issue"};
    ThunderButton closeButton{"Close", ThunderButton::Variant::primary};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace thunder::ui
