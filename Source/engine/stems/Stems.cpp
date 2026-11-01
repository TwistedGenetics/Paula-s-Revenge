// Paula's Revenge - STEMS: everything around the Demucs network (see Stems.h)
#include "Stems.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace prv::stems
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

const char* sourceName (int s)
{
    static const char* names[kSources] = { "DRUMS", "BASS", "OTHER", "VOCALS" };
    return s >= 0 && s < kSources ? names[s] : "";
}

// ---------------------------------------------------------------- FFT
Fft::Fft() : tw ((size_t) kFft / 2), rev ((size_t) kFft)
{
    for (int i = 0; i < kFft / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / kFft));
    int bits = 0; while ((1 << bits) < kFft) ++bits;
    for (int i = 0; i < kFft; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev[(size_t) i] = r;
    }
}

void Fft::run (std::complex<float>* d, bool inv) const
{
    for (int i = 0; i < kFft; ++i) if (i < rev[(size_t) i]) std::swap (d[i], d[rev[(size_t) i]]);
    for (int len = 2; len <= kFft; len <<= 1)
    {
        const int half = len / 2, step = kFft / len;
        for (int i = 0; i < kFft; i += len)
            for (int j = 0; j < half; ++j)
            {
                auto w = tw[(size_t) (j * step)];
                if (inv) w = std::conj (w);
                const auto u = d[i + j], v = d[i + j + half] * w;
                d[i + j] = u + v;
                d[i + j + half] = u - v;
            }
    }
}

void Fft::forward (std::complex<float>* d) const { run (d, false); }
void Fft::inverse (std::complex<float>* d) const
{
    run (d, true);
    const float s = 1.0f / (float) kFft;
    for (int i = 0; i < kFft; ++i) d[i] *= s;
}

namespace
{
    const std::vector<float>& hann()
    {
        static const std::vector<float> w = [] {
            std::vector<float> v ((size_t) kFft);
            for (int i = 0; i < kFft; ++i) v[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFft));   // (periodic)
            return v;
        }();
        return w;
    }
}

std::vector<float> reflectPad (const float* x, int n, int left, int right)
{
    std::vector<float> out ((size_t) (n + left + right));
    auto at = [&] (int i)
    {
        // reflect without repeating the edge, as often as it takes
        if (n == 1) return x[0];
        const int period = 2 * (n - 1);
        i %= period; if (i < 0) i += period;
        return x[i < n ? i : period - i];
    };
    for (int i = 0; i < (int) out.size(); ++i) out[(size_t) i] = at (i - left);
    return out;
}

void stft (const Fft& fft, const float* x, int n, std::vector<std::complex<float>>& out, int& frames)
{
    const auto padded = reflectPad (x, n, kFft / 2, kFft / 2);
    frames = 1 + n / kHop;
    out.assign ((size_t) frames * (kBins + 1), {});
    const auto& w = hann();
    const float norm = 1.0f / std::sqrt ((float) kFft);
    std::vector<std::complex<float>> buf ((size_t) kFft);
    for (int t = 0; t < frames; ++t)
    {
        const float* src = padded.data() + (size_t) t * kHop;
        for (int i = 0; i < kFft; ++i) buf[(size_t) i] = { src[i] * w[(size_t) i], 0.0f };
        fft.forward (buf.data());
        auto* o = out.data() + (size_t) t * (kBins + 1);
        for (int b = 0; b <= kBins; ++b) o[b] = buf[(size_t) b] * norm;
    }
}

