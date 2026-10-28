// Paula's Revenge - SAMPLE: the take recorder and what's done to a take before it's kept
#include "Recorder.h"

#include <algorithm>
#include <cmath>

namespace prv
{
// ------------------------------------------------------------------ the recorder
void TakeRecorder::arm (double sampleRate, double maxSecs, RecTrigger trigger, float thresholdLinear, double prerollSeconds)
{
    st.store (State::Idle, std::memory_order_release);
    rate = std::max (1.0, sampleRate);
    capacity = std::max<int64_t> (1, (int64_t) std::llround (maxSecs * rate));
    bufL.assign ((size_t) capacity, 0.0f);
    bufR.assign ((size_t) capacity, 0.0f);
    const int pre = std::max (1, (int) std::lround (prerollSeconds * rate));
    preL.assign ((size_t) pre, 0.0f);
    preR.assign ((size_t) pre, 0.0f);
    prePos = preFill = 0;
    trig = trigger;
    threshold = std::max (1.0e-5f, thresholdLinear);
    written.store (0, std::memory_order_release);
    full = false;
    st.store (trigger == RecTrigger::Manual ? State::Recording : State::Armed, std::memory_order_release);
}

void TakeRecorder::stop()
{
    auto s = st.load();
    if (s == State::Recording) st.store (State::Done, std::memory_order_release);
    else if (s == State::Armed) reset();
}

void TakeRecorder::reset()
{
    st.store (State::Idle, std::memory_order_release);
    written.store (0);
    full = false;
    std::vector<float>().swap (bufL);
    std::vector<float>().swap (bufR);
    std::vector<float>().swap (preL);
    std::vector<float>().swap (preR);
    capacity = 0;
}

void TakeRecorder::takeAway (std::vector<float>& left, std::vector<float>& right)
{
    const auto n = (size_t) frames();
    bufL.resize (n);
    bufR.resize (n);
    left.swap (bufL);
    right.swap (bufR);
    reset();
}

float TakeRecorder::takePeak (int channel)
{
    return (channel == 0 ? peakL : peakR).exchange (0.0f);
}

bool TakeRecorder::takeClip() { return clipped.exchange (false); }

void TakeRecorder::recentFrames (std::vector<float>& out, int count) const
{
    out.clear();
    const int64_t n = frames();
    if (bufL.empty() || n <= 0) return;
    const int64_t from = std::max<int64_t> (0, n - count);
    for (int64_t i = from; i < n; ++i) out.push_back (0.5f * (bufL[(size_t) i] + bufR[(size_t) i]));
}

void TakeRecorder::write (const float* l, const float* r, int n)
{
    int64_t w = written.load (std::memory_order_relaxed);
    const int64_t room = capacity - w;
    const int k = (int) std::min<int64_t> (n, room);
    if (k > 0)
    {
        std::copy (l, l + k, bufL.begin() + w);
        std::copy (r, r + k, bufR.begin() + w);
        w += k;
        written.store (w, std::memory_order_release);
    }
    if (w >= capacity) { full = true; st.store (State::Done, std::memory_order_release); }
}

void TakeRecorder::process (const float* l, const float* r, int n, bool playing)
{
    // the meter hears it whatever the state
    float pl = 0, pr = 0;
    for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (l[i])); pr = std::max (pr, std::abs (r[i])); }
    if (pl > peakL.load (std::memory_order_relaxed)) peakL.store (pl, std::memory_order_relaxed);
    if (pr > peakR.load (std::memory_order_relaxed)) peakR.store (pr, std::memory_order_relaxed);
    if (pl >= 0.999f || pr >= 0.999f) clipped = true;

    const auto s = st.load (std::memory_order_acquire);
    if (s == State::Armed)
    {
        if (trig == RecTrigger::WithPlayback)
        {
            if (! playing) return;
            st.store (State::Recording, std::memory_order_release);
            write (l, r, n);
            return;
        }
        // THRESHOLD: keep a short pre-roll; the first sample over the level starts the take
        const int pre = (int) preL.size();
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (l[i]) >= threshold || std::abs (r[i]) >= threshold)
            {
                // the pre-roll in order, then this block from here
                const int start = (prePos - preFill + pre) % pre;
                for (int k = 0; k < preFill; ++k)
                {
                    const int idx = (start + k) % pre;
                    write (&preL[(size_t) idx], &preR[(size_t) idx], 1);
                }
                if (st.load() == State::Done) return;
                st.store (State::Recording, std::memory_order_release);
                write (l + i, r + i, n - i);
                return;
            }
            preL[(size_t) prePos] = l[i];
            preR[(size_t) prePos] = r[i];
            prePos = (prePos + 1) % pre;
            preFill = std::min (preFill + 1, pre);
        }
        return;
    }
    if (s != State::Recording) return;
    if (trig == RecTrigger::WithPlayback && ! playing) { st.store (State::Done, std::memory_order_release); return; }
    write (l, r, n);
}

