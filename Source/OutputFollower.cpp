// Paula's Revenge - the computer's default sound output, for FOLLOW SYSTEM OUTPUT
#include "OutputFollower.h"

#if JUCE_MAC
 std::string prvMacDefaultOutputName();   // Source/mac/DefaultOutput.cpp
#elif JUCE_WINDOWS
 std::string prvWinDefaultOutputName();   // Source/win/DefaultOutput.cpp
#endif

namespace prv
{
juce::String systemDefaultOutputName()
{
   #if JUCE_MAC
    return juce::String::fromUTF8 (prvMacDefaultOutputName().c_str());
   #elif JUCE_WINDOWS
    return juce::String::fromUTF8 (prvWinDefaultOutputName().c_str());
   #else
    return {};
   #endif
}
} // namespace prv
