// Paula's Revenge - CV out
#include "CvOut.h"

#include <algorithm>
#include <cmath>

namespace prv
{
double cvVoltsFor (const std::vector<float>& n, const std::vector<float>& v, double midi)
{
    if (n.size() < 2 || n.size() != v.size()) return (midi - 60.0) / 12.0;
    // the segment (the first or last one carries on past the ends)
    size_t k = 1;
    while (k + 1 < n.size() && midi > n[k]) ++k;
    const double n0 = n[k - 1], n1 = n[k], v0 = v[k - 1], v1 = v[k];
    if (std::abs (n1 - n0) < 1e-9) return v0;
    return v0 + (v1 - v0) * (midi - n0) / (n1 - n0);
}

double cvVoltsFor (const Instrument& in, double midi)
{
    return cvVoltsFor (in.cvMapNotes, in.cvMapVolts, midi + 12.0 * std::clamp (in.cvOctave, -4, 4));
}

bool solveCvMap (const std::vector<double>& volts, const std::vector<double>& hz, std::vector<float>& notes, std::vector<float>& mapVolts)
{
    struct P { double v, n; };
    std::vector<P> pts;
    for (size_t i = 0; i < volts.size() && i < hz.size(); ++i)
        if (hz[i] > 1.0) pts.push_back ({ volts[i], 69.0 + 12.0 * std::log2 (hz[i] / 440.0) });
    std::sort (pts.begin(), pts.end(), [] (const P& a, const P& b) { return a.v < b.v; });
    // a pitch that doesn't rise with the voltage is a bad reading (an octave error, noise): dropped
    std::vector<P> good;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const bool risesFromLast = good.empty() || pts[i].n > good.back().n + 0.05;
        const bool notTooFar = good.empty() || pts[i].n - good.back().n < 36.0 * (pts[i].v - good.back().v) + 3.0;
        const bool nextAgrees = i + 1 >= pts.size() || pts[i + 1].n > pts[i].n;
        if (risesFromLast && notTooFar && (nextAgrees || i + 1 >= pts.size())) good.push_back (pts[i]);
    }
    notes.clear(); mapVolts.clear();
    if (good.size() < 3) return false;
    for (const auto& p : good) { notes.push_back ((float) p.n); mapVolts.push_back ((float) p.v); }
    return true;
}

// ------------------------------------------------------------------ the CV OUT instrument
void CvOutSynth::update (const Instrument& in)
{
    cfg.cvPitchOut = in.cvPitchOut; cfg.cvGateOut = in.cvGateOut; cfg.cvVelOut = in.cvVelOut; cfg.cvModOut = in.cvModOut;
    cfg.cvFullScale = in.cvFullScale; cfg.cvGateVolts = in.cvGateVolts; cfg.cvOctave = in.cvOctave;
    cfg.bendRange = in.bendRange;
    if (cfg.cvMapNotes != in.cvMapNotes) cfg.cvMapNotes = in.cvMapNotes;   // (only when it changes: no allocating each block)
    if (cfg.cvMapVolts != in.cvMapVolts) cfg.cvMapVolts = in.cvMapVolts;
}

void CvOutSynth::noteOn (int n, int vel, int offset)    { push (On, offset, n, vel); }
void CvOutSynth::noteOff (int n, int offset)            { push (Off, offset, n); }
void CvOutSynth::allNotesOff (int offset)               { push (AllOff, offset); }
void CvOutSynth::pitchBend (int v, int offset)          { push (Bend, offset, v); }
void CvOutSynth::setParameterAt (int knob, float v, int offset) { if (knob == 0) push (Mod, offset, (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 100000.0f)); }

void CvOutSynth::render (float* left, float* right, int n)
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
}

void CvOutSynth::apply (const Ev& e)
{
    switch (e.type)
    {
        case On:
        {
            for (int k = 0; k < numHeld; ++k) if (held[k] == e.a) { std::copy (held + k + 1, held + numHeld, held + k); --numHeld; break; }
            if (numHeld < 32) held[numHeld++] = e.a;
            note = e.a; velocity = e.b;
            if (gate) retrigger = std::max (1, (int) (rate * 0.002));   // (2 ms low: envelopes start again)
            gate = true;
            break;
        }
        case Off:
        {
            for (int k = 0; k < numHeld; ++k) if (held[k] == e.a) { std::copy (held + k + 1, held + numHeld, held + k); --numHeld; break; }
            if (numHeld > 0) note = held[numHeld - 1];   // (back to a key still down, legato)
            else gate = false;
            break;
        }
        case AllOff: numHeld = 0; gate = false; break;
        case Bend: bend = (e.a - 8192) / 8192.0 * std::max (1, cfg.bendRange); break;
        case Mod: mod = (float) (e.a / 100000.0); break;
    }
}

