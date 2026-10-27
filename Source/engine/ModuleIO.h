// Paula's Revenge - module import/export (plain C++)
#pragma once

#include "Song.h"
#include <cstdint>
#include <string>
#include <vector>

namespace prv
{
struct IOResult
{
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
};

// Detects ProTracker-style .MOD or MED/OctaMED MMD0/MMD1 and loads it.
// Turn MOD/MED-style instrument volumes into C commands (see ModuleIO.cpp)
void bakeInstrumentVolumes (Song& song);

IOResult loadModule (const std::vector<uint8_t>& bytes, Song& out);

IOResult loadMOD (const std::vector<uint8_t>& bytes, Song& out);
IOResult loadMED (const std::vector<uint8_t>& bytes, Song& out);

// Saves as a 4-channel (M.K.) or 8-channel (8CHN) .MOD.
IOResult saveMOD (const Song& song, std::vector<uint8_t>& out);

// Amiga IFF 8SVX sample loader. Returns false if the data is not 8SVX.
bool load8SVX (const std::vector<uint8_t>& bytes, Instrument& out, double& sampleRate);

} // namespace prv
