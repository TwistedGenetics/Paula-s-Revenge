#pragma once
/*
    A CHOPPAGE preset: the whole setup in one text file, the same on the MPC and the desktop.

        CHOPPAGE PRESET 1
        name AMEN ROLLERS
        [params]
        filter 0.82
        ...                      (every core parameter by id, plain value)
        [session]
        CHOPPAGE 1               (Session::saveState: breaks by path, slices, hits, slice settings, patterns, memories)
        ...

    Breaks are found by path, or by file name in the breaks folder when the path is from another machine.
*/
#include <map>
#include <string>
#include <vector>

namespace bc::Preset
{
    constexpr const char* kExtension = ".chp";

    std::string make (const std::string& name, const float* coreParams, const std::string& sessionState);

    struct Parsed { std::string name; std::map<std::string, float> params; std::string session; };
    bool parse (const std::string& text, Parsed& out);

    /** The presets in a folder (*.chp), by name: { name, path }. */
    std::vector<std::pair<std::string, std::string>> list (const std::string& folder);

    /** A file name for a new preset in folder: "<name>.chp", or "<name> 2.chp" ... when taken. */
    std::string freePath (const std::string& folder, const std::string& name);

    bool readFile (const std::string& path, std::string& text);
    bool writeFile (const std::string& path, const std::string& text);
}