void CvOutSynth::renderCv (CvBus& bus, int n)
{
    // the events in time order
    std::stable_sort (events, events + numEvents, [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });
    int ev = 0;
    for (int i = 0; i < n; ++i)
    {
        while (ev < numEvents && events[ev].offset <= i) apply (events[ev++]);
        const double v = testing ? testVolts : cvVoltsFor (cfg, note + bend);
        lastVolts = v;
        const bool g = testing || (gate && retrigger == 0);
        if (retrigger > 0) --retrigger;
        bus.put (cfg.cvPitchOut, i, cvSample (v, cfg.cvFullScale));
        bus.put (cfg.cvGateOut, i, g ? cvSample (cfg.cvGateVolts, cfg.cvFullScale) : 0.0f);
        bus.put (cfg.cvVelOut, i, cvSample (cfg.cvGateVolts * velocity / 127.0, cfg.cvFullScale));
        bus.put (cfg.cvModOut, i, cvSample (cfg.cvGateVolts * mod, cfg.cvFullScale));
    }
    while (ev < numEvents) apply (events[ev++]);
    numEvents = 0;
}

// ------------------------------------------------------------------ CV SOURCES
void CvSourceRunner::prepare (double sr) { rate = sr > 0 ? sr : 48000.0; reset(); }
void CvSourceRunner::reset() { for (auto& s : st) s = State {}; }

void CvSourceRunner::process (const Song& song, CvBus& bus, int i, double ppq, double bps, bool playing, bool started,
                              const bool* trig, const bool* rel, const float* knobA, const float* knobB)
{
    const int pulseLen = std::max (1, (int) (rate * 0.005));   // 5 ms triggers
    for (int k = 0; k < kCvSources; ++k)
    {
        const auto& c = song.cvSources[k];
        auto& s = st[k];
        if (c.kind == CvKind::Off || c.out < 1) continue;
        double v = 0;
        switch (c.kind)
        {
            case CvKind::Clock:
            {
                // a pulse each 1/ppqn of a beat while playing
                const int q = std::clamp (c.ppqn, 1, 96);
                const double tick = std::floor (ppq * q + 1e-9);
                if (! playing) s.lastTick = -1;
                else if (started || tick != s.lastTick) { if (s.lastTick >= 0 || started) s.pulse = pulseLen; s.lastTick = tick; }
                v = s.pulse > 0 ? c.volts : 0.0;
                if (s.pulse > 0) --s.pulse;
                break;
            }
            case CvKind::Reset:
                if (started) s.pulse = pulseLen;
                v = s.pulse > 0 ? c.volts : 0.0;
                if (s.pulse > 0) --s.pulse;
                break;
            case CvKind::Run:
                v = playing ? c.volts : 0.0;
                break;
            case CvKind::Lfo:
            {
                // in step with the song while it plays; on at the tempo while stopped
                const double cyc = std::clamp ((double) c.beats, 0.0625, 256.0);
                const double before = s.phase;
                if (playing) s.phase = std::fmod (ppq / cyc, 1.0);
                else s.phase = std::fmod (s.phase + bps / cyc, 1.0);
                if (s.phase < 0) s.phase += 1.0;
                if (s.phase < before || s.lastTick < 0) { s.seed = s.seed * 1664525u + 1013904223u; s.rnd = (float) ((s.seed >> 8) / 8388608.0 - 1.0); s.lastTick = 0; }   // (RANDOM: a new level each cycle)
                const double p = s.phase;
                double w = 0;
                switch (c.shape)
                {
                    case 0: w = std::sin (2.0 * 3.14159265358979323846 * p); break;
                    case 1: w = p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; break;
                    case 2: w = 2.0 * p - 1.0; break;
                    case 3: w = p < 0.5 ? 1.0 : -1.0; break;
                    default: w = s.rnd; break;
                }
                v = c.low + c.volts * w;
                break;
            }
            case CvKind::Env:
            {
                const int t = std::clamp (c.track, 0, kMaxTracks - 1);
                if (trig != nullptr && trig[t]) s.env = 1;
                else if (rel != nullptr && rel[t] && s.env != 0) s.env = 4;
                const double a = std::max (0.0005, (double) c.attack) * rate, d = std::max (0.0005, (double) c.decay) * rate, r = std::max (0.0005, (double) c.release) * rate;
                switch (s.env)
                {
                    case 1: s.envLevel += (float) (1.0 / a); if (s.envLevel >= 1.0f) { s.envLevel = 1.0f; s.env = 2; } break;
                    case 2: s.envLevel -= (float) ((1.0 - c.sustain) / d); if (s.envLevel <= c.sustain) { s.envLevel = c.sustain; s.env = 3; } break;
                    case 3: break;
                    case 4: s.envLevel -= (float) (1.0 / r); if (s.envLevel <= 0) { s.envLevel = 0; s.env = 0; } break;
                    default: break;
                }
                v = c.volts * s.envLevel;
                break;
            }
            case CvKind::Knob:
            {
                const int t = std::clamp (c.track, 0, kMaxTracks - 1);
                const float* k2 = c.knob == 0 ? knobA : knobB;
                const float x = k2 != nullptr && k2[t] >= 0 ? k2[t] : 0.0f;
                v = c.low + (c.volts - c.low) * x;
                break;
            }
            default: break;
        }
        bus.put (c.out, i, cvSample (v, song.cvFullScale));
    }
}
} // namespace prv
