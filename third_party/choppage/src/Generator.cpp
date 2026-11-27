#include "choppage/Generator.h"

namespace bc::Generator
{
    namespace
    {
        // 0.17: each style has a bank of two-bar grooves (bar A, bar B), the classic patterns as drum books write
        // them (Pocket Operations' AMEN BREAK A-D, JUNGLE A / B, ROLLING 1-4, DRUM AND BASS 1-4, STANDARD BREAKBEAT
        // 1-3, FUNKY PRESIDENT, GET UP, COLD SWEAT, HALF DROP, STEPPERS, UK GARAGE A / B, HOUSE, ELECTRO 1-3) in the
        // roles a chopped break has: K kick, S snare (the backbeat), G ghost / off-beat snare, H hat, O open hat,
        // P perc (a rim, a tom), '.' nothing new (the slice before rings on). One step = a 16th.
        struct Groove { const char* a; const char* b; };

        const std::vector<Groove>& groovesFor (Style s)
        {
            static const std::vector<Groove> jungle {
                { "K.K.S.HGHGKKS.HG", "K.K.P.HGHGKKS.HG" },   // AMEN BREAK A, B
                { "K.K.S.HGHGKKS.HG", "K.K.H.HGHGK.S.PG" },   // AMEN A, C
                { "K.K.H.HGHGK.S.PG", "KGK.S.HGHGK.H.S." },   // AMEN C, D
                { "K.K.S.HGHGK.H.S.", "HSK.S.HGHGK.H.S." },   // JUNGLE A, B
                { "K.K.S.HGHGKKS.HG", "KGK.S.HGHGK.H.S." },   // AMEN A, D
                { "K.H.S.K.H.S.S.H.", "H.H.S.H.H.K.S.H." },   // DRUM AND BASS 4 A, B
            };
            static const std::vector<Groove> amenEdit {
                { "K.K.P.HGHGKKS.HG", "KGK.S.HGHGK.SGSS" },   // AMEN B, D with a snare roll
                { "K.K.S.HGHGKKS.HG", "K.K.S.HGHGK.SSGS" },
                { "K.K.H.HGHGK.S.PG", "K.K.S.HGHGKKSGSG" },
                { "KGK.S.HGHGK.H.S.", "K.K.S.HGSGK.SGSS" },
            };
            static const std::vector<Groove> rollers {
                { "K.H.S.H.H.K.S.H.", "K.H.S.H.H.K.SKH." },   // ROLLING 1, 2
                { "K.H.S.K.H.S.K.H.", "K.H.S.K.H.S.K.S." },   // ROLLING 3 A, B
                { "K.HKS.HKHKK.S.HK", "K.K.S.HKKKK.S.H." },   // DRUM AND BASS 1 A, B
                { "K.H.S.HKHK.KS.HK", "K.H.S.H.H.HKS.HH" },   // DRUM AND BASS 2 A, B
                { "KKH.S.H.H.H.S.H.", "K.H.S.H.H.K.SKH." },   // ROLLING 4 A, ROLLING 2
                { "K.H.S.H.H.K.S.H.", "KHHHSHOOOOKHSHHH" },   // ROLLING 1, DRUM AND BASS 3
            };
            static const std::vector<Groove> breakbeat {
                { "K.H.S.H.H.K.S.H.", "K.K.S.H.H.K.S.H." },   // STANDARD BREAKBEAT 1, 2
                { "K.K.S.H.H.K.S.H.", "K.K.S.KGHGK.S.H." },   // STANDARD 2, 3
                { "K.HKS.HKHKK.S.H.", "K.H.S.H.H.K.S.H." },   // FUNKY PRESIDENT, STANDARD 1
                { "K.H.S.GGHGK.S.KG", "K.H.S.HGK.K.H.S." },   // GET UP, COLD SWEAT
            };
            static const std::vector<Groove> halftime {
                { "K.H.H.H.S.H.H.H.", "K.H.H.H.S.K.H.HG" },   // HALF DROP, D&B half-time
                { "K.H.H.H.S.K.H.HG", "K.H.H.HKS.K.HGH." },
                { "K.H.K.H.S.H.K.H.", "K.H.H.H.S.H.H.H." },   // STEPPERS, HALF DROP
            };
            static const std::vector<Groove> garage {
                { "KGHHSPHG..KPSGHH", "K.H.SPHG..KPSGH." },   // UK GARAGE A, B
                { "K.H.SPHG..KPSGH.", "K..H.S.HK.H..SH." },
            };
            static const std::vector<Groove> house {
                { "K.O.S.O.K.O.S.O.", "K.OHS.OHK.OHSGOH" },   // HOUSE
                { "K.OHS.OHK.OHS.OH", "K.O.SHOHK.O.SHOH" },   // DEEP HOUSE
            };
            static const std::vector<Groove> electro {
                { "KHHHSHKHHHHHSHHH", "KHHHSHKHHHKHSHKH" },   // ELECTRO 1 A, B
                { "KHHHSHKHHHHHSHHH", "KHHHSHHHHHKHSKHH" },   // ELECTRO 2 A, B
                { "KHHHSHKHHHHKSHHH", "KHHHSHKHHHKHSHKH" },   // ELECTRO 3 A, 1 B
            };
            switch (s)
            {
                case Style::jungle:    return jungle;
                case Style::amenEdit:  return amenEdit;
                case Style::rollers:   return rollers;
                case Style::breakbeat: return breakbeat;
                case Style::halftime:  return halftime;
                case Style::garage:    return garage;
                case Style::house:     return house;
                case Style::electro:   return electro;
            }
            return breakbeat;
        }

