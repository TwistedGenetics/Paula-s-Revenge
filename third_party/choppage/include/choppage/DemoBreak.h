#pragma once
/*
    DemoBreak — a synthesised two-bar break (kick / snare / ghost / hat / open hat)
    so CHOPPAGE makes a sound the first time it opens, before any sample is
    dropped in. Also what the tests chop. Deterministic: same audio every time.
*/
#include "choppage/Model.h"
#include <memory>
#include <cmath>

namespace bc::DemoBreak
{
    // 32 sixteenths: K kick, S snare, G ghost, H hat, O open hat, . rest  (an amen-ish two bars)
    inline const char* layout() { return "K.K.S.GKGKK.S.GOK.KGS.GKGKS.S.GH"; }

    inline SamplePtr make (double sampleRate = 44100.0, double bpm = 170.0)
    {
        const char* pat = layout();
        const int steps = 32;
        const double stepLen = sampleRate * 60.0 / bpm / 4.0;
        const int length = (int) std::round (stepLen * steps);
        auto b = std::make_shared<SampleData>();
        b->allocate (2, length);
        b->sampleRate = sampleRate;
        b->name = "CHOPPAGE DEMO 170";
        Rng noise (1234);
        float* L = b->ch[0].data();
        float* R = b->ch[1].data();
        const double twoPi = (2.0 * kPi);

        for (int st = 0; st < steps; ++st)
        {
            const char c = pat[st];
            if (c == '.') continue;
            const int start = (int) std::round (st * stepLen);
            const int len = std::min (length - start, (int) (sampleRate * (c == 'O' ? 0.35 : 0.25)));
            double phase = 0.0;
            float lp = 0.0f, hpPrev = 0.0f, hpOut = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                const double t = i / sampleRate;
                float v = 0.0f;
                const float n = noise.nextFloat() * 2.0f - 1.0f;
                switch (c)
                {
                    case 'K':
                    {
                        const double f = 48.0 + 120.0 * std::exp (-t * 30.0);
                        phase += twoPi * f / sampleRate;
                        v = (float) (std::sin (phase) * std::exp (-t * 9.0)) * 0.95f;
                        break;
                    }
                    case 'S':
                    case 'G':
                    {
                        phase += twoPi * 190.0 / sampleRate;
                        lp += (n - lp) * 0.45f;
                        const float body = (float) (std::sin (phase) * std::exp (-t * 30.0));
                        const float rattle = lp * (float) std::exp (-t * 16.0);
                        v = (0.45f * body + 0.75f * rattle) * (c == 'G' ? 0.28f : 0.9f);
                        break;
                    }
                    case 'H':
                    case 'O':
                    {
                        hpOut = 0.92f * (hpOut + n - hpPrev);   // one-pole high-pass on noise
                        hpPrev = n;
                        v = hpOut * (float) std::exp (-t * (c == 'O' ? 9.0 : 60.0)) * 0.42f;
                        break;
                    }
                    default: break;
                }
                L[start + i] += v;
                R[start + i] += v * (c == 'H' || c == 'O' ? 0.8f : 1.0f);
            }
        }
        b->buildOverview();
        return b;
    }
}
