// The Mega Synth filter models, ported into HELIX.
//
// 17 classic models (Standard LP, Sallen-Key, Ladder, Steiner-Parker, ... SH-2), each with
// six types (Low / High / Band Pass, Notch, Peak, All Pass) at 6/12/18/24 dB.
//   - A model at Low Pass and its own slope runs its original chain (biquads, ladders, shapers).
//   - Anything else runs the multimode core (zero-delay-feedback SVFs, one-poles, a 4-pole
//     ladder) with the model lending its character: drive stage, resonance strength,
//     how hard the resonance saturates, output shaping.
// Model / type / slope changes crossfade over ~10 ms so switching doesn't click.
#pragma once
#include <algorithm>
#include <cmath>

namespace helix {
namespace mega {

constexpr double kPi = 3.14159265358979323846;
template <typename T> inline T clampv(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

enum FilterKind { FT_LP, FT_HP, FT_BP, FT_NOTCH, FT_PEAK, FT_AP, FT_TYPE_COUNT };
constexpr int kNumModels = 17;

// Each model's own slope (0..3 = 6/12/18/24 dB): Low Pass at this slope is the original chain.
int nativeSlope(int model);
// Slope a type actually uses (Peak has none, Band Pass / Notch only 12 and 24).
int effectiveSlope(int type, int slope);

inline float fastTanh(float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
           / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}
inline float driveShape(float x, float k) { return fastTanh(k * clampv(x, -1.0f, 1.0f)); }

// Web Audio style biquad (LP/HP q in dB, BP/notch q as Q).
struct Biquad
{
    enum Type { LP, HP, BP, NOTCH };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0}, z2[2] = {0, 0};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void set(Type type, double freq, double q, double sampleRate);
    inline float process(float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float)y;
    }
};

struct FilterChain
{
    int mode = -1, numStages = 0;
    Biquad st[4];
    Biquad::Type types[4] {};
    float fbState[2] {0, 0}, fbGain = 0.0f;
    enum FbKind { FbNone, FbToInput, FbToPre } fbKind = FbNone;
    float preK = 0, postK = 0, character = 0;
    bool limitOut = false, ladder = false, tb303 = false;
    float lg[4] {0.5f, 0.5f, 0.5f, 0.5f};
    float ls[2][4] {};
    float lk = 0, tbA = 0.98f;
    float tbX[2] {0, 0}, tbY[2] {0, 0};
    float warm = 0;
    float dcX[2] {0, 0}, dcY[2] {0, 0}, dcR = 0.9987f;

    void configure(int newMode);
    void update(float cutoff, float res, double sr);
    void setLadder(float cutoff, float spread, float k, double sr);
    void reset();
    float process(float x, int ch, float drive);
};

struct ModelVoice
{
    float preK = 0, postK = 0, character = 0, resScale = 1, satT = 3;
    bool limitOut = false;
};

struct Svf
{
    float a1 = 1, a2 = 0, a3 = 0, k = 2;
    float ic1[2] {0, 0}, ic2[2] {0, 0};
    void set(float g, float damping)
    {
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick(float v0, int ch, float& lp, float& bp, float sat);
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
};

struct OnePole
{
    float G = 0.5f;
    float s[2] {0, 0};
    void set(float g) { G = g / (1.0f + g); }
    inline float lp(float x, int ch)
    {
        const float v = (x - s[ch]) * G;
        const float y = v + s[ch];
        s[ch] = y + v;
        return y;
    }
    void reset() { s[0] = s[1] = 0.0f; }
};

struct MultiCore
{
    int type = FT_LP, slope = 1;
    ModelVoice mv;
    float warm = 0, r = 0;
    Svf a, b;
    OnePole p;
    float bump = 0, bpGainA = 1, bpGainB = 1, peakGain = 0;
    float lg = 0.5f, lk = 0;
    float ls[2][4] {}, lfb[2] {0, 0};
    float dcX[2] {0, 0}, dcY[2] {0, 0}, dcR = 0.9987f;

    void configure(int filterType, int effSlope, int model);
    void update(float cutoff, float res, double sr);
    void reset();
    float process(float x, int ch, float drive);
};

// One complete stereo filter: drive, model, gain compensation, crossfaded switching.
class MegaFilter
{
public:
    // model 0..16, type FilterKind, slope -1 = the model's own, 0..3 = 6/12/18/24 dB
    void configure(int model, int type, int slope);
    // Per control block. cutoff Hz; res 0..1; drive 0..1; warmth 0..1.
    void update(float cutoffHz, float res, float drive, float warmth, double sr);
    void reset();
    void processBlock(float* l, float* r, int n);

private:
    struct Core
    {
        FilterChain chain;
        MultiCore multi;
        bool classic = true;
        int model = -1, type = -1, slope = -2;
    };
    void setup(Core& c, int model, int type, int slope);
    void updateCore(Core& c, float cutoff, float res, double sr);
    inline float run(Core& c, float x, int ch) { return c.classic ? c.chain.process(x, ch, drive_) : c.multi.process(x, ch, drive_); }

    static constexpr int kFadeLen = 480;
    Core cur_, old_;
    int fade_ = 0;
    float drive_ = 1.0f, comp_ = 1.0f, warm_ = 0.0f;
};

} // namespace mega
} // namespace helix
