// Paula's Revenge - find slice points in a sample
#include "Slicer.h"
#include <algorithm>
#include <cmath>

namespace prv
{
void tidySlices (std::vector<int>& s, int length)
{
    s.erase (std::remove_if (s.begin(), s.end(), [length] (int p) { return p < 0 || p >= length; }), s.end());
    s.push_back (0);
    std::sort (s.begin(), s.end());
    s.erase (std::unique (s.begin(), s.end()), s.end());
    if (length <= 0) s.clear();
}

std::vector<int> evenSlices (int length, int count)
{
    std::vector<int> s;
    count = std::clamp (count, 1, 256);
    if (length <= 0) return s;
    for (int i = 0; i < count; ++i) s.push_back ((int) ((long long) i * length / count));
    tidySlices (s, length);
    return s;
}

std::vector<int> detectSlices (const std::vector<int16_t>& L, const std::vector<int16_t>& R,
                               double sampleRate, float sensitivity, int maxSlices)
{
    const int n = (int) L.size();
    std::vector<int> out { 0 };
    if (n < 64) return out;
    const bool stereo = R.size() == L.size();
    const int hop = std::max (16, (int) (sampleRate * 0.0025));      // 2.5 ms frames
    const int frames = n / hop;
    if (frames < 4) return out;

    // loudness per frame (dB)
    std::vector<double> db ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const double a = L[(size_t) i] / 32768.0, b = stereo ? R[(size_t) i] / 32768.0 : a;
            e += 0.5 * (a * a + b * b);
        }
        db[(size_t) f] = 10.0 * std::log10 (e / hop + 1e-10);
    }
    double peak = -200;
    for (double v : db) peak = std::max (peak, v);

    // a hit = loudness jumps over the recent minimum by more than the threshold
    const double s = std::clamp ((double) sensitivity, 0.0, 1.0);
    const double jump = 18.0 - 15.0 * s;          // dB rise needed (18 dB .. 3 dB)
    const double floorDb = peak - 30.0 - 25.0 * s; // ignore anything quieter than this
    const int minGap = std::max (2, (int) (0.06 * sampleRate / hop));   // 60 ms between hits
    const int look = std::max (2, (int) (0.02 * sampleRate / hop));     // compare with the last 20 ms
    int last = 0;
    for (int f = look; f < frames; ++f)
    {
        double lo = 1e9;
        for (int k = f - look; k < f; ++k) lo = std::min (lo, db[(size_t) k]);
        if (db[(size_t) f] < floorDb || db[(size_t) f] - lo < jump || f - last < minGap) continue;
        // walk back to where the rise starts, then to the nearest zero crossing before it
        int g = f;
        while (g > last + 1 && g > 0 && db[(size_t) g - 1] < db[(size_t) g] - 1.0) --g;
        int pos = g * hop;
        for (int i = pos; i > std::max (0, pos - hop); --i)
            if ((L[(size_t) i - 1] <= 0) != (L[(size_t) i] <= 0)) { pos = i; break; }
        if (pos > out.back() + hop) out.push_back (pos);
        last = f;
        if ((int) out.size() >= maxSlices) break;
    }
    return out;
}
} // namespace prv
