// Paula's Revenge - a stereo delay line (plug-in delay compensation)
#pragma once

#include <algorithm>
#include <vector>

namespace prv
{
struct DelayLine
{
    // allocates (call outside the audio thread): delays up to maxDelay samples
    void setMax (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 1) size <<= 1;
        l.assign ((size_t) size, 0.0f);
        r.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    int maxDelay() const { return mask; }
    void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }

    // delays L/R in place by `delay` samples (clamped to the maximum)
    void process (float* L, float* R, int n, int delay)
    {
        if (l.empty()) return;
        delay = std::clamp (delay, 0, mask);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) w] = L[i];
            r[(size_t) w] = R[i];
            const int rd = (w - delay) & mask;
            L[i] = l[(size_t) rd];
            R[i] = r[(size_t) rd];
            w = (w + 1) & mask;
        }
    }

private:
    std::vector<float> l, r;
    int mask = 0, w = 0;
};
} // namespace prv
