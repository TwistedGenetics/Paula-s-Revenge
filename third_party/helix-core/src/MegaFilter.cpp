// The Mega Synth filter models (see MegaFilter.h). Ported from TwistedGenetics/Mega-Synth
// Source/Filter/Filter.cpp; the DSP is the same, only the plumbing is HELIX's.
#include "helix/MegaFilter.h"

namespace helix {
namespace mega {

int nativeSlope(int model)
{
    static const int s[kNumModels] = {1, 3, 3, 2, 2, 3, 1, 3, 3, 1, 1, 2, 3, 3, 1, 2, 3};
    return model >= 0 && model < kNumModels ? s[model] : 1;
}

static int slopeMask(int type)
{
    switch (type)
    {
        case FT_BP: case FT_NOTCH: return 0b1010;
        case FT_PEAK: return 0;
        default: return 0b1111;
    }
}

int effectiveSlope(int type, int slope)
{
    const int mask = slopeMask(type);
    if (mask == 0) return 1;
    if (mask & (1 << slope)) return slope;
    return slope <= 1 ? 1 : 3;
}

// Clean up to t, then a smooth knee that levels off at 1.5 t.
static inline float softKnee(float x, float t)
{
    const float a = std::abs(x);
    if (a <= t) return x;
    const float h = 0.5f * t;
    const float y = t + h * fastTanh((a - t) / h);
    return x < 0.0f ? -y : y;
}

// Drive into the filter: clean at 0 dB, saturation blended in by +12 dB (Warmth: softer, asymmetric).
static inline float inputShape(float x, float k, float warm)
{
    const float clean = softKnee(k * x, 1.0f);
    if (k <= 1.0f) return clean;
    float sat;
    if (warm <= 0.0f) sat = driveShape(x, k);
    else
    {
        const float c = clampv(x, -1.0f, 1.0f);
        const float soft = std::abs(x) < 1.5f ? x - (4.0f / 27.0f) * x * x * x : (x > 0 ? 1.0f : -1.0f);
        float xi = c + (soft - c) * warm;
        xi += 0.12f * warm * xi * xi;
        sat = fastTanh(k * xi);
    }
    const float a = std::min(1.0f, 0.5f * std::log2(k));
    return clean + (sat - clean) * a;
}

static inline float driveCharacter(float k) { return k <= 1.0f ? 0.0f : std::min(1.0f, 0.5f * std::log2(k)); }

static inline float modelShape(float v, float k, float amount)
{
    const float g = k >= 7.0f ? 2.8f : (k >= 6.0f ? 1.7f : (k >= 4.0f ? 1.9f : 1.0f));
    if (amount <= 0.0f) return v * g;
    const float s = driveShape(v, k);
    return amount >= 1.0f ? s : v * g + (s - v * g) * amount;
}

static inline float driveCompensation(float k) { return k <= 1.0f ? 1.0f : 1.0f / std::sqrt(1.0f + 0.2f * (k - 1.0f)); }

//==============================================================================
void Biquad::set(Type type, double freq, double q, double sampleRate)
{
    auto pass = [this] { b0 = 1; b1 = b2 = a1 = a2 = 0; };
    auto zero = [this] { b0 = b1 = b2 = a1 = a2 = 0; };
    const double nyq = sampleRate * 0.5;
    const double fn = clampv(freq / nyq, 0.0, 1.0);
    if (type == LP || type == HP)
    {
        if (fn >= 1.0) { if (type == LP) pass(); else zero(); return; }
        if (fn <= 0.0) { if (type == LP) zero(); else pass(); return; }
        const double w0 = kPi * fn;
        const double g = std::pow(10.0, q / 20.0);
        const double alpha = std::sin(w0) / (2.0 * g);
        const double c = std::cos(w0);
        const double ia0 = 1.0 / (1.0 + alpha);
        if (type == LP) { b0 = (1 - c) * 0.5 * ia0; b1 = (1 - c) * ia0; b2 = b0; }
        else            { b0 = (1 + c) * 0.5 * ia0; b1 = -(1 + c) * ia0; b2 = b0; }
        a1 = -2.0 * c * ia0; a2 = (1.0 - alpha) * ia0;
        return;
    }
    if (fn <= 0.0 || fn >= 1.0) { if (type == NOTCH) pass(); else zero(); return; }
    if (q <= 0.0) { if (type == NOTCH) zero(); else pass(); return; }
    const double w0 = kPi * fn;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double c = std::cos(w0);
    const double ia0 = 1.0 / (1.0 + alpha);
    if (type == BP) { b0 = alpha * ia0; b1 = 0; b2 = -alpha * ia0; }
    else            { b0 = ia0; b1 = -2.0 * c * ia0; b2 = ia0; }
    a1 = -2.0 * c * ia0; a2 = (1.0 - alpha) * ia0;
}

//==============================================================================
enum FilterMode { FM_LP, FM_SALLENKEY, FM_LADDER, FM_STEINER, FM_SVF, FM_OTA, FM_POLIVOKS, FM_SWCAP, FM_ACID,
                  FM_MS20, FM_OBERHEIM, FM_SEM, FM_TB303, FM_MOOGFAT, FM_ARPODYSSEY, FM_CS15, FM_SH2 };

void FilterChain::configure(int newMode)
{
    mode = newMode;
    fbKind = FbNone; preK = 0; postK = 0; fbGain = 0; ladder = false; limitOut = false; tb303 = false;
    auto set = [this](std::initializer_list<Biquad::Type> t)
    {
        numStages = 0;
        for (auto x : t) types[numStages++] = x;
    };
    using B = Biquad;
    switch (mode)
    {
        case FM_LP:          set({B::LP}); break;
        case FM_SALLENKEY:   set({B::LP, B::LP}); break;
        case FM_LADDER:      set({}); ladder = true; break;
        case FM_STEINER:     set({B::HP, B::BP, B::LP}); break;
        case FM_SVF:         set({B::BP, B::LP}); break;
        case FM_OTA:         set({B::LP, B::LP}); preK = 4.0f; break;
        case FM_POLIVOKS:    set({B::HP, B::LP}); postK = 7.0f; break;
        case FM_SWCAP:       set({B::LP, B::NOTCH, B::LP}); break;
        case FM_MS20:        set({B::HP, B::LP}); break;
        case FM_OBERHEIM:    set({B::HP, B::LP}); break;
        case FM_SEM:         set({B::BP, B::LP}); break;
        case FM_TB303:       set({}); ladder = true; tb303 = true; limitOut = true; break;
        case FM_MOOGFAT:     set({}); ladder = true; break;
        case FM_ARPODYSSEY:  set({B::HP, B::LP}); break;
        case FM_CS15:        set({B::HP, B::BP, B::LP}); break;
        case FM_SH2:         set({B::LP, B::LP}); break;
        case FM_ACID:
        default:             set({B::LP, B::LP}); preK = 6.0f; limitOut = true; break;
    }
    reset();
}

void FilterChain::reset()
{
    for (auto& s : st) s.reset();
    fbState[0] = fbState[1] = 0;
    for (auto& ch : ls) for (auto& v : ch) v = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    tbX[0] = tbX[1] = tbY[0] = tbY[1] = 0.0f;
}

void FilterChain::setLadder(float c, float spread, float k, double sr)
{
    for (int i = 0; i < 4; ++i)
    {
        const double fc = clampv((double)std::max(20.0f, c * (1.0f - i * spread)), 10.0, sr * 0.45);
        const double g = std::tan(kPi * fc / sr);
        lg[i] = (float)(g / (1.0 + g));
    }
    lk = k;
}

void FilterChain::update(float c, float res, double sr)
{
    auto mx = [](float a, float b) { return std::max(a, b); };
    auto mn = [](float a, float b) { return std::min(a, b); };
    dcR = (float)(1.0 - 2.0 * kPi * 8.0 / sr);
    auto S = [&](int k, float f, float q) { st[k].set(types[k], f, q, sr); };
    switch (mode)
    {
        case FM_LP:
            S(0, c, res); break;
        case FM_SALLENKEY:
            S(0, c, mx(0.2f, res * 0.18f));
            S(1, mx(20, c * 0.92f), mx(0.2f, res * 0.18f)); break;
        case FM_LADDER:
            setLadder(c, 0.0f, 4.0f * mn(0.98f, res / 30.0f), sr); break;
        case FM_STEINER:
            S(0, mx(20, c * 0.55f), mx(0.2f, res * 0.12f));
            S(1, c, mx(0.2f, res * 0.35f));
            S(2, mn(18000, c * 1.15f), mx(0.2f, res * 0.18f)); break;
        case FM_SVF:
            S(0, c, mx(0.2f, res * 0.28f));
            S(1, mn(18000, c * 1.08f), mx(0.2f, res * 0.14f)); break;
        case FM_OTA:
            S(0, c, mx(0.2f, res * 0.16f));
            S(1, mx(20, c * 0.88f), mx(0.2f, res * 0.22f)); break;
        case FM_POLIVOKS:
            S(0, mx(20, c * 0.65f), mx(0.2f, res * 0.2f));
            S(1, c, mx(0.2f, res * 0.55f)); break;
        case FM_SWCAP:
            S(0, c, mx(0.2f, res * 0.12f));
            S(1, mx(20, c * 1.35f), mx(0.2f, res * 0.35f));
            S(2, mx(20, c * 0.82f), mx(0.2f, res * 0.12f)); break;
        case FM_MS20:
            S(0, mx(20, c * 0.62f), mx(0.25f, res * 0.18f));
            S(1, c, mx(0.3f, res * 0.72f)); break;
        case FM_OBERHEIM:
            S(0, mx(20, c * 0.28f), mx(0.2f, res * 0.08f));
            S(1, mn(18000, c * 1.08f), mx(0.2f, res * 0.2f)); break;
        case FM_SEM:
            S(0, c, mx(0.2f, res * 0.18f));
            S(1, mn(18000, c * 1.12f), mx(0.2f, res * 0.14f)); break;
        case FM_TB303:
            tbA = (float)(1.0 / (1.0 + 2.0 * kPi * 150.0 / sr));
            setLadder(c, 0.0f, 4.0f * mn(0.98f, res / 24.0f), sr); break;
        case FM_MOOGFAT:
            setLadder(c, 0.03f, 4.0f * mn(0.985f, res / 22.0f), sr); break;
        case FM_ARPODYSSEY:
            S(0, mx(20, c * 0.42f), mx(0.2f, res * 0.12f));
            S(1, mn(18000, c * 1.04f), mx(0.2f, res * 0.24f)); break;
        case FM_CS15:
            S(0, mx(20, c * 0.5f), mx(0.2f, res * 0.1f));
            S(1, c, mx(0.2f, res * 0.42f));
            S(2, mn(18000, c * 1.1f), mx(0.2f, res * 0.16f)); break;
        case FM_SH2:
            S(0, c, mx(0.2f, res * 0.16f));
            S(1, mx(20, c * 0.9f), mx(0.2f, res * 0.36f)); break;
        case FM_ACID:
        default:
        {
            const float rs = clampv((c - 60.0f) / 440.0f, 0.2f, 1.0f);
            S(0, c, res * 0.8f * rs);
            S(1, mx(20, c * 0.95f), res * 1.2f * rs);
            fbGain = mn(0.92f, res / 28.0f); break;
        }
    }
}

float FilterChain::process(float x, int ch, float drive)
{
    float v;
    if (ladder)
    {
        float fb = fbState[ch];
        if (tb303)
        {
            const float y = tbA * (tbY[ch] + fb - tbX[ch]);
            tbX[ch] = fb; tbY[ch] = y;
            fb = y;
        }
        v = inputShape(x - lk * fb, drive, warm);
        float* s = ls[ch];
        for (int k = 0; k < 4; ++k)
        {
            const float u = (v - s[k]) * lg[k];
            v = u + s[k];
            s[k] = v + u;
        }
        if (!(std::abs(v) < 64.0f)) { reset(); v = 0.0f; }
        fbState[ch] = v;
        v *= tb303 ? (1.0f + lk * 0.45f) : (1.0f + lk * 0.35f);
    }
    else
    {
        v = inputShape(x, drive, warm);
        if (preK > 0.0f)
        {
            if (fbKind == FbToPre) v += fbGain * fbState[ch];
            v = modelShape(v, preK, character);
        }
        for (int k = 0; k < numStages; ++k) v = st[k].process(v, ch);
        if (!(std::abs(v) < 64.0f)) { reset(); v = 0.0f; }
        fbState[ch] = v;
        if (postK > 0.0f) v = modelShape(v, postK, character);
    }
    // acid / 303: only the extreme peaks; the rest: a safety knee as in the multimode core
    // (at full Resonance the Standard LP peaks ~+25 dB, which HELIX's voice bus would clip hard)
    v = softKnee(v, limitOut ? 1.5f : 4.0f);
    const float y = v - dcX[ch] + dcR * dcY[ch];
    dcX[ch] = v; dcY[ch] = y;
    return y;
}

//==============================================================================
static const ModelVoice& modelVoice(int model)
{
    //                                                preK postK char resScale satT  limit
    static const ModelVoice table[kNumModels] = { {0, 0, 0, 1.00f, 6.0f, false},    // Standard LP
                                                  {0, 0, 0, 0.90f, 5.0f, false},    // Sallen-Key
                                                  {0, 0, 0, 1.00f, 5.0f, false},    // Ladder
                                                  {0, 0, 0, 1.10f, 3.0f, false},    // Steiner-Parker
                                                  {0, 0, 0, 1.00f, 6.0f, false},    // State Variable
                                                  {4, 0, 0, 0.95f, 4.0f, false},    // Discrete OTA
                                                  {0, 7, 0, 1.15f, 2.4f, false},    // Polivoks
                                                  {0, 0, 0, 0.90f, 4.0f, false},    // Switched Capacitor
                                                  {6, 0, 0, 1.10f, 3.0f, true },    // Acid Filter Approx
                                                  {0, 0, 0, 1.15f, 2.4f, false},    // MS-20
                                                  {0, 0, 0, 0.90f, 6.0f, false},    // Oberheim
                                                  {0, 0, 0, 0.85f, 6.0f, false},    // SEM
                                                  {0, 0, 0, 1.00f, 3.6f, true },    // TB-303
                                                  {0, 0, 0, 1.00f, 5.0f, false},    // Minimoog Fat Cat
                                                  {0, 0, 0, 1.00f, 4.0f, false},    // Arp Odyssey
                                                  {0, 0, 0, 0.95f, 5.0f, false},    // CS-15
                                                  {0, 0, 0, 0.95f, 5.0f, false} };  // SH-2
    return table[clampv(model, 0, kNumModels - 1)];
}

void Svf::tick(float v0, int ch, float& lp, float& bp, float sat)
{
    const float v3 = v0 - ic2[ch];
    const float v1 = a1 * ic1[ch] + a2 * v3;
    const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
    ic1[ch] = 2.0f * v1 - ic1[ch];
    ic2[ch] = 2.0f * v2 - ic2[ch];
    if (sat > 0.0f) ic1[ch] = softKnee(ic1[ch], sat);
    lp = v2; bp = v1;
}

void MultiCore::configure(int filterType, int effSlope, int model)
{
    type = clampv(filterType, 0, (int)FT_TYPE_COUNT - 1);
    slope = clampv(effSlope, 0, 3);
    mv = modelVoice(model);
    reset();
}

void MultiCore::reset()
{
    a.reset(); b.reset(); p.reset();
    for (auto& ch : ls) for (auto& v : ch) v = 0.0f;
    lfb[0] = lfb[1] = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
}

void MultiCore::update(float c, float res, double sr)
{
    dcR = (float)(1.0 - 2.0 * kPi * 8.0 / sr);
    const float g = (float)std::tan(kPi * (double)c / sr);
    r = clampv((res - 0.1f) / 24.9f * mv.resScale, 0.0f, 1.0f);
    const float q = 0.55f * std::pow(90.0f, r);
    const float k = 1.0f / q;
    bump = 0.0f;
    switch (type)
    {
        case FT_LP:
        case FT_HP:
            if (slope == 3 && type == FT_LP)
            {
                lg = g / (1.0f + g);
                lk = 4.2f * std::pow(r, 0.75f);
            }
            else if (slope == 3)
            {
                a.set(g, 1.0f / 0.5412f);
                b.set(g, std::min(k, 1.0f / 1.3066f));
            }
            else
            {
                a.set(g, k);
                p.set(g);
                if (slope == 0) bump = 2.0f * r;
            }
            break;
        case FT_BP:
            a.set(g, slope == 3 ? std::min(k, 1.0f / 0.7071f) : k);
            b.set(g, k);
            bpGainA = slope == 3 ? a.k : std::sqrt(a.k);
            bpGainB = std::sqrt(b.k);
            break;
        case FT_NOTCH:
            a.set(g, k); b.set(g, k);
            break;
        case FT_PEAK:
        {
            const float db = 2.0f + 16.0f * r;
            const float A = std::pow(10.0f, db / 40.0f);
            const float qb = 0.7f + 4.0f * r;
            a.set(g, 1.0f / (qb * A));
            peakGain = A * A - 1.0f;
            break;
        }
        case FT_AP:
        default:
            a.set(g, k); b.set(g, k); p.set(g);
            break;
    }
}

float MultiCore::process(float x, int ch, float drive)
{
    float y = 0.0f, lp, bp;
    const float sat = mv.satT;
    if (type == FT_LP && slope == 3)
    {
        float* s = ls[ch];
        const float G = lg, H = 1.0f - lg;
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const float S = G3 * H * s[0] + G2 * H * s[1] + G * H * s[2] + H * s[3];
        const float y4 = (G4 * x + S) / (1.0f + lk * G4);
        float v = inputShape(x - lk * y4, drive, warm);
        if (mv.preK > 0.0f) v = modelShape(v, mv.preK, mv.character);
        for (int k = 0; k < 4; ++k)
        {
            const float u = (v - s[k]) * lg;
            v = u + s[k];
            s[k] = v + u;
        }
        lfb[ch] = v;
        y = v * (1.0f + lk * 0.35f);
    }
    else
    {
        float v = inputShape(x, drive, warm);
        if (mv.preK > 0.0f) v = modelShape(v, mv.preK, mv.character);
        switch (type)
        {
            case FT_LP:
            case FT_HP:
            {
                const bool hp = type == FT_HP;
                if (slope == 0)
                {
                    const float l1 = p.lp(v, ch);
                    y = hp ? v - l1 : l1;
                    if (bump > 0.0f) { a.tick(v, ch, lp, bp, sat); y += bump * a.k * bp; }
                }
                else if (slope == 3)
                {
                    a.tick(v, ch, lp, bp, 0.0f);
                    const float h1 = v - a.k * bp - lp;
                    b.tick(h1, ch, lp, bp, sat);
                    y = h1 - b.k * bp - lp;
                }
                else
                {
                    a.tick(v, ch, lp, bp, sat);
                    y = hp ? v - a.k * bp - lp : lp;
                    if (slope == 2) { const float l1 = p.lp(y, ch); y = hp ? y - l1 : l1; }
                }
                break;
            }
            case FT_BP:
                a.tick(v, ch, lp, bp, sat);
                y = bp * bpGainA;
                if (slope == 3) { b.tick(y, ch, lp, bp, sat); y = bp * bpGainB; }
                break;
            case FT_NOTCH:
                a.tick(v, ch, lp, bp, sat);
                y = v - a.k * bp;
                if (slope == 3) { const float y1 = y; b.tick(y1, ch, lp, bp, sat); y = y1 - b.k * bp; }
                break;
            case FT_PEAK:
                a.tick(v, ch, lp, bp, sat);
                y = v + a.k * peakGain * bp;
                break;
            case FT_AP:
            default:
                if (slope == 0) { const float l1 = p.lp(v, ch); y = 2.0f * l1 - v; }
                else
                {
                    a.tick(v, ch, lp, bp, 0.0f);
                    y = v - 2.0f * a.k * bp;
                    if (slope == 2) { const float l1 = p.lp(y, ch); y = 2.0f * l1 - y; }
                    else if (slope == 3) { const float y1 = y; b.tick(y1, ch, lp, bp, 0.0f); y = y1 - 2.0f * b.k * bp; }
                }
                break;
        }
    }
    if (!(std::abs(y) < 64.0f)) { reset(); y = 0.0f; }
    if (mv.postK > 0.0f) y = modelShape(y, mv.postK, mv.character);
    y = softKnee(y, mv.limitOut ? 1.5f : 4.0f);
    const float d = y - dcX[ch] + dcR * dcY[ch];
    dcX[ch] = y; dcY[ch] = d;
    return d;
}

//==============================================================================
void MegaFilter::setup(Core& c, int model, int type, int slope)
{
    c.model = model; c.type = type; c.slope = slope;
    const int s = slope < 0 ? nativeSlope(model) : slope;
    const int eff = effectiveSlope(type, s);
    c.classic = type == FT_LP && eff == nativeSlope(model);
    if (c.classic) c.chain.configure(model);
    else c.multi.configure(type, eff, model);
    c.chain.warm = c.multi.warm = warm_;
    c.chain.character = c.multi.mv.character = driveCharacter(drive_);
}

void MegaFilter::configure(int model, int type, int slope)
{
    model = clampv(model, 0, kNumModels - 1);
    type = clampv(type, 0, (int)FT_TYPE_COUNT - 1);
    slope = clampv(slope, -1, 3);
    if (model == cur_.model && type == cur_.type && slope == cur_.slope) return;
    if (cur_.model < 0) { setup(cur_, model, type, slope); fade_ = 0; return; }
    old_ = cur_;
    setup(cur_, model, type, slope);
    fade_ = kFadeLen;
}

void MegaFilter::update(float cutoffHz, float res, float drive, float warmth, double sr)
{
    // HELIX knobs (0..1) onto the Mega Synth ranges: Resonance 0.1..25, Drive 1..25 (x^2, so the
    // first half of the knob is the useful warm range).
    const float c = clampv(cutoffHz, 20.0f, (float)std::min(20000.0, sr * 0.45));
    const float rq = 0.1f + 24.9f * clampv(res, 0.0f, 1.0f);
    const float d = clampv(drive, 0.0f, 1.0f);
    drive_ = 1.0f + 24.0f * d * d;
    comp_ = driveCompensation(drive_);
    warm_ = clampv(warmth, 0.0f, 1.0f);
    const float ch = driveCharacter(drive_);
    for (Core* x : {&cur_, &old_})
    {
        x->chain.character = x->multi.mv.character = ch;
        x->chain.warm = x->multi.warm = warm_;
    }
    if (cur_.model < 0) setup(cur_, 0, FT_LP, -1);
    updateCore(cur_, c, rq, sr);
    if (fade_ > 0) updateCore(old_, c, rq, sr);
}

void MegaFilter::updateCore(Core& k, float c, float res, double sr)
{
    if (k.classic) k.chain.update(c, res, sr);
    else k.multi.update(c, res, sr);
}

void MegaFilter::reset()
{
    cur_.chain.reset(); cur_.multi.reset();
    old_.chain.reset(); old_.multi.reset();
    fade_ = 0;
}

void MegaFilter::processBlock(float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float yl = run(cur_, l[i], 0);
        float yr = run(cur_, r[i], 1);
        if (fade_ > 0)
        {
            const float ol = run(old_, l[i], 0);
            const float orr = run(old_, r[i], 1);
            const float lin = 1.0f - (float)fade_ / (float)kFadeLen;
            const float f = lin * lin * (3.0f - 2.0f * lin);
            yl = ol + (yl - ol) * f;
            yr = orr + (yr - orr) * f;
            --fade_;
        }
        l[i] = yl * comp_;
        r[i] = yr * comp_;
    }
}

} // namespace mega
} // namespace helix
