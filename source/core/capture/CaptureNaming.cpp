#include "CaptureNaming.h"

#include "core/params/Enums.h"

#include <cmath>

namespace thunder::capture
{

namespace
{
// Short scale names for file names, in params::Scale order.
constexpr const char* kScaleTags[] = {"maj",        "min",     "harmmin", "dorian",   "phrygian",
                                      "mixolydian", "majpent", "minpent", "chromatic"};
static_assert (std::size (kScaleTags) == params::kScaleNames.size());

juce::String fromView (std::string_view text)
{
    return juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));
}
} // namespace

juce::String keyTag (int keyRoot, int keyScale)
{
    const auto root = static_cast<std::size_t> (juce::jlimit (0, 11, keyRoot));
    const auto scale =
        static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (std::size (kScaleTags)) - 1, keyScale));
    return fromView (params::kKeyRootNames[root]) + "-" + kScaleTags[scale];
}

juce::String bpmTag (double bpm)
{
    const auto rounded = std::round (bpm * 10.0) / 10.0;
    if (std::abs (rounded - std::round (rounded)) < 1.0e-9)
        return juce::String (static_cast<int> (std::round (rounded)));

    return juce::String (rounded, 1);
}

juce::String sanitiseForFileName (const juce::String& text, int maxLength)
{
    juce::String result;
    auto lastWasSeparator = true; // no leading '-'

    for (auto p = text.getCharPointer(); !p.isEmpty() && result.length() < maxLength;)
    {
        const auto c = p.getAndAdvance();

        if (juce::CharacterFunctions::isLetterOrDigit (c))
        {
            result << juce::String::charToString (c);
            lastWasSeparator = false;
        }
        else if (!lastWasSeparator)
        {
            result << "-";
            lastWasSeparator = true;
        }
    }

    return result.trimCharactersAtEnd ("-");
}

juce::String buildCaptureName (const CaptureStart& start, const juce::String& presetName, juce::Time time)
{
    juce::StringArray parts{"Thunder"};

    if (const auto preset = sanitiseForFileName (presetName); preset.isNotEmpty())
        parts.add (preset);

    parts.add (keyTag (start.keyRoot, start.keyScale));
    parts.add (bpmTag (start.bpm) + "bpm");
    parts.add (juce::String (start.bars) + (start.bars == 1 ? "bar" : "bars"));
    parts.add (time.formatted ("%Y%m%d-%H%M%S"));

    return parts.joinIntoString ("_");
}

juce::File uniqueCaptureFile (const juce::File& folder, const juce::String& name)
{
    auto isTaken = [] (const juce::File& f) { return f.exists() || partialFileFor (f).exists(); };

    auto file = folder.getChildFile (name + ".wav");
    for (int n = 2; isTaken (file); ++n)
        file = folder.getChildFile (name + "_" + juce::String (n) + ".wav");

    return file;
}

juce::File partialFileFor (const juce::File& finalFile)
{
    return juce::File (finalFile.getFullPathName() + ".partial");
}

namespace
{
// Shared by every instance in the process (the plugin binary is loaded once per process).
juce::CriticalSection& namingLock()
{
    static juce::CriticalSection lock;
    return lock;
}
} // namespace

ClaimedCaptureFile claimCaptureFile (const juce::File& folder, const juce::String& name)
{
    const juce::ScopedLock sl (namingLock());

    ClaimedCaptureFile claimed;

    // Another process may create the folder at the same moment, which makes our
    // attempt fail although the folder is there.
    if (!folder.createDirectory() && !folder.isDirectory())
        return claimed;

    claimed.finalFile = uniqueCaptureFile (folder, name);
    claimed.stream = std::make_unique<juce::FileOutputStream> (partialFileFor (claimed.finalFile));
    if (!claimed.stream->openedOk())
        claimed.stream.reset();

    return claimed;
}

juce::File finishCaptureFile (const juce::File& partial, const juce::File& wanted)
{
    const juce::ScopedLock sl (namingLock());

    // juce::File::moveFileTo deletes an existing target, so check first.
    auto target = wanted;
    if (target.exists())
        target = uniqueCaptureFile (wanted.getParentDirectory(), wanted.getFileNameWithoutExtension());

    return partial.moveFileTo (target) ? target : juce::File();
}

} // namespace thunder::capture
