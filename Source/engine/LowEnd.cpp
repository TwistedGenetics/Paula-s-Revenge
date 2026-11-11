// Paula's Revenge - the LOW-END CHECK (see LowEnd.h)
#include "LowEnd.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prv
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    float toDb (double e) { return e > 1e-12 ? (float) (10.0 * std::log10 (e)) : -99.0f; }
    std::string dbText (float db) { char b[16]; std::snprintf (b, sizeof b, "%+.0f DB", (double) db); return b; }
}

// (RBJ cookbook, Q = 0.7071: two in a row make a 24 dB/octave Butterworth-ish slope)
void LowEndCheck::Biquad::lowPass (double rate, double hz)
{
    const double w = 2 * kPi * hz / rate, c = std::cos (w), a = std::sin (w) / (2 * 0.7071), a0 = 1 + a;
    b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0;
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - a) / a0); z1 = z2 = 0;
}

void LowEndCheck::Biquad::highPass (double rate, double hz)
{
    const double w = 2 * kPi * hz / rate, c = std::cos (w), a = std::sin (w) / (2 * 0.7071), a0 = 1 + a;
    b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0;
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - a) / a0); z1 = z2 = 0;
}

void LowEndCheck::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    coef = (float) std::exp (-1.0 / (2.0 * sr));
    for (int i = 0; i < 2; ++i)
    {
        lowL[i].lowPass (sr, 120); lowR[i].lowPass (sr, 120);
        sub[i].lowPass (sr, 80);
        mudHp[i].highPass (sr, 200); mudLp[i].lowPass (sr, 500);
    }
    reset();
}

void LowEndCheck::reset()
{
    for (int i = 0; i < 2; ++i)
        for (auto* b : { &lowL[i], &lowR[i], &sub[i], &mudHp[i], &mudLp[i] }) b->z1 = b->z2 = 0;
    ll = rr = lr = all = subE = mudE = 0;
    peak = 0; clipCount = 0; clipAge = peakAge = 1e9;
    aCorr = 1; aSub = -99; aMud = -99; aPeak = -99; aAll = 0; aClips = 0;
}

void LowEndCheck::process (const float* L, const float* R, int n)
{
    if (n <= 0) return;
    const double k = coef, k1 = 1.0 - coef;
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i], m = 0.5f * (l + r);
        const float lo = lowL[1].run (lowL[0].run (l)), ro = lowR[1].run (lowR[0].run (r));
        const float s = sub[1].run (sub[0].run (m));
        const float md = mudLp[1].run (mudLp[0].run (mudHp[1].run (mudHp[0].run (m))));
        ll = ll * k + k1 * lo * lo; rr = rr * k + k1 * ro * ro; lr = lr * k + k1 * lo * ro;
        all = all * k + k1 * 0.5 * (l * l + r * r);
        subE = subE * k + k1 * s * s;
        mudE = mudE * k + k1 * md * md;
        const float a = std::max (std::abs (l), std::abs (r));
        if (a >= 0.999f) { ++clipCount; clipAge = 0; }
        if (a >= peak) { peak = a; peakAge = 0; }
    }
    const double secs = n / sr;
    clipAge += secs; peakAge += secs;
    if (clipAge > 3.0) clipCount = 0;
    if (peakAge > 3.0) peak *= 0.9f;   // (the old peak slips away)
    // (flush the filters' denormals when it's silent)
    if (all < 1e-14) reset();
    const double lowE = std::sqrt (ll * rr);
    aCorr.store (lowE > 1e-10 ? (float) std::clamp (lr / lowE, -1.0, 1.0) : 1.0f);
    aSub.store (toDb (subE / std::max (all, 1e-14)));
    aMud.store (toDb (mudE / std::max (all, 1e-14)));
    aPeak.store (peak > 0 ? 20.0f * std::log10 (peak) : -99.0f);
    aAll.store ((float) all);
    aClips.store (clipCount);
}

LowEndReport LowEndCheck::report() const
{
    LowEndReport r;
    r.signal = aAll.load() > 1e-5;          // (-50 dB)
    r.lowCorr = aCorr.load(); r.subDb = aSub.load(); r.mudDb = aMud.load();
    r.peakDb = aPeak.load(); r.clips = aClips.load();
    return r;
}

std::vector<LowEndLine> lowEndAdvice (const LowEndReport& r)
{
    std::vector<LowEndLine> out;
    if (! r.signal)
    {
        out.push_back ({ 0, "PLAY THE SONG", "THE CHECK LISTENS TO THE MASTER WHILE IT PLAYS" });
        return out;
    }
    // the bass in mono (clubs and phones sum it)
    if (r.lowCorr < -0.3f)
        out.push_back ({ 2, "BASS OUT OF PHASE", "IT CANCELS IN MONO - UTILITY: MONO <120HZ ON THE BASS" });
    else if (r.lowCorr < 0.75f)
        out.push_back ({ 1, "BASS NOT MONO", "WIDE BELOW 120 HZ - UTILITY: MONO <120HZ ON THE BASS" });
    else
        out.push_back ({ 0, "BASS IS MONO", "BELOW 120 HZ IS THE SAME BOTH SIDES" });
    // the sub against the rest
    if (r.subDb < -15.0f)
        out.push_back ({ 1, "NOT MUCH SUB", "SUB " + dbText (r.subDb) + " OF THE MIX - TURN IT UP, OR ADD A SUB" });
    else if (r.subDb > -1.5f)
        out.push_back ({ 1, "SUB SWAMPS THE MIX", "SUB " + dbText (r.subDb) + " OF THE MIX - TURN IT DOWN, OR DUCK IT"
                                                    " UNDER THE KICK" });
    else
        out.push_back ({ 0, "SUB LEVEL OK", "SUB " + dbText (r.subDb) + " OF THE MIX" });
    // mud
    if (r.mudDb > -5.0f)
        out.push_back ({ 1, "MUDDY", "200-500 HZ " + dbText (r.mudDb) + " - CUT THERE ON PADS AND BREAKS" });
    else
        out.push_back ({ 0, "CLEAR LOW MIDS", "200-500 HZ " + dbText (r.mudDb) });
    // clipping
    if (r.clips > 0)
        out.push_back ({ 2, "CLIPPING", std::to_string (r.clips) + " SAMPLES AT FULL SCALE - TURN THE MASTER DOWN" });
    else
        out.push_back ({ 0, "NO CLIPPING", "PEAK " + dbText (r.peakDb) });
    return out;
}
} // namespace prv
