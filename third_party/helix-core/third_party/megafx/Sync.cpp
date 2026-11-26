// Mega Synth's tempo-sync helpers (from its Engine.cpp), which its effects call.
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <string>
#include "Params.h"

namespace tg
{

double syncBeats (const char* key)
{
    struct E { const char* k; double b; };
    static const E table[] = {
        { "2bar", 8 }, { "4bar", 16 }, { "1bar", 4 }, { "1/2", 2 }, { "1/2d", 3 }, { "1/2t", 4.0 / 3.0 },
        { "1/4", 1 }, { "1/4d", 1.5 }, { "1/4t", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/8d", 0.75 }, { "1/8t", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16d", 0.375 }, { "1/16t", 1.0 / 6.0 }, { "1/32", 0.125 }
    };
    for (auto& e : table) if (std::strcmp (e.k, key) == 0) return e.b;
    return 0.0;
}

double syncSeconds (const ChoiceList& list, int idx, double tempo, double fallback)
{
    if (idx <= 0 || idx >= list.size) return fallback;
    const double beats = syncBeats (list.keys[idx]);
    if (beats <= 0) return fallback;
    return (60.0 / std::max (1.0, tempo)) * beats;
}

double syncRate (const ChoiceList& list, int idx, double tempo, double fallback)
{
    const double s = syncSeconds (list, idx, tempo, 0.0);
    if (idx <= 0 || s <= 0) return fallback;
    return 1.0 / s;
}

} // namespace tg
