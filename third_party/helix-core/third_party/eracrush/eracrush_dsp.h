#pragma once
/*
 *  eracrush_dsp.h — Sampler-era crusher DSP core (portable C++, no SDK deps)
 *
 *  Twisted Genetics — microKORG 2 logue SDK mod FX
 *
 *  Signal path per channel:
 *    drive -> anti-alias pre-filter -> converter clip -> sample & hold at the machine's
 *    rate -> quantiser (linear or companded) -> reconstruction / output filter -> mix
 *
 *  The machine sets native rate, bit depth, quantiser type and output filter voicing.
 *  These are characterful approximations of the classic boxes, not circuit models.
 */

#include <cstdint>
#include <cmath>

namespace eracrush {

inline float kSampleRate = 48000.f;   // HELIX: set to the host rate (FxLibrary.cpp)
constexpr float kPi = 3.14159265f;

enum Machine { kSP1200 = 0, kMPC60, kS950, kS1000, kAmiga, kNumMachines };

struct MachineSpec {
  float rate;        // native sample rate (Hz)
  int   bits;        // native word length
  float muLaw;       // >0 = companded "non-linear" quantiser (mu), 0 = linear
  float postHz;      // output filter cutoff at Tone = 50%
  float postQ1;      // first output biquad Q
  float postQ2;      // second output biquad Q (0 = 2-pole only)
  bool  postTracks;  // output filter follows the playback rate (variable-rate samplers)
  float clipKnee;    // 0 = hard converter clip, >0 = softer analogue front end
  float hiss;        // converter / analogue noise floor at Age = 100%
};

static const MachineSpec kSpecs[kNumMachines] = {
  // SP-1200: 26.04 kHz, 12-bit linear, gritty 4-pole output filter with a little bite
  {26040.f, 12, 0.f, 11000.f, 0.62f, 1.10f, false, 0.f, 0.0020f},
  // MPC60: 40 kHz, 12-bit companded (the "16-bit sounding 12-bit"), smooth 2-pole
  {40000.f, 12, 18.f, 15000.f, 0.71f, 0.f, false, 0.35f, 0.0010f},
  // S950: up to 48 kHz, 12-bit, filters track the sample rate
  {31250.f, 12, 0.f, 0.f, 0.71f, 0.f, true, 0.15f, 0.0015f},
  // S1000: 44.1 kHz, 16-bit, clean — mostly for pitch-down rate tricks
  {44100.f, 16, 0.f, 0.f, 0.71f, 0.f, true, 0.3f, 0.0004f},
  // Amiga (Paula): ~28.9 kHz, 8-bit, fixed 4.4 kHz-ish "LED" filter voicing
  {28867.f, 8, 0.f, 4400.f, 0.66f, 0.f, false, 0.f, 0.0030f},
};

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ---------------------------------------------------------------------------
// Biquad (transposed direct form II), RBJ low-pass
// ---------------------------------------------------------------------------
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1[2] = {0, 0}, z2[2] = {0, 0};
  bool  bypass = true;

  void setLowpass(float hz, float q) {
    if (hz >= 0.45f * kSampleRate) {  // past ~21.6 kHz: treat as open
      bypass = true;
      return;
    }
    bypass = false;
    hz = hz < 20.f ? 20.f : hz;
    const float w = 2.f * kPi * hz / kSampleRate;
    const float cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw / (2.f * q);
    const float a0 = 1.f + alpha;
    const float inv = 1.f / a0;
    b0 = (1.f - cw) * 0.5f * inv;
    b1 = (1.f - cw) * inv;
    b2 = b0;
    a1 = -2.f * cw * inv;
    a2 = (1.f - alpha) * inv;
  }

  inline float process(float x, int ch) {
    if (bypass) return x;
    const float y = b0 * x + z1[ch];
    z1[ch] = b1 * x - a1 * y + z2[ch];
    z2[ch] = b2 * x - a2 * y;
    return y;
  }

  void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0.f; }
};

struct Params {
  int   machine = kSP1200;
  float rate  = 1.f;   // 0..1  (native / ~20 .. native)
  int   bits  = 0;     // 0 = machine native, else 1..16
  float drive = 0.2f;  // 0..1  (0 .. +18 dB into the converter)
  float alias = 0.4f;  // 0..1  (0 = tracking AA filter, 1 = raw aliasing)
  float tone  = 0.5f;  // 0..1  (0.5 = machine voicing, 1 = output filter open)
  float age   = 0.15f; // 0..1  (hiss, clock jitter)
  float mix   = 1.f;   // 0..1
};

class Crusher {
 public:
  Params params;

  Crusher() { reset(); }

  void reset() {
    for (int s = 0; s < kPreStages; s++) pre_[s].clear();
    post1_.clear(); post2_.clear();
    held_[0] = held_[1] = 0.f;
    phase_ = 0.f;
    mixZ_ = params.mix;
    rng_ = 0x2545F491u;
  }

