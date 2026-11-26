#pragma once
/*
    SampleData — one decoded break, immutable once built. Shared (std::shared_ptr)
    between the loader, the UI and the audio thread. The Session keeps every one
    it made alive and frees them only on the message / worker side, so the audio
    thread never drops the last reference.
    Audio stays at the file's own rate; voices compensate in their increment.

    SliceTable — up to 64 slice start positions (from Twisted Breaks, limit raised from 16).
*/
#include "choppage/Dsp.h"
#include <memory>
#include <sstream>
#include <string>

namespace bc
{
    struct SampleData
    {
        std::vector<float> ch[2];         // ch[1] empty for mono
        int    channels = 0;
        int    numSamples = 0;
        double sampleRate = 44100.0;
        std::string name;                 // file name without extension
        std::string path;
        // REX loops: the file's own slice points (sample positions) and length in bars (empty / 0 otherwise)
        std::vector<int> fileMarkers;
        int    fileBars = 0;
        double fileBpm = 0.0;

        static constexpr int overviewBucket = 256;
        std::vector<float> overviewMin, overviewMax;

        double lengthSeconds() const noexcept { return sampleRate > 0 ? numSamples / sampleRate : 0.0; }
        const float* data (int c) const noexcept { return ch[c < channels ? c : 0].data(); }

        void allocate (int numChannels, int length)
        {
            channels = clampv (numChannels, 1, 2);
            numSamples = std::max (0, length);
            for (int c = 0; c < 2; ++c) ch[c].assign (c < channels ? (size_t) numSamples : 0, 0.0f);
        }

        void buildOverview()
        {
            const int buckets = (numSamples + overviewBucket - 1) / overviewBucket;
            overviewMin.assign ((size_t) buckets, 0.0f);
            overviewMax.assign ((size_t) buckets, 0.0f);
            for (int b = 0; b < buckets; ++b)
            {
                float lo = 0, hi = 0;
                const int s1 = std::min (numSamples, (b + 1) * overviewBucket);
                for (int c = 0; c < channels; ++c)
                    for (int i = b * overviewBucket; i < s1; ++i) { lo = std::min (lo, ch[c][(size_t) i]); hi = std::max (hi, ch[c][(size_t) i]); }
                overviewMin[(size_t) b] = lo; overviewMax[(size_t) b] = hi;
            }
        }

        /** Min / max over [s0, s1) (uses the overview for wide spans). */
        std::pair<float, float> peakRange (int s0, int s1) const noexcept
        {
            s0 = clampv (s0, 0, numSamples); s1 = clampv (s1, s0, numSamples);
            float lo = 0, hi = 0;
            if (s1 - s0 >= overviewBucket * 2 && ! overviewMin.empty())
            {
                const int b1 = std::min ((int) overviewMin.size(), (s1 + overviewBucket - 1) / overviewBucket);
                for (int b = s0 / overviewBucket; b < b1; ++b) { lo = std::min (lo, overviewMin[(size_t) b]); hi = std::max (hi, overviewMax[(size_t) b]); }
            }
            else
                for (int c = 0; c < channels; ++c)
                    for (int i = s0; i < s1; ++i) { lo = std::min (lo, ch[c][(size_t) i]); hi = std::max (hi, ch[c][(size_t) i]); }
            return { lo, hi };
        }
    };
    using SamplePtr = std::shared_ptr<const SampleData>;

    struct SliceTable
    {
        static constexpr int kMax = 64;
        static constexpr int kMinGap = 64;
        int count = 0;
        std::array<int, kMax> starts {};

        bool isEmpty() const noexcept { return count <= 0; }

        Range region (int slice, int length) const noexcept
        {
            if (count <= 0) return { 0, length };
            slice = clampv (slice, 0, count - 1);
            const int s = clampv (starts[(size_t) slice], 0, length);
            const int e = slice + 1 < count ? starts[(size_t) slice + 1] : length;
            return { s, clampv (e, s, length) };
        }

        void normalise (int length) noexcept
        {
            std::sort (starts.begin(), starts.begin() + count);
            int out = 0;
            for (int i = 0; i < count; ++i)
            {
                const int p = starts[(size_t) i];
                if (p < 0 || p >= length - kMinGap) continue;
                if (out > 0 && p - starts[(size_t) out - 1] < kMinGap) continue;
                starts[(size_t) out++] = p;
            }
            count = std::min (out, kMax);
        }

        static SliceTable equal (int length, int slices) noexcept
        {
            SliceTable t;
            slices = clampv (slices, 1, kMax);
            for (int i = 0; i < slices; ++i) t.starts[(size_t) i] = (int) ((int64_t) length * i / slices);
            t.count = slices;
            t.normalise (length);
            return t;
        }

        int sliceAt (int pos) const noexcept
        {
            int k = 0;
            for (int i = 0; i < count; ++i) if (starts[(size_t) i] <= pos) k = i;
            return k;
        }

        std::string toString() const
        {
            std::string s;
            for (int i = 0; i < count; ++i) { if (i) s += ','; s += std::to_string (starts[(size_t) i]); }
            return s;
        }

        static SliceTable fromString (const std::string& s)
        {
            SliceTable t;
            std::stringstream ss (s);
            std::string tok;
            while (std::getline (ss, tok, ',') && t.count < kMax)
                if (! tok.empty()) t.starts[(size_t) t.count++] = std::atoi (tok.c_str());
            return t;
        }

        bool operator== (const SliceTable& o) const noexcept
        {
            return count == o.count && std::equal (starts.begin(), starts.begin() + count, o.starts.begin());
        }
    };
}