void istft (const Fft& fft, const std::complex<float>* spec, int frames, int length, float* out)
{
    const auto& w = hann();
    const int total = kFft + kHop * (frames - 1);
    std::vector<float> y ((size_t) total, 0.0f), env ((size_t) total, 0.0f);
    std::vector<std::complex<float>> buf ((size_t) kFft);
    const float norm = std::sqrt ((float) kFft);
    for (int t = 0; t < frames; ++t)
    {
        const auto* s = spec + (size_t) t * (kBins + 1);
        // the full spectrum from its first half (a real signal); DC and Nyquist real
        buf[0] = { s[0].real() * norm, 0.0f };
        buf[(size_t) kBins] = { s[kBins].real() * norm, 0.0f };
        for (int b = 1; b < kBins; ++b) { buf[(size_t) b] = s[b] * norm; buf[(size_t) (kFft - b)] = std::conj (s[b] * norm); }
        fft.inverse (buf.data());
        const size_t at = (size_t) t * kHop;
        for (int i = 0; i < kFft; ++i)
        {
            y[at + (size_t) i] += buf[(size_t) i].real() * w[(size_t) i];
            env[at + (size_t) i] += w[(size_t) i] * w[(size_t) i];
        }
    }
    const int start = kFft / 2;
    for (int i = 0; i < length; ++i)
    {
        const int k = start + i;
        out[i] = (k < total && env[(size_t) k] > 1.0e-11f) ? y[(size_t) k] / env[(size_t) k] : 0.0f;
    }
}

// ---------------------------------------------------------------- rate change
std::vector<float> resample (const std::vector<float>& in, double from, double to)
{
    if (in.empty() || from <= 0 || to <= 0 || std::abs (from - to) < 1.0e-9) return in;
    // Kaiser-windowed sinc, 32 zero crossings each side, cut off a little below the lower Nyquist
    const double ratio = to / from;
    const double cutoff = 0.97 * std::min (1.0, ratio);
    const int zeros = 32;
    const double halfLen = zeros / cutoff;            // (in input samples)
    const double beta = 9.0;
    auto bessel0 = [] (double x)
    {
        double s = 1, t = 1;
        for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
        return s;
    };
    const double ib = 1.0 / bessel0 (beta);
    const size_t outLen = (size_t) std::max (1.0, std::floor ((double) in.size() * ratio));
    std::vector<float> out (outLen);
    for (size_t k = 0; k < outLen; ++k)
    {
        const double pos = (double) k / ratio;
        const auto a = (int64_t) std::ceil (pos - halfLen), b = (int64_t) std::floor (pos + halfLen);
        double sum = 0, wsum = 0;
        for (int64_t i = std::max<int64_t> (0, a); i <= std::min<int64_t> ((int64_t) in.size() - 1, b); ++i)
        {
            const double d = (double) i - pos;
            const double xx = d * cutoff;
            const double sinc = std::abs (xx) < 1e-12 ? 1.0 : std::sin (kPi * xx) / (kPi * xx);
            const double r = d / halfLen;
            const double win = std::abs (r) >= 1 ? 0.0 : bessel0 (beta * std::sqrt (1 - r * r)) * ib;
            const double h = sinc * win;
            sum += in[(size_t) i] * h;
            wsum += h;
        }
        out[k] = (float) (wsum != 0 ? sum / wsum : 0.0);
    }
    return out;
}

