// Paula's Revenge - the Mac's default sound output's name (CoreAudio only, no JUCE in this file:
// the Mac headers' Point / Component would clash with JUCE's)
#include <CoreAudio/CoreAudio.h>
#include <string>

std::string prvMacDefaultOutputName()
{
    AudioObjectPropertyAddress pa { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0 };
    AudioDeviceID dev = 0;
    UInt32 size = sizeof (dev);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &pa, 0, nullptr, &size, &dev) != noErr || dev == 0) return {};
    // the same name JUCE lists the device by (kAudioDevicePropertyDeviceNameCFString)
    AudioObjectPropertyAddress na { kAudioDevicePropertyDeviceNameCFString, kAudioObjectPropertyScopeWildcard, 0 };
    CFStringRef name = nullptr;
    size = sizeof (name);
    if (AudioObjectGetPropertyData (dev, &na, 0, nullptr, &size, &name) != noErr || name == nullptr) return {};
    char buf[512] = {};
    const bool ok = CFStringGetCString (name, buf, sizeof (buf), kCFStringEncodingUTF8);
    CFRelease (name);
    return ok ? std::string (buf) : std::string();
}
