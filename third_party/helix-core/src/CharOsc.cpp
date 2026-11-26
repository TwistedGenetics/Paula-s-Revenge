#include "helix/CharOsc.h"
#include "../third_party/charosc/hoover_dsp.h"
#include "../third_party/charosc/dreadbass.h"
#include "../third_party/charosc/rollers.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace helix {

const char* charKnobName(int type, int k)
{
    static const char* const hoover[6] = {"Detune", "PWM", "Drive", "Sub", "Dive", "Dive Time"};
    static const char* const dread[4][6] = {
        {"Sweep", "Reso", "Crunch", "Sub", "Pitch Env", "Sweep Time"},     // Dread
        {"Harmonic", "Reso", "Drive", "Sub", "Pitch Env", "Ring Time"},    // Donk
        {"FM Index", "Feedback", "Drive", "Sub", "Pitch Env", "Env Time"}, // Growl
        {"PWM", "Detune", "Drive", "Sub", "Pitch Env", "Env Time"}};       // Hoover
    static const char* const rollers[6] = {"Detune", "Blend", "Drive", "Crunch", "Drop", "Drop Time"};
    if (k < 0 || k > 5) return "";
    switch (charFamily(type))
    {
        case 0: return hoover[k];
        case 1: return dread[type - kCharDread][k];
        case 2: return rollers[k];
        default: return "-";
    }
}

int charFamily(int t)
{
    if (t >= kCharHoover && t <= kCharRave) return 0;
    if (t >= kCharDread && t <= kCharDreadHoover) return 1;
    if (t >= kCharRollSub && t <= kCharRollHoover) return 2;
    return -1;
}

struct CharOsc::Impl
{
    hoover::Engine hv;
    dread::Engine<float> db;
    Rollers rl;
    int dbCache[dread::Params::kNumParams];
    int rlCache[Rollers::kNumParams];
    Impl()
    {
        db.init();
        rl.init(0x2468ACEu);
        for (int& c : dbCache) c = -99999;
        for (int& c : rlCache) c = -99999;
    }
    void dbSet(int id, int v) { if (dbCache[id] != v) { dbCache[id] = v; db.setParam(id, v); } }
    void rlSet(int id, int v) { if (rlCache[id] != v) { rlCache[id] = v; rl.setParam(id, v); } }
};

CharOsc::CharOsc() : d_(std::make_unique<Impl>()) {}
CharOsc::~CharOsc() = default;

// The ported engines read their sample rate from per-thread globals: set them to this voice's rate before
// rendering (an offline print at another rate on another thread never disturbs the live voices).
static void useSampleRate(float sr)
{
    if (hoover::kSampleRate == sr) return;
    hoover::kSampleRate = sr;
    dread::setSampleRate(sr);
    rmath::setSampleRate(sr);
}

void CharOsc::start(bool fresh)
{
    pendingStart_ = true;
    pendingFresh_ = pendingFresh_ || fresh;
}

void CharOsc::render(int type, float note, const CharKnobs& k, float* out, int n)
{
    const int fam = charFamily(type);
    if (fam < 0) { std::memset(out, 0, sizeof(float) * (size_t)n); lastType_ = type; pendingStart_ = false; return; }
    Impl& d = *d_;
    useSampleRate(sr_);
    const bool switched = charFamily(lastType_) != fam;
    const bool start = pendingStart_ || switched;
    const bool fresh = pendingFresh_ || switched;
    pendingStart_ = pendingFresh_ = false;
    lastType_ = type;
    // Output trims: each engine's level brought to about a HELIX oscillator's (RMS ~0.2 on a held C2, like Osc A's default saw)
    switch (fam)
    {
        case 0:
        {
            hoover::Params& p = d.hv.params;
            p.mode = type - kCharHoover;
            p.detune = k.timbre; p.pwm = k.character; p.drive = k.drive; p.sub = k.sub;
            p.dive = k.env; p.diveTime = k.time; p.chorus = 0.5f; p.drift = 0.3f; p.transpose = 0.0f;
            if (start) d.hv.trigger(0, fresh);
            d.hv.render(0, note, out, n);
            for (int i = 0; i < n; ++i) out[i] *= 2.8f;
            break;
        }
        case 1:
        {
            using P = dread::Params;
            d.dbSet(P::kMode, type - kCharDread);
            d.dbSet(P::kTimbre, (int)std::lround(k.timbre * 1023.0f));
            d.dbSet(P::kChar, (int)std::lround(k.character * 100.0f));
            d.dbSet(P::kDrive, (int)std::lround(k.drive * 100.0f));
            d.dbSet(P::kSub, (int)std::lround(k.sub * 100.0f));
            d.dbSet(P::kSubOct, 0);
            d.dbSet(P::kKnock, 0);
            d.dbSet(P::kPluck, 0);
            d.dbSet(P::kEgInt, (int)std::lround(k.env < 0.0f ? k.env * 24.0f : k.env * 48.0f));
            d.dbSet(P::kEgTime, 1 + (int)std::lround(k.time * 999.0f));
            d.dbSet(P::kRetrig, 1);
            d.db.setNote(0, note);
            if (start) d.db.noteOn(0, fresh, false);
            for (int i = 0; i < n; i += dread::kMaxBlock)
            {
                const int m = std::min((int)dread::kMaxBlock, n - i);
                d.db.render(out + i, m);
            }
            for (int i = 0; i < n; ++i) out[i] *= 1.7f;
            break;
        }
        default:
        {
            d.rlSet(Rollers::kMode, type - kCharRollSub);
            d.rlSet(Rollers::kDetune, (int)std::lround(k.timbre * 1023.0f));
            d.rlSet(Rollers::kBlend, (int)std::lround(k.character * 1023.0f));
            d.rlSet(Rollers::kDrive, (int)std::lround(k.drive * 100.0f));
            d.rlSet(Rollers::kCrunch, (int)std::lround(k.sub * 100.0f));
            d.rlSet(Rollers::kDrop, (int)std::lround(std::fabs(k.env) * 100.0f));
            d.rlSet(Rollers::kDropTime, (int)std::lround(k.time * 100.0f));
            d.rlSet(Rollers::kVoices, 3);
            d.rlSet(Rollers::kDrift, 30);
            d.rlSet(Rollers::kSubShape, 0);
            d.rl.setPitch((float)(440.0 * std::pow(2.0, (note - 69.0) / 12.0)) * rmath::kInvSR);
            if (start) d.rl.noteOn();
            d.rl.process(out, (uint32_t)n, 1, 1.35f);
            break;
        }
    }
}

} // namespace helix
