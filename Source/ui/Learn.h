// Paula's Revenge - LEARN: the next control that moves becomes an automation knob's target
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace prv::ui
{
struct ParamLearner
{
    bool armed = false;
    int  what = -1;                                     // which knob is being learned (the caller's numbering)
    std::function<std::vector<float>()> read;           // the target's current parameter values
    std::vector<float> base;

    void arm (int which, std::function<std::vector<float>()> reader)
    {
        what = which;
        read = std::move (reader);
        base = read ? read() : std::vector<float> {};
        armed = true;
    }
    void cancel() { armed = false; what = -1; }

    // the parameter that has moved since arming (-1 = none yet); disarms when found
    int poll()
    {
        if (! armed || ! read) return -1;
        const auto now = read();
        for (size_t i = 0; i < std::min (now.size(), base.size()); ++i)
            if (std::abs (now[i] - base[i]) > 0.002f) { armed = false; return (int) i; }
        return -1;
    }
};
} // namespace prv::ui
