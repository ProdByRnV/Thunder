#pragma once

#include "plugin/ui/ParamUi.h"

#include <functional>
#include <memory>
#include <vector>

namespace thunder::ui
{

// R-CHAIN (UI.md §8.3) as the v0.5 "plain buttons" version: IN, one node per
// module in processing order, OUT, on a wire. Clicking a node shows that
// module; its dot switches the module on or off (§9.3).
class ChainRail final : public juce::Component
{
public:
    struct Module
    {
        juce::String name;
        params::ParamId onParam;
    };

    ChainRail (juce::AudioProcessorValueTreeState& apvts, std::vector<Module> modules);
    ~ChainRail() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void (int)> onSelect;
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }

    // [MSG] As a click on node `index`'s body or dot would.
    void clickNode (int index);
    void clickDot (int index);

private:
    class Node;

    std::vector<std::unique_ptr<Node>> nodes;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE (ChainRail)
};

} // namespace thunder::ui