  // Stereo interleaved in/out, any frame count
  void process(const float *in, float *out, int frames) {
    update();
    const MachineSpec &m = kSpecs[machine_];
    const float inc = rateEff_ / kSampleRate;
    const float jitter = params.age * 0.04f;
    const float hiss = params.age * m.hiss;
    const float knee = m.clipKnee;
    const float mixTarget = params.mix;
    const float mixInc = (mixTarget - mixZ_) / (frames > 0 ? frames : 1);

    for (int i = 0; i < frames; i++) {
      const float dryL = in[2 * i], dryR = in[2 * i + 1];

      // front end: drive + anti-alias filter (runs every sample, like the analogue input stage)
      float l = dryL * gain_, r = dryR * gain_;
      for (int s = 0; s < kPreStages; s++) {
        l = pre_[s].process(l, 0);
        r = pre_[s].process(r, 1);
      }

      // sample clock with optional jitter; both channels share one clock
      phase_ += inc * (1.f + jitter * rand11());
      if (phase_ >= 1.f) {
        phase_ -= (float)(int32_t)phase_;
        held_[0] = quantise(clip(l + hiss * rand11(), knee));
        held_[1] = quantise(clip(r + hiss * rand11(), knee));
      }

      // reconstruction: zero-order hold through the machine's output filter
      float wl = post2_.process(post1_.process(held_[0], 0), 0) * makeup_;
      float wr = post2_.process(post1_.process(held_[1], 1), 1) * makeup_;

      mixZ_ += mixInc;
      out[2 * i]     = dryL + (wl - dryL) * mixZ_;
      out[2 * i + 1] = dryR + (wr - dryR) * mixZ_;
    }
    mixZ_ = mixTarget;
  }

 private:
  static constexpr int kPreStages = 4;  // 8-pole Butterworth anti-alias filter
  Biquad pre_[kPreStages];
  Biquad post1_, post2_;
  float held_[2];
  float phase_;
  float mixZ_;
  uint32_t rng_;

  // cached per-block values
  int   machine_ = 0;
  float rateEff_ = 26040.f;
  float gain_ = 1.f, makeup_ = 1.f;
  float q_ = 2048.f, qInv_ = 1.f / 2048.f;
  float mu_ = 0.f, muNorm_ = 1.f;

  float rand11() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (int32_t)rng_ * (1.f / 2147483648.f);
  }

  static inline float clip(float x, float knee) {
    if (knee <= 0.f) return clampf(x, -1.f, 1.f);  // hard converter clip
    // soft knee: linear up to (1-knee), then smooth into +-1
    const float t = 1.f - knee;
    const float ax = x < 0.f ? -x : x;
    if (ax <= t) return x;
    const float over = (ax - t) / knee;
    const float y = t + knee * (over / (1.f + over));
    return x < 0.f ? -y : y;
  }

  inline float quantise(float x) const {
    if (mu_ > 0.f) {
      // compand -> quantise -> expand (mu-law style "non-linear" converter)
      const float ax = x < 0.f ? -x : x;
      float c = std::log1p(mu_ * ax) * muNorm_;
      c = std::floor(c * q_ + 0.5f) * qInv_;
      const float e = std::expm1(c / muNorm_) / mu_;
      return x < 0.f ? -e : e;
    }
    return std::floor(x * q_ + 0.5f) * qInv_;
  }

  void update() {
    const Params &p = params;
    machine_ = (p.machine >= 0 && p.machine < kNumMachines) ? p.machine : 0;
    const MachineSpec &m = kSpecs[machine_];

    // Rate: 100% = native, 0% = native / ~20 (4.3 octaves down)
    rateEff_ = m.rate * std::exp2(-(1.f - clampf(p.rate, 0.f, 1.f)) * 4.32f);

    // Bits
    const int bits = (p.bits >= 1 && p.bits <= 16) ? p.bits : m.bits;
    q_ = (float)(1 << (bits - 1));
    if (q_ < 1.f) q_ = 1.f;
    qInv_ = 1.f / q_;
    mu_ = m.muLaw;
    muNorm_ = mu_ > 0.f ? 1.f / std::log1p(mu_) : 1.f;

    // Drive: 0 .. +18 dB in, partial make-up out so it gets louder *and* dirtier
    const float driveDb = clampf(p.drive, 0.f, 1.f) * 18.f;
    gain_ = std::pow(10.f, driveDb / 20.f);
    makeup_ = std::pow(10.f, -driveDb * 0.6f / 20.f);

    // Anti-alias pre-filter: steep 8-pole tracking the effective rate at Alias 0 (Akai-style),
    // sweeping up ~5.5 octaves so 100% is fully open (SP-1200-style raw aliasing)
    static const float kButterQ[kPreStages] = {0.5098f, 0.6013f, 0.9000f, 2.5629f};
    const float aaHz = 0.40f * rateEff_ * std::exp2(clampf(p.alias, 0.f, 1.f) * 5.5f);
    for (int s = 0; s < kPreStages; s++) pre_[s].setLowpass(aaHz, kButterQ[s]);

    // Output filter: machine voicing, +-2 octaves with Tone, fully open at 100%
    float postHz = m.postTracks ? 0.45f * rateEff_ : m.postHz;
    const float tone = clampf(p.tone, 0.f, 1.f);
    if (tone >= 0.99f) {
      postHz = 1e9f;  // open
    } else {
      postHz *= std::exp2((tone - 0.5f) * 4.f);
    }
    post1_.setLowpass(postHz, m.postQ1);
    if (m.postQ2 > 0.f)
      post2_.setLowpass(postHz, m.postQ2);
    else
      post2_.setLowpass(1e9f, 0.71f);
  }
};

}  // namespace eracrush