// ------------------------------------------------------------------ after recording
void findTrim (const std::vector<float>& left, const std::vector<float>& right, float floor, int padFrames, int64_t& start, int64_t& end)
{
    const int64_t n = (int64_t) left.size();
    auto loud = [&] (int64_t i) { return std::abs (left[(size_t) i]) > floor || (i < (int64_t) right.size() && std::abs (right[(size_t) i]) > floor); };
    start = 0; end = n;
    while (start < n && ! loud (start)) ++start;
    if (start >= n) { start = 0; end = 0; return; }   // (all silence)
    while (end > start && ! loud (end - 1)) --end;
    start = std::max<int64_t> (0, start - padFrames);
    end = std::min<int64_t> (n, end + padFrames);
}

void normalise (std::vector<float>& left, std::vector<float>& right, float peak)
{
    float mx = 0;
    for (float v : left) mx = std::max (mx, std::abs (v));
    for (float v : right) mx = std::max (mx, std::abs (v));
    if (mx < 1.0e-6f) return;
    const float g = peak / mx;
    for (auto& v : left) v *= g;
    for (auto& v : right) v *= g;
}

namespace
{
    // a 2nd-order low-pass (RBJ), run twice: steep enough before going down in rate
    void lowPass (std::vector<float>& x, double cutoff, double rate)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (cutoff, rate * 0.49) / rate;
        const double q = 0.7071, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        const double a0 = 1.0 + alpha;
        const double b0 = (1.0 - c) / 2.0 / a0, b1 = (1.0 - c) / a0, b2 = b0, a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;
        for (int pass = 0; pass < 2; ++pass)
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : x)
            {
                const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = v; y2 = y1; y1 = y;
                v = (float) y;
            }
        }
    }
}

std::vector<float> resample (const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0 || toRate <= 0 || std::abs (fromRate - toRate) < 1.0e-6) return in;
    std::vector<float> src (in);
    if (toRate < fromRate) lowPass (src, toRate * 0.45, fromRate);
    const double step = fromRate / toRate;
    const auto outLen = (size_t) std::max (1.0, std::floor ((double) src.size() / step));
    std::vector<float> out (outLen);
    const auto at = [&] (int64_t i) { return src[(size_t) std::clamp<int64_t> (i, 0, (int64_t) src.size() - 1)]; };
    for (size_t k = 0; k < outLen; ++k)
    {
        // 4-point Hermite
        const double pos = (double) k * step;
        const auto i = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i);
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        out[k] = ((c3 * t + c2) * t + c1) * t + x0;
    }
    return out;
}

bool takeToInstrument (const std::vector<float>& left, const std::vector<float>& right, double rate, const TakeOptions& o, Instrument& out)
{
    if (left.empty() || rate <= 0) return false;
    // the channels
    std::vector<float> L, R;
    const bool hasR = right.size() == left.size();
    switch (o.channels)
    {
        case RecChannels::Stereo: L = left; if (hasR) R = right; break;
        case RecChannels::Mono:   L = left; if (hasR) for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (left[i] + right[i]); break;
        case RecChannels::Left:   L = left; break;
        case RecChannels::Right:  L = hasR ? right : left; break;
    }
    // a stereo take that's the same on both sides is mono
    if (! R.empty())
    {
        bool same = true;
        for (size_t i = 0; i < L.size() && same; ++i) same = std::abs (L[i] - R[i]) < 1.0e-6f;
        if (same) R.clear();
    }
    if (o.trimSilence)
    {
        int64_t a = 0, b = 0;
        findTrim (L, R, 0.001f, (int) (rate * 0.002), a, b);   // -60 dB, 2 ms kept before the start
        if (b <= a) return false;                               // (nothing but silence)
        L = std::vector<float> (L.begin() + a, L.begin() + b);
        if (! R.empty()) R = std::vector<float> (R.begin() + a, R.begin() + b);
    }
    if (o.normalise) normalise (L, R);
    if (o.bits > 0 && o.bits < 16)
    {
        // a vintage take: back on the sampler's grid after normalising
        for (auto& v : L) v = VintageAdc::quantise (v, o.bits);
        for (auto& v : R) v = VintageAdc::quantise (v, o.bits);
    }
    double playRate = rate;
    if (o.amigaRate > 0 && o.bits == 0)
    {
        L = resample (L, rate, o.amigaRate);
        if (! R.empty()) { R = resample (R, rate, o.amigaRate); R.resize (L.size(), 0.0f); }
        playRate = o.amigaRate;
    }
    const float scale = o.bits > 0 && o.bits < 16 ? 32768.0f : 32767.0f;   // (a vintage take: exactly on its bits)
    auto to16 = [scale] (const std::vector<float>& v)
    {
        std::vector<int16_t> d (v.size());
        for (size_t i = 0; i < v.size(); ++i) d[i] = (int16_t) std::clamp ((int) std::lround (v[i] * scale), -32768, 32767);
        return d;
    };
    out.clear();
    out.name = o.name.substr (0, 22);
    out.data = to16 (L);
    if (! R.empty()) out.dataR = to16 (R);
    out.rate = std::clamp (playRate, 500.0, 192000.0);
    out.natRate = out.rate;
    return ! out.data.empty();
}

