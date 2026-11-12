// Paula's Revenge - the MORPH FILTER (see MorphFilter.h)
#include "MorphFilter.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace prv
{
namespace morph
{
namespace
{
    // the vowels (a voice's first three formants, Hz) - as phonetics tables give them
    constexpr float A1 = 730, A2 = 1090, A3 = 2440, E1 = 530, E2 = 1840, E3 = 2480, I1 = 270, I2 = 2290, I3 = 3010,
                    O1 = 570, O2 = 840, O3 = 2410, U1 = 300, U2 = 870, U3 = 2240;
    Section pk (float ha, float hb, float db, float q) { return { Peak, ha, db, q, hb, db, q }; }
    Section pk2 (float ha, float da, float qa, float hb, float db, float qb) { return { Peak, ha, da, qa, hb, db, qb }; }
    Section nt (float ha, float hb, float q) { return { Notch, ha, 0, q, hb, 0, q }; }
    Section lp (float ha, float hb, float q) { return { LowPass, ha, 0, q, hb, 0, q }; }
    Section hp (float ha, float hb, float q) { return { HighPass, ha, 0, q, hb, 0, q }; }
    Section off() { return {}; }

    const std::array<Model, kModels>& all()
    {
        static const std::array<Model, kModels> m {{
            { "AH > EE",       {{ pk (A1, I1, 16, 7), pk (A2, I2, 14, 9), pk (A3, I3, 10, 10), lp (5000, 6000, 0.7f), off(), off() }}, -1 },
            { "OH > AH",       {{ pk (O1, A1, 16, 7), pk (O2, A2, 14, 9), pk (O3, A3, 10, 10), lp (4500, 5500, 0.7f), off(), off() }}, -1 },
            { "OO > EE",       {{ pk (U1, E1, 16, 7), pk (U2, E2, 14, 9), pk (U3, E3, 10, 10), lp (4000, 6000, 0.7f), off(), off() }}, 0 },
            { "TALK",          {{ pk (O1, I1, 18, 9), pk (O2, I2, 16, 11), pk (O3, I3, 12, 12), pk (3300, 4100, 8, 10), lp (4500, 7000, 0.8f), off() }}, -2 },
            { "PHASER",        {{ nt (200, 900, 1.2f), nt (480, 2000, 1.2f), nt (1100, 4300, 1.2f), nt (2500, 9000, 1.2f), off(), off() }}, 0 },
            { "COMB",          {{ pk (110, 440, 12, 14), pk (220, 880, 12, 14), pk (330, 1320, 12, 14), pk (440, 1760, 12, 14), pk (550, 2200, 12, 14), pk (660, 2640, 12, 14) }}, -1 },
            { "WAH",           {{ lp (300, 2600, 6), off(), off(), off(), off(), off() }}, 0 },
            { "ACID",          {{ lp (180, 4200, 3.5f), lp (180, 4200, 3.5f), off(), off(), off(), off() }}, 0 },
            { "LOW > HIGH",    {{ lp (500, 20000, 0.9f), hp (20, 1500, 0.9f), pk (500, 1500, 6, 2), off(), off(), off() }}, 0 },
            { "BELLS",         {{ pk (420, 600, 15, 20), pk (1130, 1610, 13, 20), pk (2210, 3150, 12, 20), pk (3700, 5300, 10, 20), off(), off() }}, 0 },
            { "SCREAM",        {{ pk (900, 1800, 18, 14), pk (2400, 4800, 16, 14), lp (6000, 9000, 1.2f), off(), off(), off() }}, -2 },
            { "DARK > BRIGHT", {{ lp (700, 14000, 1.1f), pk2 (120, 6, 1.2f, 3200, 7, 1.5f), off(), off(), off(), off() }}, 0 },
        }};
        return m;
    }
}
const Model& model (int i) { return all()[(size_t) std::clamp (i, 0, kModels - 1)]; }
const char* modelName (int i) { return model (i).name; }
const char* modelChoices()
{
    static const std::string s = []
    {
        std::string r;
        for (int i = 0; i < kModels; ++i) { if (i) r += "|"; r += modelName (i); }
        return r;
    }();
    return s.c_str();
}
} // namespace morph

void MorphFilter::reset() { for (auto& s : sec) s.ic1 = s.ic2 = 0; }

void MorphFilter::set (int mi, float m, float octaves, float emphasis, double sr)
{
    const auto& md = morph::model (mi);
    m = std::clamp (m, 0.0f, 1.0f);
    const float shift = std::pow (2.0f, std::clamp (octaves, -4.0f, 4.0f));
    const float qMul = std::pow (2.0f, (std::clamp (emphasis, 0.0f, 1.0f) - 0.5f) * 2.5f);       // (x0.42 .. x2.4)
    const float gMul = 0.6f + 0.8f * std::clamp (emphasis, 0.0f, 1.0f);                          // (x0.6 .. x1.4)
    trim = std::pow (10.0f, md.trimDb * (0.6f + 0.4f * gMul) / 20.0f);
    for (size_t i = 0; i < sec.size(); ++i)
    {
        const auto& d = md.s[i];
        auto& s = sec[i];
        s.kind = d.kind;
        if (d.kind == morph::Off) continue;
        // between the two shapes: frequency on a log scale, gain and Q straight
        const float hz = std::clamp (d.hzA * std::pow (d.hzB / d.hzA, m) * shift, 20.0f, (float) (sr * 0.45));
        const float q = std::max (0.3f, (d.qA + (d.qB - d.qA) * m) * qMul);
        const float db = (d.dbA + (d.dbB - d.dbA) * m) * gMul;
        const float g = (float) std::tan (3.14159265358979 * hz / sr);
        s.k = 1.0f / q;
        s.a1 = 1.0f / (1.0f + g * (g + s.k)); s.a2 = g * s.a1; s.a3 = g * s.a2;
        s.gain = std::pow (10.0f, db / 20.0f) - 1.0f;
    }
}

float MorphFilter::process (float x)
{
    for (auto& s : sec)
    {
        if (s.kind == morph::Off) continue;
        const float v3 = x - s.ic2, v1 = s.a1 * s.ic1 + s.a2 * v3, v2 = s.ic2 + s.a2 * s.ic1 + s.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
        const float bp = v1, lp = v2, hp = x - s.k * v1 - v2;
        switch (s.kind)
        {
            case morph::Peak:     x = x + s.gain * s.k * bp; break;
            case morph::Notch:    x = x - s.k * bp; break;
            case morph::LowPass:  x = lp; break;
            case morph::HighPass: x = hp; break;
            case morph::BandPass: x = s.k * bp; break;
            default: break;
        }
    }
    // (a model whose peaks pile up: kept from running away)
    x *= trim;
    const float ax = std::abs (x);
    return ax < 1.5f ? x : std::copysign (1.5f + 0.5f * std::tanh ((ax - 1.5f) * 2.0f), x);   // (never past 2)
}
} // namespace prv
