#include "choppage/Generator.h"

namespace bc::Generator
{
    namespace
    {
        struct Templates { const char* a; const char* b; };

        Templates templatesFor (Style s) noexcept
        {
            switch (s)
            {
                case Style::jungle:    return { "K.HGS.GHK.KHS.GH", "K.HGS.GKHKH.S.HG" };
                case Style::amenEdit:  return { "K.K.S.GKGKK.S.GS", "K.KGS.GKGKS.S.GS" };
                case Style::rollers:   return { "K..HS..HK.KHS..H", "K..HS.GHK.KHS.HG" };
                case Style::breakbeat: return { "K.H.S.HKH.K.S.H.", "K.H.S.H.K.KHS.HK" };
                case Style::halftime:  return { "K.H.H.H.S.H.H.HG", "K.H.H.HKS.H.HGH." };
                case Style::garage:    return { "K..H.S.HK.H..SH.", "K.H..S.HK..HKS.H" };
                case Style::house:     return { "K.OHS.OHK.OHS.OH", "K.OHS.OHK.OHSGOH" };
                case Style::electro:   return { "K..HS..HK.KH.SHK", "K..HS.HHKK.HS..H" };
            }
            return { "K.H.S.H.K.H.S.H.", "K.H.S.H.K.H.S.H." };
        }

        bool roleToBadge (char c, Badge& b) noexcept
        {
            switch (c)
            {
                case 'K': b = Badge::kick;  return true;
                case 'S': b = Badge::snare; return true;
                case 'G': b = Badge::ghost; return true;
                case 'H': b = Badge::hat;   return true;
                case 'O': b = Badge::open;  return true;
                default: return false;
            }
        }

        // Stand-ins when the break has no slice of the asked-for kind.
        const Badge* fallbacks (Badge b, int& n) noexcept
        {
            static const Badge k[] = { Badge::kick, Badge::other };
            static const Badge s[] = { Badge::snare, Badge::ghost, Badge::other };
            static const Badge g[] = { Badge::ghost, Badge::snare, Badge::hat };
            static const Badge h[] = { Badge::hat, Badge::open, Badge::ghost };
            static const Badge o[] = { Badge::open, Badge::hat };
            static const Badge x[] = { Badge::other, Badge::hat };
            switch (b)
            {
                case Badge::kick:  n = 2; return k;
                case Badge::snare: n = 3; return s;
                case Badge::ghost: n = 3; return g;
                case Badge::hat:   n = 3; return h;
                case Badge::open:  n = 2; return o;
                default:           n = 2; return x;
            }
        }
    }

    int pickSlice (const SlotContent& c, Badge want, Rng& rng)
    {
        int n = 0;
        const Badge* order = fallbacks (want, n);
        for (int f = 0; f < n; ++f)
        {
            int candidates[kMaxSlices]; int count = 0;
            for (int k = 0; k < c.numSlices(); ++k)
                if (c.badges[(size_t) k] == order[f]) candidates[count++] = k;
            if (count > 0) return candidates[rng.nextInt (count)];
        }
        return -1;
    }

    int nextWithSameBadge (const SlotContent& c, int from)
    {
        const int n = c.numSlices();
        if (from < 0 || from >= n) return from;
        const Badge b = c.badges[(size_t) from];
        for (int i = 1; i <= n; ++i)
        {
            const int k = (from + i) % n;
            if (c.badges[(size_t) k] == b) return k;
        }
        return from;
    }

    Pattern pattern (const Input& in)
    {
        Pattern out = in.current;
        out.numSteps = in.original.numSteps;
        if (in.content == nullptr) return out;
        const auto& c = *in.content;
        Rng rng (in.seed);
        const auto t = templatesFor (in.style);
        const float amount = clampv (in.amount, 0.0f, 1.0f);

        for (int step = 0; step < out.numSteps; ++step)
        {
            if (in.current.locked[(size_t) step]) continue;
            const int16_t orig = in.original.slice[(size_t) step];
            int16_t chosen = orig;

            const int bar = step / 16;
            // Second template on odd bars, and sometimes on even ones once AMOUNT is high.
            const bool useB = (bar % 2 == 1) || rng.nextFloat() < amount * 0.3f;
            const char role = (useB ? t.b : t.a)[step % 16];

            if (rng.nextFloat() < amount)
            {
                Badge b;
                if (roleToBadge (role, b))
                {
                    const int k = pickSlice (c, b, rng);
                    if (k >= 0) chosen = (int16_t) k;
                }
                else if (role == '.')
                {
                    if (rng.nextFloat() < amount * 0.6f) chosen = -1;
                }
            }

            if (chosen < 0 && in.layers && rng.nextFloat() < amount * 0.3f)
            {
                const int k = pickSlice (c, rng.nextBool() ? Badge::ghost : Badge::hat, rng);
                if (k >= 0) chosen = (int16_t) k;
            }

            out.slice[(size_t) step] = chosen;
            out.locked[(size_t) step] = false;
        }
        // Never leave the downbeat empty: the break has to land.
        if (out.slice[0] < 0 && ! in.current.locked[0])
        {
            const int k = pickSlice (c, Badge::kick, rng);
            out.slice[0] = (int16_t) (k >= 0 ? k : 0);
        }
        return out;
    }

    Pattern fx (const Pattern& current, float amount, int64_t seed)
    {
        Pattern out = current;
        Rng rng (seed);
        amount = clampv (amount, 0.0f, 1.0f);
        for (int step = 0; step < out.numSteps; ++step)
        {
            if (current.locked[(size_t) step]) continue;
            out.fx[(size_t) step] = {};
            if (step % 16 == 0) continue;                       // keep the "one" clean
            const bool late = step % 16 >= 12;                  // tape stops and rolls live at the end of a bar
            const float p = amount * (late ? 0.45f : 0.18f);
            if (rng.nextFloat() >= p) continue;

            const float r = rng.nextFloat();
            FxType type;
            if (late && r < 0.2f + amount * 0.2f)  type = FxType::tapeStop;
            else if (r < 0.55f)                    type = FxType::stutter;
            else if (r < 0.8f)                     type = FxType::reverse;
            else                                   type = FxType::stretch;

            static const uint8_t rates[] = { 8, 16, 16, 32, 32, 64 };
            const int maxIdx = amount > 0.6f ? 6 : 5;
            out.fx[(size_t) step] = { type, rates[rng.nextInt (maxIdx)] };
        }
        return out;
    }
}
