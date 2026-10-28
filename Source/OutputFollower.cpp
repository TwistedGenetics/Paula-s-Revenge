// Paula's Revenge - the Mac's default sound output, for FOLLOW MAC OUTPUT
#include "OutputFollower.h"

#if JUCE_MAC
 std::string prvMacDefaultOutputName();   // Source/mac/DefaultOutput.cpp
#endif

namespace prv
{
juce::String macDefaultOutputName()
{
   #if JUCE_MAC
    return juce::String::fromUTF8 (prvMacDefaultOutputName().c_str());
   #else
    return {};
   #endif
}
} // namespace prv
