// Paula's Revenge - effect command names and help text (plain C++)
#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace prv
{
// Short name, e.g. "LOW-PASS"
std::string commandName (int cmd);
// One line describing what this exact command + value does, e.g. "L80: LOW-PASS CUTOFF 50% (632 HZ)"
std::string describeCommand (int cmd, uint8_t param);
// The list of commands for the right-click menu: what to type, a name, a short
// overview, and a sensible value to start from.
struct CommandInfo
{
    uint8_t cmd;           // 0-F, or cmdLetter ('G'..'Z')
    uint8_t param;         // value to start with (for E commands the high digit picks the sub-command)
    bool extended;         // an Exy command: the menu writes the sub-command digit too
    const char* code;      // "C", "E9", "L"
    const char* name;      // "SET VOLUME"
    const char* overview;  // "VOLUME 00-40 (40 = FULL)"
};
const std::vector<CommandInfo>& commandList();

// (v1.01) On a CHOPPAGE track the sample-only commands mean something else: Uxy holds FX pad x for y lines, 9xx is
// that hit's pad setting, Sxx plays slice xx+1 of the break playing. Empty for any other command.
std::string choppageCommandName (int cmd);
std::string choppageOverview (int cmd);
// pad: the pad the 9xx goes with (0-7, from the U beside it or held), -1 not known
std::string describeChoppageCommand (int cmd, uint8_t param, int pad = -1);
const char* choppagePadName (int pad);   // 0-7: STUTTER ... S950 DROP

// true if the command can be stored in a .MOD (0-F)
inline bool isProTrackerCommand (int cmd) { return cmd >= 0 && cmd < 16; }
} // namespace prv