// ------------------------------------------------------------------ vintage sampling
const double kAmigaSampleRates[4] = { 8363, 16726, 22050, 28867 };
const double kS950SampleRates[8] = { 7500, 10000, 15000, 22050, 24000, 32000, 40000, 48000 };

float VintageAdc::Biquad::run (float x)
{
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return (float) y;
}

void VintageAdc::Biquad::lowpass (double fc, double fs, double q)
{
    const double w = 2.0 * 3.14159265358979323846 * std::clamp (fc, 20.0, fs * 0.49) / fs;
    const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
    b0 = (1.0 - c) / 2.0 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
}

void VintageAdc::prepare (double hostRate, int)
{
    host = hostRate > 0 ? hostRate : 48000.0;
    set (cfg);
}

void VintageAdc::reset()
{
    phase = 1.0; heldL = heldR = 0;
    for (auto* f : { aaL, aaR, outL, outR }) for (int k = 0; k < 3; ++k) f[k].z1 = f[k].z2 = 0;
}

void VintageAdc::set (const VintageSettings& s)
{
    cfg = s;
    cfg.bits = std::clamp (cfg.bits, 4, 16);
    cfg.driveDb = std::clamp (cfg.driveDb, 0.0f, 24.0f);
    gain = std::pow (10.0f, cfg.driveDb / 20.0f);
    // the S950's 6-pole Butterworth: three sections, the corner just under half the rate
    filtered = cfg.model == SamplerModel::S950 && cfg.rate > 0;
    if (filtered)
    {
        const double fc = std::min (cfg.rate * 0.45, host * 0.45);
        const double qs[3] = { 0.5176, 0.7071, 1.9319 };
        for (int k = 0; k < 3; ++k) { aaL[k].lowpass (fc, host, qs[k]); aaR[k] = aaL[k]; outL[k] = aaL[k]; outR[k] = aaL[k]; }
    }
    reset();
}

float VintageAdc::inputStage (float x, SamplerModel m, float g)
{
    x *= g;
    if (m == SamplerModel::S950)
    {
        // the input amplifier goes soft above about -4 dB, then the converter's rails
        const float a = std::abs (x);
        if (a > 0.6f) x = std::copysign (0.6f + 0.4f * std::tanh ((a - 0.6f) / 0.4f), x);
    }
    return std::clamp (x, -1.0f, 1.0f);   // (the converter: hard at full scale)
}

float VintageAdc::quantise (float x, int bits)
{
    if (bits >= 16) return x;
    const float levels = (float) (1 << (std::clamp (bits, 2, 16) - 1));
    return std::clamp (std::round (x * levels), -levels, levels - 1.0f) / levels;
}

int VintageAdc::process (const float* l, const float* r, int n, float* monL, float* monR, float* decL, float* decR)
{
    if (! active())
    {
        std::copy (l, l + n, monL); std::copy (r, r + n, monR);
        std::copy (l, l + n, decL); std::copy (r, r + n, decR);
        return n;
    }
    const double step = cfg.rate / host;
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        float xl = inputStage (l[i], cfg.model, gain), xr = inputStage (r[i], cfg.model, gain);
        if (filtered) for (int s = 0; s < 3; ++s) { xl = aaL[s].run (xl); xr = aaR[s].run (xr); }
        phase += step;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            heldL = quantise (xl, cfg.bits);
            heldR = quantise (xr, cfg.bits);
            decL[k] = heldL; decR[k] = heldR; ++k;
        }
        float ml = heldL, mr = heldR;
        if (filtered) for (int s = 0; s < 3; ++s) { ml = outL[s].run (ml); mr = outR[s].run (mr); }
        monL[i] = ml; monR[i] = mr;
    }
    return k;
}
} // namespace prv
