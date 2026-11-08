#include "StretchSync.h"
#include "TimeStretch.h"
#include <algorithm>
#include <cmath>

namespace prv::stretch
{
const char* modeName (int m) { return m == 1 ? "CLASSIC" : m == 2 ? "AKAIZER" : "OFF"; }

bool wanted (const Instrument& in)
{
    return in.stretchMode > 0 && in.syncLines > 0 && (in.hasData() || ! in.stretchSrc.empty()) && ! in.isPlugin();
}

bool active (const Instrument& in) { return ! in.stretchSrc.empty() && in.stretchSrc.factor > 0; }

namespace
{
    int srcRangeStart (const Instrument::StretchSource& s) { return std::clamp (s.playStart, 0, std::max (0, (int) s.data.size() - 1)); }
    int srcRangeEnd (const Instrument::StretchSource& s) { const int n = (int) s.data.size(); return s.playEnd > srcRangeStart (s) && s.playEnd < n ? s.playEnd : n; }
}

double factorFor (const Instrument& in, double bpm, int speed)
{
    const auto& s = in.stretchSrc;
    const int len = s.empty() ? in.rangeEnd() - in.rangeStart() : srcRangeEnd (s) - srcRangeStart (s);
    if (len <= 0 || in.rate <= 0 || in.syncLines <= 0) return 1.0;
    const double lineSec = 2.5 / std::max (1.0, bpm) * std::max (1, speed);
    // BEAT SYNC plays C-3 at the instrument's rate: the stretch makes START..END last syncLines lines there
    const double natural = len / in.rate;
    return std::clamp (in.syncLines * lineSec / natural, 0.25, 20.0);
}

uint64_t fingerprint (const Instrument& in)
{
    uint64_t h = 1469598103934665603ull ^ (uint64_t) in.data.size() ^ ((uint64_t) in.dataR.size() << 32);
    // every sample (a few hundred thousand multiplies: well under a millisecond, a few times a second)
    for (auto v : in.data) h = (h ^ (uint16_t) v) * 1099511628211ull;
    for (auto v : in.dataR) h = (h ^ (uint16_t) v) * 1099511628211ull;
    return h;
}

bool edited (const Instrument& in) { return active (in) && fingerprint (in) != in.stretchSrc.mark; }

void begin (Instrument& in)
{
    auto& s = in.stretchSrc;
    s = {};
    s.data = in.data; s.dataR = in.dataR; s.slices = in.slices;
    s.playStart = in.playStart; s.playEnd = in.playEnd; s.loopStart = in.loopStart; s.loopLength = in.loopLength;
}

void restore (Instrument& in)
{
    auto& s = in.stretchSrc;
    if (s.empty()) return;
    in.data = std::move (s.data); in.dataR = std::move (s.dataR); in.slices = std::move (s.slices);
    in.playStart = s.playStart; in.playEnd = s.playEnd; in.loopStart = s.loopStart; in.loopLength = s.loopLength;
    s = {};
}

void bake (Instrument& in)
{
    in.stretchSrc = {};
    in.stretchMode = 0;
}

Instrument original (const Instrument& in)
{
    Instrument o = in;
    if (! in.stretchSrc.empty()) restore (o);
    return o;
}

bool needsRender (const Instrument& in, double bpm, int speed)
{
    if (! wanted (in) || in.stretchSrc.empty()) return false;
    const auto& s = in.stretchSrc;
    if (s.factor <= 0 || s.semis != in.stretchSemis || s.mode != in.stretchMode) return true;
    const double f = factorFor (in, bpm, speed);
    return std::abs (f - s.factor) > s.factor * 0.003;
}

std::optional<Render> make (const Instrument& in, double bpm, int speed)
{
    const auto& s = in.stretchSrc;
    if (s.empty()) return std::nullopt;
    Render r;
    r.factor = factorFor (in, bpm, speed);
    r.semis = std::clamp (in.stretchSemis, -24, 24);
    r.mode = in.stretchMode;
    StretchSettings st;
    st.mode = in.stretchMode == 2 ? StretchSettings::Mode::Akaizer : StretchSettings::Mode::Classic;
    st.timeFactor = r.factor;
    st.semitones = r.semis;
    st.sampleRate = in.rate > 0 ? in.rate : 44100.0;
    st.cycleLength = 1000;
    const bool stereo = ! s.dataR.empty();
    std::vector<std::vector<float>> ch (stereo ? 2 : 1);
    ch[0].resize (s.data.size());
    for (size_t i = 0; i < s.data.size(); ++i) ch[0][i] = s.data[i] / 32768.0f;
    if (stereo) { ch[1].resize (s.dataR.size()); for (size_t i = 0; i < s.dataR.size(); ++i) ch[1][i] = s.dataR[i] / 32768.0f; }
    auto out = cyclicStretch (ch, st);
    if (out.empty() || out[0].empty()) return std::nullopt;
    auto toI = [] (const std::vector<float>& v)
    {
        std::vector<int16_t> o (v.size());
        for (size_t i = 0; i < v.size(); ++i) o[i] = (int16_t) std::lround (std::clamp (v[i], -1.0f, 1.0f) * 32767.0f);
        return o;
    };
    r.data = toI (out[0]);
    if (stereo && out.size() > 1) r.dataR = toI (out[1]);
    // the points move with the audio
    const double k = (double) r.data.size() / std::max<size_t> (1, s.data.size());
    const int n = (int) r.data.size();
    auto at = [&] (int p) { return std::clamp ((int) std::lround (p * k), 0, n); };
    for (int p : s.slices) r.slices.push_back (std::min (at (p), std::max (0, n - 1)));
    r.slices.erase (std::unique (r.slices.begin(), r.slices.end()), r.slices.end());
    r.playStart = at (s.playStart);
    r.playEnd = s.playEnd > 0 ? at (s.playEnd) : 0;
    r.loopStart = at (s.loopStart);
    r.loopLength = s.loopLength >= 4 ? std::max (4, at (s.loopStart + s.loopLength) - r.loopStart) : s.loopLength;
    if (r.loopStart + r.loopLength > n) r.loopLength = std::max (0, n - r.loopStart);
    return r;
}

void apply (Instrument& in, Render&& r)
{
    in.data.swap (r.data); in.dataR.swap (r.dataR); in.slices.swap (r.slices);
    in.playStart = r.playStart; in.playEnd = r.playEnd; in.loopStart = r.loopStart; in.loopLength = r.loopLength;
    auto& s = in.stretchSrc;
    s.factor = r.factor; s.semis = r.semis; s.mode = r.mode;
    s.mark = fingerprint (in);
}
} // namespace prv::stretch
