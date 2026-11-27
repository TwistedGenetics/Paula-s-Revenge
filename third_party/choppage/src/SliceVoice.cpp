#include "choppage/SliceVoice.h"
#include <cmath>

namespace bc
{
    void SliceVoice::start (const Start& st) noexcept
    {
        s = st;
        content = st.content;
        active = content != nullptr && content->buffer != nullptr && st.region.length() > 1;
        if (! active) return;

        repeats   = st.stutter > 1 ? st.stutter : 1;
        windowLen = (double) st.region.length() / (double) repeats;
        repeat = 0;
        head = 0.0;
        grains.setup (st.cycleLength, st.cycleFreq, sampleRate);
        grains.reset();
        age = 0;
        releaseLeft = -1;

        // TRANS: below 0.5 an exponential decay (tau 400 ms -> 12 ms); above, the tail is lifted.
        envDecay = 1.0f;
        if (st.trans < 0.5f)
        {
            const float t = st.trans / 0.5f;                       // 0 tight .. 1 natural
            const float tauMs = 12.0f * std::pow (400.0f / 12.0f, t);
            envCoef = std::exp (-1.0f / (tauMs * 0.001f * (float) sampleRate));
            tailLift = 0.0f;
        }
        else
        {
            envCoef = 1.0f;
            tailLift = (st.trans - 0.5f) * 2.0f;                   // up to +6 dB on the tail
        }
    }

    void SliceVoice::release (int fadeSamples) noexcept
    {
        if (! active || releaseLeft >= 0) return;
        releaseLen = releaseLeft = std::max (1, fadeSamples);
    }

    float SliceVoice::read (int ch, double pos) const noexcept
    {
        // pos: offset from the start of the stutter window, in source samples
        const auto& buf = *content->buffer;
        const int n = buf.numSamples;
        const int c = std::min (ch, buf.channels - 1);
        double abs;
        if (s.reverse) abs = (double) s.region.end - 1.0 - pos;
        else           abs = (double) s.region.start + pos;
        const int i = (int) std::floor (abs);
        if (i < 0 || i + 1 >= n) return 0.0f;
        const float f = (float) (abs - (double) i);
        const float* d = buf.data (c);
        return d[i] + (d[i + 1] - d[i]) * f;
    }

    void SliceVoice::render (float* const* out, int numChannels, int startSample, int numSamples) noexcept
    {
        if (! active) return;
        const double stretch = clampv (s.stretch, 0.25, 8.0);
        const bool cyclic = std::abs (stretch - 1.0) > 0.01;
        const double headInc = s.inTime ? s.rate : s.rate / stretch;

        for (int i = 0; i < numSamples; ++i)
        {
            // End of this stutter repeat?
            if (head >= windowLen)
            {
                if (++repeat >= repeats) { active = false; content = nullptr; return; }
                head = 0.0;
                grains.reset();
            }

            float g = s.level;
            if (age < s.fadeSamples) g *= (float) age / (float) s.fadeSamples;                     // fade in
            if (age >= s.gateSamples && releaseLeft < 0) release (s.fadeSamples);                 // gate
            if (releaseLeft >= 0)
            {
                g *= (float) releaseLeft / (float) releaseLen;
                if (--releaseLeft <= 0) { active = false; content = nullptr; return; }
            }
            // Fade the last ms of a one-shot so it never clicks at the region edge
            const double left = (windowLen - head) / std::max (1e-9, headInc);
            if (left < (double) s.fadeSamples) g *= (float) std::max (0.0, left / (double) s.fadeSamples);

            envDecay *= envCoef;
            const float env = envDecay * (1.0f + tailLift * std::min (1.0f, (float) age / (float) (0.02 * sampleRate)));
            g *= env;

            if (cyclic) grains.tick (head);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float v = cyclic ? grains.sample ([this, ch] (double p) { return read (ch, p); }) : read (ch, head);
                out[ch][startSample + i] += v * g;
            }

            head += headInc;
            if (cyclic) grains.advance (s.inTime ? s.rate * s.grainPitch : s.rate);
            ++age;
        }
    }
}