// ---------------------------------------------------------------- one piece
bool separatePiece (DemucsModel& model, const float* left, const float* right, std::vector<float>& out, std::string& error)
{
    static const Fft fft;
    const int nsrc = model.sources();
    std::vector<float> xt ((size_t) 2 * kSegment), x ((size_t) 4 * kBins * kFrames);
    std::vector<std::complex<float>> spec;
    int frames = 0;
    for (int c = 0; c < 2; ++c)
    {
        const float* ch = c == 0 ? left : right;
        std::copy (ch, ch + kSegment, xt.begin() + (size_t) c * kSegment);
        // demucs' _spec: reflect-padded (1536, then up to a whole number of hops + 1536), then the
        // STFT, its last bin and 2 frames at each end left out
        const auto padded = reflectPad (ch, kSegment, kPad, kPadded - kPad - kSegment);
        stft (fft, padded.data(), kPadded, spec, frames);
        for (int b = 0; b < kBins; ++b)
            for (int k = 0; k < kFrames; ++k)
            {
                const auto z = spec[(size_t) (k + 2) * (kBins + 1) + (size_t) b];
                x[((size_t) (2 * c) * kBins + (size_t) b) * kFrames + (size_t) k] = z.real();
                x[((size_t) (2 * c + 1) * kBins + (size_t) b) * kFrames + (size_t) k] = z.imag();
            }
    }
    std::vector<float> xOut ((size_t) nsrc * 4 * kBins * kFrames), xtOut ((size_t) nsrc * 2 * kSegment);
    if (! model.run (xt.data(), x.data(), xOut.data(), xtOut.data(), error)) return false;
    // each source: its spectrogram back to sound (_ispec), plus its time branch
    out.assign ((size_t) nsrc * 2 * kSegment, 0.0f);
    std::vector<std::complex<float>> z ((size_t) (kFrames + 4) * (kBins + 1));
    std::vector<float> wave ((size_t) kPadded);
    for (int s = 0; s < nsrc; ++s)
        for (int c = 0; c < 2; ++c)
        {
            std::fill (z.begin(), z.end(), std::complex<float> {});
            const float* re = xOut.data() + (((size_t) s * 4 + (size_t) (2 * c)) * kBins) * kFrames;
            const float* im = xOut.data() + (((size_t) s * 4 + (size_t) (2 * c + 1)) * kBins) * kFrames;
            for (int b = 0; b < kBins; ++b)
                for (int k = 0; k < kFrames; ++k)
                    z[(size_t) (k + 2) * (kBins + 1) + (size_t) b] = { re[(size_t) b * kFrames + (size_t) k], im[(size_t) b * kFrames + (size_t) k] };
            istft (fft, z.data(), kFrames + 4, kPadded, wave.data());
            float* o = out.data() + ((size_t) s * 2 + (size_t) c) * kSegment;
            const float* t = xtOut.data() + ((size_t) s * 2 + (size_t) c) * kSegment;
            for (int i = 0; i < kSegment; ++i) o[i] = wave[(size_t) (kPad + i)] + t[i];
        }
    return true;
}

// ---------------------------------------------------------------- the whole sample
bool separate (DemucsModel& model, const std::vector<float>& left, const std::vector<float>& right,
               std::vector<std::vector<float>>& outL, std::vector<std::vector<float>>& outR, Progress& progress, std::string& error)
{
    const int64_t n = (int64_t) left.size();
    const int nsrc = model.sources();
    outL.assign ((size_t) nsrc, std::vector<float> ((size_t) n, 0.0f));
    outR.assign ((size_t) nsrc, std::vector<float> ((size_t) n, 0.0f));
    if (n == 0) return true;
    // the level: (x - mean) / std of the mono mix (and back after)
    double mean = 0;
    for (int64_t i = 0; i < n; ++i) mean += 0.5 * ((double) left[(size_t) i] + right[(size_t) i]);
    mean /= (double) n;
    double var = 0;
    for (int64_t i = 0; i < n; ++i) { const double m = 0.5 * ((double) left[(size_t) i] + right[(size_t) i]) - mean; var += m * m; }
    double sd = n > 1 ? std::sqrt (var / (double) (n - 1)) : 1.0;
    if (! (sd > 1.0e-8)) sd = 1.0e-8;   // (silence)
    // the pieces' cross-fade: a triangle, 1 .. S/2, S/2 .. 1
    std::vector<float> weight ((size_t) kSegment);
    const int half = kSegment / 2;
    for (int i = 0; i < half; ++i) weight[(size_t) i] = (float) (i + 1);
    for (int i = half; i < kSegment; ++i) weight[(size_t) i] = (float) (kSegment - i);
    const float wmax = *std::max_element (weight.begin(), weight.end());
    for (auto& v : weight) v /= wmax;
    std::vector<float> sumW ((size_t) n, 0.0f);
    const int stride = (int) ((1.0 - 0.25) * kSegment);
    const int pieces = (int) ((n + stride - 1) / stride);
    std::vector<float> pl ((size_t) kSegment), pr ((size_t) kSegment), res;
    int done = 0;
    for (int64_t offset = 0; offset < n; offset += stride)
    {
        if (progress.cancel != nullptr && progress.cancel->load()) { error = "CANCELLED"; return false; }
        const int64_t c = std::min<int64_t> (kSegment, n - offset);
        // the piece, centred in a whole segment (the mix around it where there is some, else silence)
        const int64_t delta = kSegment - c;
        const int64_t start = offset - delta / 2;
        for (int64_t i = 0; i < kSegment; ++i)
        {
            const int64_t k = start + i;
            const bool in = k >= 0 && k < n;
            pl[(size_t) i] = in ? (float) ((left[(size_t) k] - mean) / sd) : 0.0f;
            pr[(size_t) i] = in ? (float) ((right[(size_t) k] - mean) / sd) : 0.0f;
        }
        if (! separatePiece (model, pl.data(), pr.data(), res, error)) return false;
        // its middle c samples, cross-faded in
        const int64_t trim = delta / 2;
        for (int s = 0; s < nsrc; ++s)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = res.data() + ((size_t) s * 2 + (size_t) ch) * kSegment + (size_t) trim;
                auto& dst = ch == 0 ? outL[(size_t) s] : outR[(size_t) s];
                for (int64_t i = 0; i < c; ++i) dst[(size_t) (offset + i)] += weight[(size_t) i] * src[i];
            }
        for (int64_t i = 0; i < c; ++i) sumW[(size_t) (offset + i)] += weight[(size_t) i];
        ++done;
        if (progress.report) progress.report ((double) done / pieces);
    }
    for (int s = 0; s < nsrc; ++s)
        for (int64_t i = 0; i < n; ++i)
        {
            const float w = sumW[(size_t) i] > 0 ? sumW[(size_t) i] : 1.0f;
            outL[(size_t) s][(size_t) i] = (float) (outL[(size_t) s][(size_t) i] / w * sd + mean);
            outR[(size_t) s][(size_t) i] = (float) (outR[(size_t) s][(size_t) i] / w * sd + mean);
        }
    return true;
}

