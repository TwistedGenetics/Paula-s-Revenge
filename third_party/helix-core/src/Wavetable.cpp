#include "helix/Wavetable.h"
#include "helix/FFT.h"
#include "helix/Math.h"
#include <cmath>
#include <complex>

namespace helix {

static bool isPow2(int n) { return n > 0 && (n & (n - 1)) == 0; }
static int log2i(int n) { int l = 0; while ((1 << l) < n) ++l; return l; }

void resampleCycle(const float* src, int srcLen, float* dst, int dstLen)
{
    if (srcLen <= 0) { for (int i = 0; i < dstLen; ++i) dst[i] = 0.0f; return; }
    for (int i = 0; i < dstLen; ++i)
    {
        const double pos = (double)i * srcLen / dstLen;
        const int i1 = (int)pos;
        const float t = (float)(pos - i1);
        auto at = [&](int k) { k %= srcLen; if (k < 0) k += srcLen; return src[k]; };
        const float y0 = at(i1 - 1), y1 = at(i1), y2 = at(i1 + 1), y3 = at(i1 + 2);
        // Catmull-Rom
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        dst[i] = ((c3 * t + c2) * t + c1) * t + c0;
    }
}

std::shared_ptr<Wavetable> Wavetable::fromFrames(const float* frames, int numFrames, int frameLen,
                                                 const std::string& name, bool normalise)
{
    numFrames = clamp(numFrames, 1, kMaxFrames);
    std::shared_ptr<Wavetable> wt(new Wavetable());
    wt->numFrames_ = numFrames;
    wt->name_ = name;

    size_t total = 0;
    for (int L = 0; L < kNumMipLevels; ++L)
    {
        // Linear interpolation makes an image of harmonic h at (N - h) whose level is about (h / N)^2 relative to
        // h. The top harmonic (amplitude ~1/maxH for a saw) is the worst case, so the image is ~maxH / N^2.
        // N = sqrt(4000 * maxH) keeps it near -72 dB; small levels get up to 32x oversampling, large ones 2-4x.
        int size = 256;
        while (size < kFrameSize && (double)size * size < 4000.0 * mipMaxHarmonic(L)) size <<= 1;
        wt->levelSize_[L] = size;
        wt->levelShift_[L] = 32 - log2i(size);
        wt->levelOffset_[L] = total;
        total += (size_t)numFrames * (size_t)(size + 1);
    }
    wt->data_.assign(total, 0.0f);

    // Work at a power-of-two source length of at least kFrameSize.
    const bool usable = isPow2(frameLen) && frameLen >= 8 && frameLen <= 65536;
    const int srcLen = usable ? frameLen : kFrameSize;
    std::vector<float> cycle((size_t)srcLen);
    std::vector<std::complex<double>> spec((size_t)srcLen), tmp;

    for (int f = 0; f < numFrames; ++f)
    {
        const float* in = frames + (size_t)f * (size_t)frameLen;
        if (usable) std::copy(in, in + frameLen, cycle.begin());
        else resampleCycle(in, frameLen, cycle.data(), srcLen);

        for (int i = 0; i < srcLen; ++i) spec[(size_t)i] = std::complex<double>(cycle[(size_t)i], 0.0);
        fft(spec, false);
        const int srcHarm = srcLen / 2;

        for (int L = 0; L < kNumMipLevels; ++L)
        {
            const int size = wt->levelSize_[L];
            const int maxH = std::min({mipMaxHarmonic(L), srcHarm - 1, size / 2 - 1});
            tmp.assign((size_t)size, std::complex<double>(0.0, 0.0));
            const double scale = 1.0 / (double)srcLen;   // forward FFT -> amplitude, inverse unscaled
            for (int h = 1; h <= maxH; ++h)              // h = 0 (DC) dropped on purpose
            {
                tmp[(size_t)h] = spec[(size_t)h] * scale;
                tmp[(size_t)(size - h)] = spec[(size_t)(srcLen - h)] * scale;
            }
            fft(tmp, true);
            float* dst = wt->data_.data() + wt->levelOffset_[L] + (size_t)f * (size_t)(size + 1);
            for (int i = 0; i < size; ++i) dst[i] = (float)tmp[(size_t)i].real();
            dst[size] = dst[0];
        }
    }

    if (normalise)
    {
        // Normalise on the full-band level so every level shares one gain.
        float peak = 0.0f;
        for (int f = 0; f < numFrames; ++f)
        {
            const float* p = wt->frame(0, f);
            for (int i = 0; i < kFrameSize; ++i) peak = std::max(peak, std::fabs(p[i]));
        }
        if (peak > 1e-6f)
        {
            const float g = 1.0f / peak;
            for (float& s : wt->data_) s *= g;
        }
    }
    return wt;
}

} // namespace helix
