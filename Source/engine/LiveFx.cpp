// Paula's Revenge - LIVE MODE's performance effects (see LiveFx.h)
#include "LiveFx.h"
#include <algorithm>
#include <cmath>

namespace prv
{
const char* LiveFx::name (Kind k)
{
    switch (k)
    {
        case Repeat4:    return "REPEAT 1/4";
        case Repeat8:    return "REPEAT 1/8";
        case Repeat16:   return "REPEAT 1/16";
        case Repeat32:   return "STUTTER 1/32";
        case TapeStop:   return "TAPE STOP";
        case FilterLow:  return "FILTER DOWN";
        case FilterHigh: return "FILTER UP";
        default:         return "";
    }
}

void LiveFx::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    const size_t n = (size_t) (sr * 4.0);   // 4 s of the mix
    histL.assign (n, 0.0f); histR.assign (n, 0.0f);
    reset();
}

void LiveFx::reset()
{
    std::fill (histL.begin(), histL.end(), 0.0f); std::fill (histR.begin(), histR.end(), 0.0f);
    w = 0; active = None; wanted = None; fade = 0; tapeRate = 1; tapeLagging = false; filterAmt = 0; filterDir = 0;
    f1L = f2L = f1R = f2R = h1L = h1R = g1L = g1R = 0; caught = repLen = repPos = 0;
}

float LiveFx::read (const std::vector<float>& h, double pos) const
{
    const int n = (int) h.size();
    while (pos < 0) pos += n;
    while (pos >= n) pos -= n;
    const int i0 = (int) pos; const float fr = (float) (pos - i0);
    const int i1 = i0 + 1 >= n ? 0 : i0 + 1;
    return h[(size_t) i0] + (h[(size_t) i1] - h[(size_t) i0]) * fr;
}

void LiveFx::process (float* L, float* R, int n, double bpm)
{
    if (histL.empty()) return;
    const int H = (int) histL.size();
    const Kind want = (Kind) wanted;
    const bool isRep = want >= Repeat4 && want <= Repeat32;
    // a new effect: the old one fades out first (a few ms), then the new one starts
    if (want != active && fade <= 0.0f && ! (active == TapeStop && tapeLagging))
    {
        active = want;
        if (isRep)
        {
            const double beat = 60.0 / std::max (40.0, bpm) * sr;
            const double frac = want == Repeat4 ? 1.0 : want == Repeat8 ? 0.5 : want == Repeat16 ? 0.25 : 0.125;
            repLen = std::clamp ((int) (beat * frac), 64, H / 2);
            repStart = w; repPos = 0; caught = 0;
        }
        if (want == TapeStop) { tapeRead = w; tapeRate = 1.0f; tapeLagging = true; }
        if (want == FilterLow) filterDir = -1;
        if (want == FilterHigh) filterDir = 1;
    }
    const float fadeStep = (float) (1.0 / (0.004 * sr));            // (4 ms)
    const float tapeDown = (float) (1.0 / (0.7 * sr)), tapeUp = (float) (1.0 / (0.25 * sr));
    const float sweep = (float) (1.0 / (0.45 * sr));
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        histL[(size_t) w] = inL; histR[(size_t) w] = inR;
        float effL = inL, effR = inR;
        bool effOn = false;
        if (active >= Repeat4 && active <= Repeat32)
        {
            // the first time through it plays live (and is caught); then the caught slice loops, each pass faded at
            // its ends so the loop point never clicks
            effOn = true;
            if (caught < repLen)
            {
                // (its last few ms fade down, as each loop's end does: the first repeat starts from silence, no jump)
                const int left = repLen - 1 - caught;
                const float g = left < 96 ? (float) left / 96.0f : 1.0f;
                ++caught; effL = inL * g; effR = inR * g;
            }
            else
            {
                const int at = (repStart + repPos) % H;
                const int edge = std::min (repPos, repLen - 1 - repPos);
                const float g = edge < 96 ? (float) edge / 96.0f : 1.0f;
                effL = histL[(size_t) at] * g; effR = histR[(size_t) at] * g;
                if (++repPos >= repLen) repPos = 0;
            }
            if (want != active) effOn = false;   // (letting go: fading back to live)
        }
        else if (active == TapeStop)
        {
            // slowing (held) or speeding back up (let go); caught up with the live mix: done
            if (want == TapeStop) tapeRate = std::max (0.0f, tapeRate - tapeDown);
            else tapeRate = std::min (1.0f, tapeRate + tapeUp);
            tapeRead += tapeRate;
            effL = read (histL, tapeRead); effR = read (histR, tapeRead);
            // (stopped: the last sample would hold as DC - it fades to silence instead)
            const float level = std::min (1.0f, tapeRate * 6.0f);
            effL *= level; effR *= level;
            effOn = true;
            if (want != TapeStop && tapeRate >= 1.0f) { tapeLagging = false; effOn = false; }
        }
        // the crossfade between live and the effect
        if (effOn) fade = std::min (1.0f, fade + fadeStep); else fade = std::max (0.0f, fade - fadeStep);
        if (fade <= 0.0f && active != None && active != FilterLow && active != FilterHigh && want != active && ! tapeLagging) active = None;
        float oL = inL + (effL - inL) * fade, oR = inR + (effR - inR) * fade;
        // FILTER: sweeping while held, back when let go (it runs as long as it's anywhere but open)
        const bool fHeld = want == FilterLow || want == FilterHigh;
        if (fHeld) { filterDir = want == FilterLow ? -1 : 1; filterAmt = std::min (1.0f, filterAmt + sweep); }
        else filterAmt = std::max (0.0f, filterAmt - sweep * 1.6f);
        if (active == FilterLow || active == FilterHigh) { if (! fHeld && filterAmt <= 0.0f) active = None; }
        if (filterAmt > 0.0001f)
        {
            if (filterDir < 0)
            {
                const float hz = 18000.0f * std::pow (250.0f / 18000.0f, filterAmt);
                const float a = 1.0f - std::exp (-6.2831853f * hz / (float) sr);
                f1L += a * (oL - f1L); f2L += a * (f1L - f2L); oL = f2L;
                f1R += a * (oR - f1R); f2R += a * (f1R - f2R); oR = f2R;
            }
            else
            {
                const float hz = 20.0f * std::pow (4000.0f / 20.0f, filterAmt);
                const float a = 1.0f - std::exp (-6.2831853f * hz / (float) sr);
                // (two one-pole high-passes: each takes off what a low-pass at hz lets through)
                h1L += a * (oL - h1L); const float xL = oL - h1L; g1L += a * (xL - g1L); oL = xL - g1L;
                h1R += a * (oR - h1R); const float xR = oR - h1R; g1R += a * (xR - g1R); oR = xR - g1R;
            }
        }
        else { f1L = f2L = oL; f1R = f2R = oR; h1L = h1R = g1L = g1R = 0; }
        L[i] = oL; R[i] = oR;
        if (++w >= H) w = 0;
    }
}
} // namespace prv
