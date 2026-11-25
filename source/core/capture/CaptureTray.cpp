#include "CaptureTray.h"

#include "core/state/StateIds.h"

#include <algorithm>

namespace thunder::capture
{

namespace
{
const juce::Identifier kCapture{"Capture"};
const juce::Identifier kFile{"file"};
const juce::Identifier kBars{"bars"};
const juce::Identifier kSource{"source"};
const juce::Identifier kTail{"tail"};
const juce::Identifier kLoop{"loop"};

// The same names as the session's captureSource.
const char* sourceName (state::CaptureSource source) noexcept
{
    switch (source)
    {
        case state::CaptureSource::FullMix:
            return "fullMix";
        case state::CaptureSource::Input:
            return "input";
        case state::CaptureSource::Layer:
            break;
    }
    return "layer";
}

state::CaptureSource sourceFromName (const juce::String& name) noexcept
{
    for (const auto source : {state::CaptureSource::FullMix, state::CaptureSource::Input})
        if (name == sourceName (source))
            return source;
    return state::CaptureSource::Layer;
}
} // namespace

juce::String TrayEntry::describe() const
{
    juce::String text = juce::String (bars) + (bars == 1 ? " bar" : " bars");
    if (loop)
        text << " loop";
    else if (tail)
        text << " +tail";

    text << juce::String::fromUTF8 (" \xc2\xb7 ");
    switch (source)
    {
        case state::CaptureSource::FullMix:
            return text + "Mix";
        case state::CaptureSource::Input:
            return text + "Input";
        case state::CaptureSource::Layer:
            break;
    }
    return text + "Layer";
}

void CaptureTray::add (const TrayEntry& entry)
{
    remove (entry.file);
    entries.insert (entries.begin(), entry);
    if (entries.size() > static_cast<std::size_t> (kMaxEntries))
        entries.resize (static_cast<std::size_t> (kMaxEntries));
}

void CaptureTray::remove (const juce::File& file)
{
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const TrayEntry& e) { return e.file == file; }),
                   entries.end());
}

bool CaptureTray::dropMissing()
{
    const auto before = entries.size();
    entries.erase (std::remove_if (entries.begin(), entries.end(),
                                   [] (const TrayEntry& e) { return !e.file.existsAsFile(); }),
                   entries.end());
    return entries.size() != before;
}

juce::ValueTree CaptureTray::toValueTree() const
{
    if (entries.empty())
        return {};

    juce::ValueTree tree (state::ids::captureTray);
    for (const auto& e : entries)
    {
        juce::ValueTree item (kCapture);
        item.setProperty (kFile, e.file.getFullPathName(), nullptr);
        item.setProperty (kBars, e.bars, nullptr);
        item.setProperty (kSource, sourceName (e.source), nullptr);
        item.setProperty (kTail, e.tail, nullptr);
        item.setProperty (kLoop, e.loop, nullptr);
        tree.appendChild (item, nullptr);
    }
    return tree;
}

CaptureTray CaptureTray::fromValueTree (const juce::ValueTree& tree)
{
    CaptureTray tray;
    if (!tree.hasType (state::ids::captureTray))
        return tray;

    for (const auto& item : tree)
    {
        if (!item.hasType (kCapture) || tray.size() >= kMaxEntries)
            continue;

        const auto path = item.getProperty (kFile).toString();
        if (path.isEmpty() || !juce::File::isAbsolutePath (path))
            continue;

        TrayEntry e;
        e.file = juce::File (path);
        e.bars = std::clamp (static_cast<int> (item.getProperty (kBars, 0)), 0, 1024);
        e.source = sourceFromName (item.getProperty (kSource).toString());
        e.tail = static_cast<bool> (item.getProperty (kTail, false));
        e.loop = static_cast<bool> (item.getProperty (kLoop, false));
        if (e.bars > 0)
            tray.entries.push_back (e);
    }
    return tray;
}

} // namespace thunder::capture
