// Paula's Revenge - settings shared by every copy of the app/plugin on this computer
#pragma once

#include <JuceHeader.h>

namespace prv
{
inline juce::PropertiesFile& prefs()
{
    static juce::PropertiesFile file ([]
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Paula's Revenge Shared";
        o.filenameSuffix = ".settings";
        o.folderName = "Paula's Revenge";
        o.osxLibrarySubFolder = "Application Support";
        o.millisecondsBeforeSaving = 500;
        return o;
    }());
    return file;
}

// Folder to start file windows in: this song's own setting, else the last one used anywhere
inline juce::File startFolder (const juce::String& own, const char* key)
{
    for (const auto& path : { own, prefs().getValue (key) })
    {
        if (path.isEmpty()) continue;
        juce::File f (path);
        if (f.isDirectory()) return f;
    }
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

inline void rememberFolder (juce::String& own, const char* key, const juce::File& folder)
{
    own = folder.getFullPathName();
    prefs().setValue (key, own);
}
} // namespace prv