std::vector<std::string> modelsFor (Quality q)
{
    if (q == Quality::Fast) return { "htdemucs.onnx" };
    return { "htdemucs_ft_drums.onnx", "htdemucs_ft_bass.onnx", "htdemucs_ft_other.onnx", "htdemucs_ft_vocals.onnx" };
}

bool separateSample (Quality q, const ModelPath& path, const std::vector<float>& left, const std::vector<float>& right, double rate,
                     StemResult& result, Progress& progress, std::string& error, int threads)
{
    if (left.empty()) { error = "THE SAMPLE IS EMPTY"; return false; }
    // the model's rate (and stereo)
    std::vector<float> L = resample (left, rate, kRate);
    std::vector<float> R = right.size() == left.size() ? resample (right, rate, kRate) : L;
    R.resize (L.size(), 0.0f);
    const auto files = modelsFor (q);
    result = {};
    result.rate = kRate;
    for (size_t m = 0; m < files.size(); ++m)
    {
        const auto p = path (files[m]);
        if (p.empty()) { error = "THE MODEL " + files[m] + " ISN'T DOWNLOADED"; return false; }
        auto model = loadOnnxModel (p, threads, error);
        if (model == nullptr) return false;
        if (model->sources() != kSources) { error = "THE MODEL " + files[m] + " ISN'T A 4-STEM ONE"; return false; }
        Progress part;
        part.cancel = progress.cancel;
        const double base = (double) m / (double) files.size(), span = 1.0 / (double) files.size();
        part.report = [&] (double f) { if (progress.report) progress.report (base + f * span); };
        std::vector<std::vector<float>> oL, oR;
        if (! separate (*model, L, R, oL, oR, part, error)) return false;
        if (q == Quality::Fast)
            for (int s = 0; s < kSources; ++s) { result.left[s] = std::move (oL[(size_t) s]); result.right[s] = std::move (oR[(size_t) s]); }
        else
        {
            // (each fine-tuned model is the best at its own source)
            result.left[m] = std::move (oL[m]);
            result.right[m] = std::move (oR[m]);
        }
    }
    return true;
}
} // namespace prv::stems
