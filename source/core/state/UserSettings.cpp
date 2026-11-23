#include "UserSettings.h"

#include <juce_data_structures/juce_data_structures.h>

namespace thunder::state
{

namespace
{
// Keys in settings.xml. Part of the file format: never rename one.
constexpr const char* kCapturesFolderKey = "capturesFolder";
constexpr const char* kCaptureFormatKey = "captureFormat";
constexpr const char* kPreservePresetSessionKey = "preservePresetSession";
constexpr const char* kPresetAuthorKey = "presetAuthor";
constexpr const char* kUiScaleKey = "uiScale";
constexpr const char* kShowTooltipsKey = "showTooltips";
constexpr const char* kVisualiserViewKey = "visualiserView";
constexpr const char* kVisualiserBarsKey = "visualiserBars";
constexpr const char* kShowDryKey = "visualiserShowDry";
constexpr const char* kShowGrainsKey = "visualiserShowGrains";
constexpr const char* kReduceMotionKey = "reduceMotion";
constexpr const char* kFrameRateKey = "frameRate";

constexpr const char* kFormat24 = "int24";
constexpr const char* kFormat32f = "float32";

juce::PropertiesFile::Options fileOptions()
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = -1; // only when save() is called
    return options;
}
} // namespace

juce::File UserSettings::defaultCapturesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Thunder")
        .getChildFile ("Captures");
}

juce::File UserSettings::getCapturesFolder() const
{
    return capturesFolder == juce::File() ? defaultCapturesFolder() : capturesFolder;
}

juce::File UserSettings::defaultFile()
{
    // The same places juce::PropertiesFile uses for applications.
#if JUCE_MAC
    const auto base =
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
#elif JUCE_LINUX || JUCE_BSD
    const auto base = juce::File ("~/.config");
#else
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif

    return base.getChildFile ("Thunder").getChildFile ("settings.xml");
}

UserSettings UserSettings::load (const juce::File& file)
{
    UserSettings settings;

    if (!file.existsAsFile())
        return settings;

    juce::PropertiesFile properties (file, fileOptions());

    if (const auto folder = properties.getValue (kCapturesFolderKey);
        folder.isNotEmpty() && juce::File::isAbsolutePath (folder))
        settings.capturesFolder = juce::File (folder);

    if (properties.getValue (kCaptureFormatKey) == kFormat32f)
        settings.captureFormat = capture::CaptureFormat::Float32;

    settings.preservePresetSession = properties.getBoolValue (kPreservePresetSessionKey, true);
    settings.presetAuthor = properties.getValue (kPresetAuthorKey).trim().substring (0, 128);

    // Only a scale the editor can show (75–200%); anything else means "not set".
    if (const auto scale = static_cast<float> (properties.getDoubleValue (kUiScaleKey));
        scale >= 0.75f && scale <= 2.0f)
        settings.uiScale = scale;

    settings.showTooltips = properties.getBoolValue (kShowTooltipsKey, true);
    settings.spectrumByDefault = properties.getValue (kVisualiserViewKey) == "spectrum";
    if (const auto bars = properties.getIntValue (kVisualiserBarsKey, 2); bars == 1 || bars == 2 || bars == 4)
        settings.visualiserBars = bars;
    settings.showDry = properties.getBoolValue (kShowDryKey, true);
    settings.showGrains = properties.getBoolValue (kShowGrainsKey, true);
    if (properties.containsKey (kReduceMotionKey))
    {
        settings.reduceMotion = properties.getBoolValue (kReduceMotionKey, false);
        settings.reduceMotionChosen = true;
    }
    if (properties.getIntValue (kFrameRateKey, 60) == 30)
        settings.frameRate = 30;

    return settings;
}

bool UserSettings::save (const juce::File& file) const
{
    if (!file.getParentDirectory().createDirectory())
        return false;

    juce::PropertiesFile properties (file, fileOptions());
    properties.setValue (kCapturesFolderKey,
                         capturesFolder == juce::File() ? juce::String() : capturesFolder.getFullPathName());
    properties.setValue (kCaptureFormatKey, captureFormat == capture::CaptureFormat::Float32 ? kFormat32f : kFormat24);
    properties.setValue (kPreservePresetSessionKey, preservePresetSession);
    properties.setValue (kPresetAuthorKey, presetAuthor);
    if (uiScale > 0.0f)
        properties.setValue (kUiScaleKey, static_cast<double> (uiScale));
    properties.setValue (kShowTooltipsKey, showTooltips);
    properties.setValue (kVisualiserViewKey, spectrumByDefault ? "spectrum" : "wave");
    properties.setValue (kVisualiserBarsKey, visualiserBars);
    properties.setValue (kShowDryKey, showDry);
    properties.setValue (kShowGrainsKey, showGrains);
    if (reduceMotionChosen)
        properties.setValue (kReduceMotionKey, reduceMotion);
    properties.setValue (kFrameRateKey, frameRate == 30 ? 30 : 60);
    return properties.save();
}

} // namespace thunder::state
