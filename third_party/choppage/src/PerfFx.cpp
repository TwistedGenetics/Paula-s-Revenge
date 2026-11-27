#include "choppage/PerfFx.h"
#include <cmath>

namespace bc
{
    namespace { constexpr int kReadOrder[] = { PerfFx::tapeStop, PerfFx::reverse, PerfFx::roll, PerfFx::stutter, PerfFx::akaize }; }

    void PerfFx::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        int size = 1;
        while (size < (int) (sr * 4.0)) size <<= 1;
        ring[0].assign ((size_t) size, 0.0f);
        ring[1].assign ((size_t) size, 0.0f);
        ringMask = size - 1;
        rampStep = 1.0f / (float) std::max (1.0, 0.003 * sr);
        (void) maxBlock;
        dive.prepare (sr);
        dive.setType (Svf::Type::lowPass);
        reset();
    }

    void PerfFx::reset() noexcept
    {
        std::fill (ring[0].begin(), ring[0].end(), 0.0f);
        std::fill (ring[1].begin(), ring[1].end(), 0.0f);
        written = 0;
        for (auto& f : fx) f = {};
        wasHeld.fill (false);
        dive.reset();
        diveMix = 0.0f;
        diveCutoff = 18000.0f;
    }

    float PerfFx::ringAt (int ch, double absPos) const noexcept
    {
        if (absPos < 0.0 || absPos > (double) written - 1.0) return 0.0f;
        if ((double) written - absPos >= (double) ringMask) return 0.0f;   // older than the ring holds
        const auto i = (int64_t) std::floor (absPos);
        const float f = (float) (absPos - (double) i);
        const float* d = ring[ch].data();
        const float a = d[(int) (i & ringMask)];
        const float b = d[(int) ((i + 1) & ringMask)];
        return a + (b - a) * f;
    }

    void PerfFx::engage (int pad, const Settings& s) noexcept
    {
        auto& r = fx[(size_t) pad];
        r.age = 0;
        const double now = (double) written - 1.0;   // the sample just recorded
        switch (pad)
        {
            case stutter:
            {
                r.loopLen = s.samplesPerBeat * 4.0 / (double) s.stutterDiv;
                double back = 0.0;
                if (s.quantSteps > 0)
                {
                    const double grid = s.samplesPerBeat / 4.0 * s.quantSteps;
                    back = std::fmod (s.samplesSinceGrid, grid);
                }
                r.loopStart = now - back;
                r.phase = back;            // carries on live until the loop wraps
                break;
            }
            case roll:
                r.loopLen = s.samplesPerBeat * 4.0 / (double) s.rollDiv;
                r.loopStart = now - r.loopLen;
                r.phase = 0.0;
                r.speed = 1.0;
                r.level = (s.rollMode == 1 || s.rollMode == 2) ? 0.15f : 1.0f;
                break;
            case reverse:
            case tapeStop:
                r.pos = now;
                r.speed = 1.0;
                break;
            case akaize:
                r.pos = now;
                r.grains.setup (s.akCycleLength, s.akCycleFreq, sr);
                akSetLength = s.akCycleLength; akSetFreq = s.akCycleFreq;
                r.grains.reset();
                r.grains.tick (r.pos);
                break;
            case filterDive:
                diveAge = 0;
                break;
            default: break;
        }
    }

    float PerfFx::readEffect (int pad, int ch, const Settings&) const noexcept
    {
        const auto& r = fx[(size_t) pad];
        switch (pad)
        {
            case stutter:  return ringAt (ch, r.loopStart + r.phase);
            case roll:     return ringAt (ch, r.loopStart + r.phase) * r.level;
            case reverse:
            case tapeStop: return ringAt (ch, r.pos);
            case akaize:
                return r.grains.sample ([this, ch] (double p) { return ringAt (ch, p); });
            default: return 0.0f;
        }
    }

    void PerfFx::advance (int pad, const Settings& s) noexcept
    {
        auto& r = fx[(size_t) pad];
        ++r.age;
        switch (pad)
        {
            case stutter:
                r.phase += 1.0;
                if (r.phase >= r.loopLen) r.phase -= r.loopLen;
                break;
            case roll:
            {
                // over two beats (ROLL MODE): up / down an octave, and / or the level from 15% up to full or down to 10%
                const double t = std::min (1.0, (double) r.age / (s.samplesPerBeat * 2.0));
                const int m = s.rollMode;
                const double bend = (m == 0 || m == 2) ? t : (m == 4 || m == 5) ? -t : 0.0;
                r.speed = std::pow (2.0, bend);
                r.level = (m == 1 || m == 2) ? (float) (0.15 + 0.85 * t)
                        : (m == 3 || m == 4) ? (float) (1.0 - 0.9 * t) : 1.0f;
                r.phase += r.speed;
                while (r.phase >= r.loopLen) r.phase -= r.loopLen;
                break;
            }
            case reverse:
                r.pos -= 1.0;
                break;
            case tapeStop:
            {
                const double t = (double) r.age / std::max (1.0, s.tapeMs * 0.001 * sr);
                r.speed = std::max (0.0, 1.0 - t);
                r.speed *= r.speed;          // a heavy platter slows fast, then drags
                r.pos += r.speed;
                break;
            }
            case akaize:
            {
                // the read point moves at 1 / stretch; grains read on at the recording's own speed
                if (s.akCycleLength != akSetLength || s.akCycleFreq != akSetFreq)
                {
                    r.grains.setup (s.akCycleLength, s.akCycleFreq, sr);   // turning CYCLE while it plays
                    akSetLength = s.akCycleLength; akSetFreq = s.akCycleFreq;
                }
                // STRETCH: the read point moves at 1 / stretch (the beat drags). IN TIME: it keeps the beat's own speed,
                // and the grains go through the source as the stretch would (each group of them repeating a little of
                // it), then jump on to the beat - the metallic sound in time; read at IN TIME PITCH
                r.pos += s.akInTime ? 1.0 : 1.0 / std::max (1.0, s.akRatio);
                r.grains.advance (s.akInTime ? s.akPitch : 1.0);
                if (s.akInTime) r.grains.tickInTime (r.pos, s.akRatio);
                else r.grains.tick (r.pos);
                break;
            }
            default: break;
        }
    }

    void PerfFx::process (float* left, float* right, int numSamples, const std::array<bool, kFxPads>& held,
                          const Settings& s) noexcept
    {
        const int chans = 2;
        float* data[2] = { left, right };

        // Which read effect wins (highest priority held)
        int winner = -1;
        for (int p : kReadOrder) if (held[(size_t) p]) { winner = p; break; }

        for (int i = 0; i < numSamples; ++i)
        {
            // Record the dry input
            const int w = (int) (written & ringMask);
            for (int ch = 0; ch < 2; ++ch)
                ring[ch][(size_t) w] = data[ch][i];
            ++written;

            // Edges: (re)start an effect the moment its pad goes down
            if (i == 0)
                for (int p = 0; p < kFxPads; ++p)
                {
                    if (held[(size_t) p] && ! wasHeld[(size_t) p]) engage (p, s);
                    wasHeld[(size_t) p] = held[(size_t) p];
                }

            float sum[2] = { 0.0f, 0.0f };
            float total = 0.0f;
            for (int p : kReadOrder)
            {
                auto& r = fx[(size_t) p];
                const float target = p == winner ? 1.0f : 0.0f;
                if (r.gain < target)      r.gain = std::min (target, r.gain + rampStep);
                else if (r.gain > target) r.gain = std::max (target, r.gain - rampStep);
                if (r.gain <= 0.0f) continue;
                for (int ch = 0; ch < 2; ++ch) sum[ch] += readEffect (p, ch, s) * r.gain;
                total += r.gain;
                advance (p, s);
            }
            total = std::min (1.0f, total);
            for (int ch = 0; ch < chans; ++ch)
                data[ch][i] = data[ch][i] * (1.0f - total) + sum[ch];
        }

        // FILTER DIVE: closes from 18 kHz to 150 Hz over a beat while held, opens in 50 ms after.
        const bool diving = held[(size_t) filterDive];
        if (diving || diveMix > 0.0f)
        {
            dive.setResonance (0.7f + s.diveRes * 6.0f);
            for (int i = 0; i < numSamples; ++i)
            {
                if (diving)
                {
                    const float t = std::min (1.0f, (float) diveAge++ / (float) s.samplesPerBeat);
                    diveCutoff = 18000.0f * std::pow (150.0f / 18000.0f, t);
                    diveMix = std::min (1.0f, diveMix + rampStep);
                }
                else
                {
                    diveCutoff = std::min (18000.0f, diveCutoff * (1.0f + 4.0f / (float) (0.05 * sr)) + 1.0f);
                    if (diveCutoff >= 17999.0f) diveMix = std::max (0.0f, diveMix - rampStep);
                }
                if ((i & 15) == 0) dive.setCutoff (clampv (diveCutoff, 40.0f, (float) (sr * 0.45)));
                for (int ch = 0; ch < chans; ++ch)
                {
                    const float x = data[ch][i];
                    const float y = dive.process (ch, x);
                    data[ch][i] = x + (y - x) * diveMix;
                }
            }
        }
    }
}
