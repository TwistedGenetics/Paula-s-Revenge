#pragma once
/*
    AkaiGrains: the Akaizer v1.2 grain scheme, live. Used by the AKAIZE pad and per-slice STRETCH.

    The original (see Akaizer.h) lays a half-sine window of 2 x CYCLE LENGTH samples over the input every
    sampleRate / CYCLE FREQ output samples, reading the input at its own speed from a point that moves at
    1 / stretch, and adds the grains up. Here that runs one output sample at a time:

        tick (srcPos)     every hop: a new grain starts reading at srcPos
        tickInTime (srcPos, stretch, rate)
                          AKAIZE MODE IN TIME: the grains read as they would stretched by `stretch` (each group of
                          about `stretch` grains moves through the source at 1 / stretch), then the next group jumps
                          on to where the beat is - the Akai stretch's metallic sound, the beat kept
        sample (read)     sum of every live grain: read (grain position) x sin (pi x (age + 1) / L)
        advance (rate)    every grain reads on by rate (1 = the source's own speed, more = pitched up)

    The original adds without scaling (and clips to 16 bits). Live, the sum is scaled by the average overlap so
    the level stays where the dry sound was; the rendered file (Akaizer.h) stays exactly the original.
*/
#include <algorithm>
#include <array>
#include <cmath>

namespace bc
{
    class AkaiGrains
    {
    public:
        static constexpr int kMaxGrains = 32;   // more overlap than this (very high CYCLE FREQ) drops the oldest

        /** cycleLength in samples (grain = twice that), cycleFreq in grains per second, at sampleRate. */
        void setup (int cycleLength, double cycleFreq, double sampleRate) noexcept
        {
            length = std::max (2, 2 * std::max (1, cycleLength));
            hop = std::max (1.0, sampleRate / std::max (1.0, cycleFreq));
            const double overlap = (2.0 / 3.14159265358979) * (double) length / hop;   // mean window sum
            norm = (float) (1.0 / std::max (1.0, overlap));
            step = 1.0 / (double) length;
        }

        void reset() noexcept { count = 0; sinceHop = 1.0e9; groupLeft = 0; }

        /** Call once per output sample, before sample(): starts a grain at srcPos when one is due. */
        void tick (double srcPos) noexcept
        {
            if (sinceHop >= hop) start (srcPos);
            sinceHop += 1.0;
        }

        /** IN TIME: as tick(), but the grains move through the source as a stretch by `stretch` would (rate: how
            far the source moves per output sample at normal speed), and every group of round (stretch) grains starts
            again from srcPos (where the beat is now). At stretch 1 it's the same as tick(). */
        void tickInTime (double srcPos, double stretch, double rate = 1.0) noexcept
        {
            if (sinceHop >= hop)
            {
                stretch = std::max (1.0, stretch);
                if (groupLeft <= 0)
                {
                    groupStart = srcPos;
                    groupIndex = 0;
                    groupLeft = std::max (1, (int) std::lround (stretch));
                }
                start (groupStart + (double) groupIndex * hop * rate / stretch);
                ++groupIndex;
                --groupLeft;
            }
            sinceHop += 1.0;
        }

        /** The windowed sum for one channel; read (pos) returns the source at a position. */
        template <typename Read>
        float sample (Read&& read) const noexcept
        {
            float v = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                const auto& g = grains[(size_t) k];
                v += read (g.pos) * window ((double) (g.age + 1) * step);
            }
            return v * norm;
        }

        /** After every channel has been read: grains move on, finished ones go. */
        void advance (double rate) noexcept
        {
            int w = 0;
            for (int k = 0; k < count; ++k)
            {
                auto g = grains[(size_t) k];
                g.pos += rate;
                if (++g.age < length) grains[(size_t) w++] = g;
            }
            count = w;
        }

        bool active() const noexcept { return count > 0; }

    private:
        struct Grain { double pos; int age; };

        void start (double srcPos) noexcept
        {
            sinceHop = sinceHop > 2.0 * hop ? 0.0 : sinceHop - hop;
            if (count == kMaxGrains)
            {
                for (int k = 1; k < count; ++k) grains[(size_t) (k - 1)] = grains[(size_t) k];
                --count;
            }
            grains[(size_t) count++] = { srcPos, 0 };
        }

        // sin (pi x t) for t in 0..1, from a table
        static float window (double t) noexcept
        {
            static const auto table = [] {
                std::array<float, 1025> a {};
                for (size_t i = 0; i < a.size(); ++i) a[i] = (float) std::sin (3.14159265358979 * (double) i / 1024.0);
                return a;
            }();
            if (t <= 0.0 || t >= 1.0) return 0.0f;
            const double x = t * 1024.0;
            const int i = (int) x;
            const float f = (float) (x - i);
            return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
        }

        std::array<Grain, kMaxGrains> grains {};
        int count = 0;
        int length = 1600;
        double hop = 860.0, sinceHop = 1.0e9, step = 1.0 / 1600.0;
        float norm = 1.0f;
        double groupStart = 0.0;           // IN TIME: where this group of grains started in the source
        int groupIndex = 0, groupLeft = 0;
    };
}
