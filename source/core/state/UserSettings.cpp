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
    return properties.save();
}

} // namespace thunder::state
