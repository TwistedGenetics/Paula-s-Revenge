// Paula's Revenge - following an external MIDI clock (plain C++): the tempo from the 24-a-beat
// clock ticks (averaged, so a jittery clock still gives a steady tempo), and where a Song
// Position Pointer (in 16ths = lines) lands in the song.
#pragma once

#include "Song.h"
#include <algorithm>
#include <cmath>

namespace prv
{
class ClockFollower
{
public:
    void reset() { count = 0; head = 0; last = -1; smoothed = 0; }
    // a clock tick (0xF8) at time t, in seconds
    void tick (double t)
    {
        if (last >= 0)
        {
            const double d = t - last;
            if (d <= 0 || d > 0.5) { reset(); last = t; return; }   // (a gap: the clock stopped, start again)
            gaps[head] = d; head = (head + 1) % kWindow; count = std::min (count + 1, kWindow);
            double sum = 0; for (int i = 0; i < count; ++i) sum += gaps[i];
            const double bpm = 60.0 / (sum / count * 24.0);
            smoothed = smoothed <= 0 ? bpm : smoothed + (bpm - smoothed) * 0.15;
        }
        last = t;
    }
    bool locked (double now) const { return count >= 12 && last >= 0 && now - last < 0.5; }
    double bpm() const { return smoothed; }   // beats a minute (0 = not yet)
private:
    static constexpr int kWindow = 48;   // two beats of ticks
    double gaps[kWindow] {};
    int count = 0, head = 0;
    double last = -1, smoothed = 0;
};

// a Song Position Pointer (16ths from the start = lines, 4 to a beat) as a song position and line
inline bool songPositionAt (const Song& s, long sixteenths, int& seqPos, int& line)
{
    long left = std::max (0L, sixteenths);
    for (int p = 0; p < (int) s.sequence.size(); ++p)
    {
        const int b = s.safeBlock (s.sequence[(size_t) p]);
        const int n = std::max (1, s.blocks[(size_t) b].lines);
        if (left < n) { seqPos = p; line = (int) left; return true; }
        left -= n;
    }
    return false;
}
} // namespace prv
