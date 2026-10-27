// Paula's Revenge - effect command names and help text (plain C++)
#pragma once

#include <string>
#include <cstdint>

namespace prv
{
// Short name, e.g. "LOW-PASS"
std::string commandName (int cmd);
// One line describing what this exact command + value does, e.g. "L80: LOW-PASS CUTOFF 50% (632 HZ)"
std::string describeCommand (int cmd, uint8_t param);
// true if the command can be stored in a .MOD (0-F)
inline bool isProTrackerCommand (int cmd) { return cmd >= 0 && cmd < 16; }
} // namespace prv
