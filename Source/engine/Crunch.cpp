// Paula's Revenge - CRUNCH (see Crunch.h)
#include "Crunch.h"
#include "Recorder.h"
#include <algorithm>
#include <cmath>

namespace prv
{
const char* crunchName (CrunchStyle s)
{
    switch (s)
    {
        case CrunchStyle::Amiga8: return "AMIGA 8-BIT";
        case CrunchStyle::Sp1200: return "SP-1200";
        case CrunchStyle::Mpc60:  return "MPC60";
    }
    return "";
}

bool crunchInstrument (Instrument& in, CrunchStyle style)
{
    if (in.data.empty() || in.rate <= 0) return false;
    VintageSettings v;
    switch (style)
    {
        case CrunchStyle::Amiga8: v.model = SamplerModel::Amiga; v.bits = 8;  v.rate = 16726.0; break;
        case CrunchStyle::Sp1200: v.model = SamplerModel::Amiga; v.bits = 12; v.rate = 26040.0; v.driveDb = 3.0f; break;
        case CrunchStyle::Mpc60:  v.model = SamplerModel::S950;  v.bits = 12; v.rate = 40000.0; break;
    }
    const double from = in.rate;
    const bool stereo = ! in.dataR.empty() && in.dataR.size() == in.data.size();
    const int n = (int) in.data.size();
    VintageAdc adc;
    const int block = 4096;
    adc.prepare (from, block);
    adc.set (v);
    adc.setDrive (v.driveDb);
    std::vector<float> l ((size_t) block), r ((size_t) block), monL ((size_t) block), monR ((size_t) block);
    std::vector<float> decL ((size_t) adc.maxOut (block)), decR (decL.size());
    std::vector<float> outL, outR;
    outL.reserve ((size_t) (n * v.rate / from) + 16); outR.reserve (outL.capacity());
    // (a few ms of silence after, to flush the filters, then trimmed back to the length the sample should be)
    const int tail = (int) (from * 0.01);
    for (int pos = 0; pos < n + tail; pos += block)
    {
        const int m = std::min (block, n + tail - pos);
        for (int i = 0; i < m; ++i)
        {
            const int k = pos + i;
            l[(size_t) i] = k < n ? in.data[(size_t) k] / 32768.0f : 0.0f;
            r[(size_t) i] = k < n ? (stereo ? in.dataR[(size_t) k] : in.data[(size_t) k]) / 32768.0f : 0.0f;
        }
        const int got = adc.process (l.data(), r.data(), m, monL.data(), monR.data(), decL.data(), decR.data());
        outL.insert (outL.end(), decL.begin(), decL.begin() + got);
        outR.insert (outR.end(), decR.begin(), decR.begin() + got);
    }
    const double ratio = v.rate / from;
    const size_t len = std::min (outL.size(), (size_t) std::max (1.0, std::round (n * ratio)));
    outL.resize (len); outR.resize (len);
    auto to16 = [] (float x) { return (int16_t) std::clamp ((int) std::lround (x * 32767.0f), -32768, 32767); };
    in.data.resize (len);
    for (size_t i = 0; i < len; ++i) in.data[i] = to16 (outL[i]);
    if (stereo) { in.dataR.resize (len); for (size_t i = 0; i < len; ++i) in.dataR[i] = to16 (outR[i]); }
    else in.dataR.clear();
    // the points move with the audio
    auto at = [&] (int p) { return (int) std::clamp ((long long) std::llround (p * ratio), 0LL, (long long) len); };
    for (auto& s : in.slices) s = at (s);
    in.slices.erase (std::unique (in.slices.begin(), in.slices.end()), in.slices.end());
    in.loopStart = at (in.loopStart);
    in.loopLength = in.loopLength >= 4 ? std::max (4, at (in.loopLength)) : in.loopLength;
    in.playStart = at (in.playStart);
    in.playEnd = in.playEnd > 0 ? at (in.playEnd) : 0;
    in.natRate = in.natRate > 0 ? in.natRate * ratio : v.rate;
    in.rate = v.rate;
    return true;
}
} // namespace prv
