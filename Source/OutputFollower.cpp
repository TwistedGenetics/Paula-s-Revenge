// Paula's Revenge - the Mac's default sound output, for FOLLOW MAC OUTPUT
#include "OutputFollower.h"

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

namespace prv
{
juce::String macDefaultOutputName()
{
   #if JUCE_MAC
    AudioObjectPropertyAddress pa { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0 };
    AudioDeviceID dev = 0;
    UInt32 size = sizeof (dev);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &pa, 0, nullptr, &size, &dev) != noErr || dev == 0) return {};
    // the same name JUCE lists the device by (kAudioDevicePropertyDeviceNameCFString)
    AudioObjectPropertyAddress na { kAudioDevicePropertyDeviceNameCFString, kAudioObjectPropertyScopeWildcard, 0 };
    CFStringRef name = nullptr;
    size = sizeof (name);
    if (AudioObjectGetPropertyData (dev, &na, 0, nullptr, &size, &name) != noErr || name == nullptr) return {};
    const auto s = juce::String::fromCFString (name);
    CFRelease (name);
    return s;
   #else
    return {};
   #endif
}
} // namespace prv