        bool roleIsPerc (char c) noexcept { return c == 'P'; }

        bool roleToBadge (char c, Badge& b) noexcept
        {
            switch (c)
            {
                case 'K': b = Badge::kick;  return true;
                case 'S': b = Badge::snare; return true;
                case 'G': b = Badge::ghost; return true;
                case 'H': b = Badge::hat;   return true;
                case 'O': b = Badge::open;  return true;
                case 'P': b = Badge::other; return true;
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
        const float amount = clampv (in.amount, 0.0f, 1.0f);

        // one groove from the style's bank for this roll
        const auto& bank = groovesFor (in.style);
        const auto& groove = bank[(size_t) rng.nextInt ((int) bank.size())];

        // the pattern's own kick and snare: mostly one slice each (a drummer hits the same drum), now and then another
        int mainSlice[kNumBadges];
        for (int b = 0; b < kNumBadges; ++b) mainSlice[b] = pickSlice (c, (Badge) b, rng);

        for (int step = 0; step < out.numSteps; ++step)
        {
            if (in.current.locked[(size_t) step]) continue;
            const int16_t orig = in.original.slice[(size_t) step];
            int16_t chosen = orig;

            const int bar = step / 16;
            // bar A, then B (4 bars: A B A B, the last bar's end a fill when AMOUNT is high)
            const char* line = (bar % 2 == 1) ? groove.b : groove.a;
            char role = line[step % 16];
            if (bar == out.numSteps / 16 - 1 && step % 16 >= 12 && amount > 0.7f && rng.nextFloat() < (amount - 0.7f) * 2.0f)
                role = (step % 2 == 0) ? 'S' : 'G';   // a snare fill into the top

            if (rng.nextFloat() < amount)
            {
                Badge b;
                if (roleToBadge (role, b))
                {
                    int k = -1;
                    // hats and ghosts: the break's own slice at this step when it is one (keeps its feel), else any
                    if ((b == Badge::hat || b == Badge::ghost || b == Badge::open) && orig >= 0 && c.badges[(size_t) orig] == b) k = orig;
                    else if (b == Badge::kick || b == Badge::snare)
                        k = rng.nextFloat() < 0.8f ? mainSlice[(int) b] : pickSlice (c, b, rng);
                    else k = pickSlice (c, b, rng);
                    if (k < 0 && roleIsPerc (role)) k = pickSlice (c, Badge::ghost, rng);
                    if (k >= 0) chosen = (int16_t) k;
                }
                else if (role == '.')
                {
                    // nothing new here: the hit before rings on (the original's hit stays sometimes, at low AMOUNT)
                    if (rng.nextFloat() < 0.35f + amount * 0.6f) chosen = -1;
                }
            }

            if (chosen < 0 && in.layers && rng.nextFloat() < amount * 0.15f)
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
            const int k = mainSlice[(int) Badge::kick] >= 0 ? mainSlice[(int) Badge::kick] : pickSlice (c, Badge::kick, rng);
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
