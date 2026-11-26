#include "choppage/SoundChain.h"
#include <cmath>

namespace bc
{
    void SoundChain::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        charLp.prepare (sr);
        charLp.setType (Svf::Type::lowPass);
        charLp.setResonance (0.6f);
        filt.prepare (sr);
        cutoff.reset (sr, 0.03);
        cutoff.setCurrentAndTarget (20000.0f);
        for (int c = 0; c < 2; ++c)
        {
            echo[c].assign ((size_t) (sr * 3.0) + 1, 0.0f);
            pre[c].assign ((size_t) (sr * 0.11) + 1, 0.0f);
            wet[c].assign ((size_t) std::max (1, maxBlock), 0.0f);
        }
        verb.prepare (sr);
        gain.reset (sr, 0.02);
        gain.setCurrentAndTarget (1.0f);
        reset();
    }

    void SoundChain::reset()
    {
        holdPhase = crushPhase = 1.0;
        held[0] = held[1] = crushHeld[0] = crushHeld[1] = 0.0f;
        toneState[0] = toneState[1] = echoLp[0] = echoLp[1] = 0.0f;
        charLp.reset(); filt.reset(); verb.reset();
        for (int c = 0; c < 2; ++c)
        {
            std::fill (echo[c].begin(), echo[c].end(), 0.0f);
            std::fill (pre[c].begin(), pre[c].end(), 0.0f);
        }
        echoW = preW = 0;
    }

    void SoundChain::process (float* left, float* right, int n, const Settings& s) noexcept
    {
        float* d[2] = { left, right };
        const int chans = 2;

        // ---- CHARACTER: the machine's rate (or the fader, whichever is lower), bits, output filter.
        const auto& c = character (s.character);
        double rate = c.rate > 0.0 ? std::min (c.rate, s.faderRate) : s.faderRate;
        int bits = c.bits;
        if (s.dropHeld) { rate = std::min (rate, s.dropRate); bits = bits == 0 ? 8 : std::min (bits, 8); }
        const bool decimate = rate < sr - 1.0;
        const double step = rate / sr;
        const float q = bits > 0 ? (float) (1 << (bits - 1)) : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (decimate)
            {
                holdPhase += step;
                if (holdPhase >= 1.0)
                {
                    holdPhase -= 1.0;
                    for (int ch = 0; ch < chans; ++ch) held[ch] = d[ch][i];
                }
            }
            for (int ch = 0; ch < chans; ++ch)
            {
                float v = decimate ? held[ch] : d[ch][i];
                if (q > 0.0f) v = std::round (clampv (v, -1.0f, 1.0f) * q) / q;
                d[ch][i] = v;
            }
        }
        if (c.lowpassHz > 0.0f && c.lowpassHz < sr * 0.45)
        {
            charLp.setCutoff (c.lowpassHz);
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i) d[ch][i] = charLp.process (ch, d[ch][i]);
        }

        // ---- FILTER
        filt.setType (s.highPass ? Svf::Type::highPass : Svf::Type::lowPass);
        filt.setResonance (0.707f + s.filterRes * 7.0f);
        cutoff.setTarget (20.0f * std::pow (1000.0f, clampv (s.filter, 0.0f, 1.0f)));
        const bool open = (! s.highPass && s.filter >= 0.999f) || (s.highPass && s.filter <= 0.001f);
        if (! open || cutoff.isSmoothing())
            for (int i = 0; i < n; ++i)
            {
                const float f = cutoff.next();
                if ((i & 7) == 0) filt.setCutoff (clampv (f, 20.0f, (float) (sr * 0.45)));
                for (int ch = 0; ch < chans; ++ch) d[ch][i] = filt.process (ch, d[ch][i]);
            }

        // ---- DISTORTION: tanh drive up to +30 dB, level matched, tone tilt
        if (s.dist > 0.001f)
        {
            const float g = dbToGain (s.dist * 30.0f);
            const float norm = 1.0f / std::tanh (g);
            const float toneA = 0.15f + 0.8f * (s.distTone * 0.5f + 0.5f);
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float y = std::tanh (d[ch][i] * g) * norm * 0.8f;
                    toneState[ch] += (y - toneState[ch]) * toneA;
                    d[ch][i] = toneState[ch];
                }
        }

        // ---- CRUSH: bits 16 -> 4, rate x1 -> x8
        if (s.crush > 0.001f)
        {
            const float cq = std::pow (2.0f, 15.0f - s.crush * 12.0f);
            const double cstep = 1.0 / (1.0 + s.crush * 7.0);
            for (int i = 0; i < n; ++i)
            {
                crushPhase += cstep;
                if (crushPhase >= 1.0)
                {
                    crushPhase -= 1.0;
                    for (int ch = 0; ch < chans; ++ch) crushHeld[ch] = std::round (d[ch][i] * cq) / cq;
                }
                for (int ch = 0; ch < chans; ++ch) d[ch][i] = crushHeld[ch];
            }
        }

        // ---- ECHO: tempo synced, filtered feedback, mono or stereo
        if (s.echoMix > 0.001f)
        {
            const int len = (int) echo[0].size();
            const int delay = clampv ((int) s.echoSamples, 1, len - 1);
            for (int i = 0; i < n; ++i)
            {
                const int r = (echoW - delay + len) % len;
                const float wetv[2] = { echo[0][(size_t) r], echo[1][(size_t) r] };
                const float mono = 0.5f * (d[0][i] + d[1][i]);
                for (int ch = 0; ch < 2; ++ch)
                {
                    echoLp[ch] += (wetv[ch] - echoLp[ch]) * 0.35f;
                    echo[ch][(size_t) echoW] = (s.echoMono ? mono : d[ch][i]) + echoLp[ch] * s.echoFb;
                }
                for (int ch = 0; ch < chans; ++ch) d[ch][i] += wetv[ch] * s.echoMix;
                echoW = (echoW + 1) % len;
            }
        }

        // ---- REVERB: the reverb hears the signal REV PRE ms late; the dry path is untouched
        if (s.reverb > 0.001f)
        {
            Reverb::Params p;
            p.roomSize = 0.3f + s.revDecay * 0.68f;
            p.damping = s.revDamp;
            p.width = s.revWidth;
            p.wet = 1.0f;
            p.dry = 0.0f;
            verb.setParams (p);
            const int plen = (int) pre[0].size();
            const int pd = clampv ((int) (s.revPreMs * 0.001f * sr), 0, plen - 1);
            const int m = std::min (n, (int) wet[0].size());
            for (int i = 0; i < m; ++i)
            {
                const int r = (preW - pd + plen) % plen;
                for (int ch = 0; ch < 2; ++ch)
                {
                    pre[ch][(size_t) preW] = d[ch][i];
                    wet[ch][(size_t) i] = pre[ch][(size_t) r];
                }
                preW = (preW + 1) % plen;
            }
            verb.processStereo (wet[0].data(), wet[1].data(), m);
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < m; ++i) d[ch][i] = d[ch][i] * (1.0f - s.reverb * 0.4f) + wet[ch][(size_t) i] * s.reverb;
        }

        // ---- VOLUME + meter
        gain.setTarget (s.volumeDb <= -59.9f ? 0.0f : dbToGain (s.volumeDb));
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.next();
            for (int ch = 0; ch < chans; ++ch)
            {
                d[ch][i] *= g;
                pk = std::max (pk, std::abs (d[ch][i]));
            }
        }
        float prev = peak.load();
        while (pk > prev && ! peak.compare_exchange_weak (prev, pk)) {}
    }
}
