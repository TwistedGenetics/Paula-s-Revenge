// Ported into HELIX from TwistedGenetics' own logue SDK unit (see README.md here); sample rate made settable.
#pragma once
/*
 *  hoover_dsp.h — Hoover / Reese oscillator DSP core (portable C++, no SDK deps)
 *
 *  Twisted Genetics — microKORG 2 logue SDK oscillator
 *
 *  Per voice: a stack of detuned "PWM-saw" members. Each member is a band-limited
 *  (polyBLEP) saw plus a twin saw whose phase offset is swept by an LFO — the
 *  classic Alpha Juno pulse-saw motion that sits under the hoover sound.
 *  On top: a pitch dive on note-on, chorus-style pitch smear across the stack,
 *  slow analogue drift, a sine sub an octave down, and soft-clip drive.
 *
 *  Kept dependency-free so it can be host-tested and rendered to WAV off the unit.
 */

#include <cstdint>
#include <cmath>

namespace hoover {

constexpr int   kMaxVoices  = 8;
constexpr int   kMaxMembers = 4;
constexpr int   kMaxBlock   = 64;
inline thread_local float kSampleRate = 48000.f;   // HELIX: per thread, the rate of the engine being rendered (was constexpr 48 kHz)
constexpr float kTwoPi      = 6.283185307f;

enum Mode { kModeHoover = 0, kModeMentasm, kModeReese, kModeRave, kNumModes };

// Mod-matrix destinations (normalised offsets added to the knob value)
enum ModDest { kModDetune = 0, kModDive, kModPwm, kModChorus, kModSub, kModDrive, kNumModDest };

struct Params {
  int   mode     = kModeHoover;
  float detune   = 0.45f;  // 0..1
  float dive     = 0.5f;   // -1..1  (x 24 semitones)
  float diveTime = 0.35f;  // 0..1  (5 ms .. 2 s)
  float pwm      = 0.6f;   // 0..1
  float pwmRate  = 0.3f;   // 0..1  (0.1 .. 8 Hz)
  float chorus   = 0.5f;   // 0..1
  float sub      = 0.25f;  // 0..1
  float drive    = 0.1f;   // 0..1
  float drift    = 0.3f;   // 0..1
  float transpose = 0.f;   // semitones (semi + fine)
};

struct Member {      // one layer of the stack, as a mode defines it
  float spread;      // -1..1, scaled by detune amount
  float semis;       // fixed interval (octave layers)
  bool  twin;        // PWM-saw twin on/off
};

struct ModeDef {
  int    count;
  float  maxCents;   // detune at Detune = 100%
  Member m[kMaxMembers];
};

static const ModeDef kModes[kNumModes] = {
  // HOOVER: three PWM-saws, wide-ish detune
  {3, 32.f, {{-1.f, 0.f, true}, {0.f, 0.f, true}, {1.f, 0.f, true}, {0.f, 0.f, false}}},
  // MENTASM: detuned trio plus an octave-down PWM-saw for the growl
  {4, 28.f, {{-1.f, 0.f, true}, {0.f, 0.f, true}, {1.f, 0.f, true}, {0.35f, -12.f, true}}},
  // REESE: two plain saws beating against each other
  {2, 24.f, {{-1.f, 0.f, false}, {1.f, 0.f, false}, {0.f, 0.f, false}, {0.f, 0.f, false}}},
  // RAVE: trio plus an octave-up PWM-saw for the scream
  {4, 30.f, {{-1.f, 0.f, true}, {0.f, 0.f, true}, {1.f, 0.f, true}, {-0.5f, 12.f, true}}},
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

static inline float wrap01(float p) { return p - (float)(int32_t)p; }  // p >= 0

// 2-sample polyBLEP residual for a unit downward step at phase wrap
static inline float polyblep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.f;
  }
  if (t > 1.f - dt) {
    t = (t - 1.f) / dt;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

static inline float blepSaw(float ph, float dt) { return 2.f * ph - 1.f - polyblep(ph, dt); }

// sin(2*pi*t) for t in [0,1), ~0.1% error
static inline float sinUnit(float t) {
  const float x = 2.f * t - 1.f;
  const float ax = x < 0.f ? -x : x;
  float y = -4.f * x * (1.f - ax);
  const float ay = y < 0.f ? -y : y;
  return 0.225f * (y * ay - y) + y;
}

// Rational tanh approximation, exact at 0, saturates to +-1
static inline float softclip(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

static inline float noteToDt(float note) {
  // 440 * 2^((n-69)/12) / fs, clamped below Nyquist-ish for BLEP sanity
  const float hz = 440.f * std::exp2((note - 69.f) * (1.f / 12.f));
  return clampf(hz * (1.f / kSampleRate), 0.f, 0.45f);
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

class Engine {
 public:
  Params params;
  float  mod[kNumModDest][kMaxVoices];  // per-voice mod offsets from the mk2 matrix

  Engine() { reset(); }

  void reset() {
    uint32_t seed = 0x1234567u;
    for (int v = 0; v < kMaxVoices; v++) {
      for (int k = 0; k < kMaxMembers; k++) {
        // free-running phases, scattered so the stack never starts phase-locked
        seed = seed * 1664525u + 1013904223u;
        phase_[v][k] = (seed >> 8) * (1.f / 16777216.f);
        twinPh_[v][k] = 0.f;
        dt_[v][k] = 0.f;
        drift_[v][k] = 0.f;
      }
      subPh_[v] = 0.f;
      subDt_[v] = 0.f;
      dive_[v] = 0.f;
      pwmLfo_[v] = 0.13f * v;        // decorrelate voices
      chorusLfo_[v] = 0.29f * v;
      driveG_[v] = 1.f;
      driveMk_[v] = 1.f;
      driveWet_[v] = 0.f;
      fresh_[v] = true;
    }
    rng_ = 0x9E3779B9u;
    for (int d = 0; d < kNumModDest; d++)
      for (int v = 0; v < kMaxVoices; v++) mod[d][v] = 0.f;
  }

  // Note-on / voice steal: restart the pitch dive.
  // fresh = the voice was idle, so jump straight to the new pitch instead of
  // gliding from whatever note it last played (a steal keeps the glide: no click).
  void trigger(int v, bool fresh = false) {
    if (v < 0 || v >= kMaxVoices) return;
    dive_[v] = 1.f;
    if (fresh) fresh_[v] = true;
  }

  // Render one voice. note = MIDI note number (fractional ok). out gets 'frames' mono samples.
  void render(int v, float note, float *out, int frames) {
    while (frames > 0) {
      const int n = frames > kMaxBlock ? kMaxBlock : frames;
      renderBlock(v, note, out, n);
      out += n;
      frames -= n;
    }
  }

 private:
  float phase_[kMaxVoices][kMaxMembers];
  float twinPh_[kMaxVoices][kMaxMembers];  // current twin phase offset
  float dt_[kMaxVoices][kMaxMembers];
  float drift_[kMaxVoices][kMaxMembers];
  float subPh_[kMaxVoices];
  float subDt_[kMaxVoices];
  float dive_[kMaxVoices];      // dive envelope, 1 -> 0
  float pwmLfo_[kMaxVoices];
  float chorusLfo_[kMaxVoices];
  float driveG_[kMaxVoices];    // drive gain / make-up / wet, ramped per block
  float driveMk_[kMaxVoices];
  float driveWet_[kMaxVoices];
  bool  fresh_[kMaxVoices];
  uint32_t rng_;

  float rand11() {  // white -1..1
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (int32_t)rng_ * (1.f / 2147483648.f);
  }

  void renderBlock(int v, float note, float *out, int frames) {
    const Params &p = params;
    const int modeIdx = (p.mode >= 0 && p.mode < kNumModes) ? p.mode : 0;
    const ModeDef &md = kModes[modeIdx];
    const float blockSec = frames * (1.f / kSampleRate);

    // ---- per-block control values (knob + mod matrix) ----
    const float detune = clampf(p.detune + mod[kModDetune][v], 0.f, 1.f);
    const float diveAmt = clampf(p.dive + mod[kModDive][v], -1.f, 1.f);
    const float pwm = clampf(p.pwm + mod[kModPwm][v], 0.f, 1.f);
    const float chorus = clampf(p.chorus + mod[kModChorus][v], 0.f, 1.f);
    const float sub = clampf(p.sub + mod[kModSub][v], 0.f, 1.f);
    const float drive = clampf(p.drive + mod[kModDrive][v], 0.f, 1.f);

    // dive envelope: exponential return to pitch, 5 ms .. 2 s time constant
    const float tau = 0.005f * std::exp(p.diveTime * 5.991f);  // ln(400)
    const float diveSemis = diveAmt * 24.f * dive_[v];
    dive_[v] *= std::exp(-blockSec / tau);
    if (dive_[v] < 1e-4f) dive_[v] = 0.f;

    // LFOs (block rate is plenty for these slow sweeps)
    const float pwmHz = 0.1f * std::exp2(p.pwmRate * 6.32f);  // 0.1 .. 8 Hz
    pwmLfo_[v] = wrap01(pwmLfo_[v] + pwmHz * blockSec);
    chorusLfo_[v] = wrap01(chorusLfo_[v] + 0.55f * blockSec);

    // detune curve: squared so low settings give slow Reese beating
    const float detCents = detune * detune * md.maxCents;
    const float chorusCents = chorus * 14.f;
    const float driftCents = p.drift * 7.f;

    const float baseNote = note + p.transpose + diveSemis;
    float dtEnd[kMaxMembers];
    float twinEnd[kMaxMembers];
    for (int k = 0; k < md.count; k++) {
      const Member &m = md.m[k];
      // slow random walk per member
      float d = drift_[v][k] * 0.995f + rand11() * 0.02f;
      drift_[v][k] = clampf(d, -1.f, 1.f);

      const float smear = chorusCents * sinUnit(wrap01(chorusLfo_[v] + (float)k / md.count));
      const float cents = m.spread * detCents + smear + drift_[v][k] * driftCents;
      dtEnd[k] = noteToDt(baseNote + m.semis + cents * 0.01f);

      // twin phase offset sweeps 0 .. pwm*0.5 (0.5 = octave-up character)
      const float lfo = 0.5f + 0.5f * sinUnit(wrap01(pwmLfo_[v] + 0.21f * k));
      twinEnd[k] = m.twin ? (0.02f + pwm * 0.46f * lfo) : 0.f;
    }
    const float subDtEnd = noteToDt(note + p.transpose + diveSemis - 12.f);

    // ---- gains ----
    static const float kStackGain[kMaxMembers + 1] = {0.f, 1.f, 0.7071f, 0.5774f, 0.5f};
    const float stackGain = kStackGain[md.count];
    const float subGain = sub * 0.9f;
    // Drive: 0 = clean bypass. Above 0, the stack (scaled into the clipper's
    // near-linear range) is pushed by +0..+30 dB on an even dB taper, then
    // brought back down so heavy settings get only slightly louder, not much louder.
    const float driveDb = drive * 30.f;
    const float gEnd = std::exp2(driveDb * (1.f / 6.0206f));     // 1 .. ~31.6
    // make-up from the clipper's own gain at a typical stack level (kTypRms into the clipper),
    // so loudness rises gently (~+3 dB at 100 %) instead of jumping or collapsing
    constexpr float kTypRms = 0.23f;
    const float mkEnd = kTypRms * (1.f + drive * 0.4f) / softclip(gEnd * kTypRms);
    const float wetEnd = clampf(drive * 12.f, 0.f, 1.f);         // fade the clipper in over the first ~8%
    const float outTrim = 0.32f;
    const float invN = 1.f / frames;

    if (fresh_[v]) {  // first block for this voice: no ramp from zero
      for (int k = 0; k < md.count; k++) {
        dt_[v][k] = dtEnd[k];
        twinPh_[v][k] = twinEnd[k];
      }
      subDt_[v] = subDtEnd;
      driveG_[v] = gEnd;
      driveMk_[v] = mkEnd;
      driveWet_[v] = wetEnd;
      fresh_[v] = false;
    }

    // ---- per-member render, accumulated into out ----
    for (int i = 0; i < frames; i++) out[i] = 0.f;

    for (int k = 0; k < md.count; k++) {
      const bool twin = md.m[k].twin;
      float ph = phase_[v][k];
      float dt = dt_[v][k];
      const float dtInc = (dtEnd[k] - dt) * invN;
      float tw = twinPh_[v][k];
      const float twInc = (twinEnd[k] - tw) * invN;

      if (twin) {
        for (int i = 0; i < frames; i++) {
          const float s1 = blepSaw(ph, dt);
          const float ph2 = wrap01(ph + tw);
          const float s2 = blepSaw(ph2, dt);
          out[i] += 0.5f * (s1 + s2);
          ph += dt;
          if (ph >= 1.f) ph -= 1.f;
          dt += dtInc;
          tw += twInc;
        }
      } else {
        for (int i = 0; i < frames; i++) {
          out[i] += blepSaw(ph, dt);
          ph += dt;
          if (ph >= 1.f) ph -= 1.f;
          dt += dtInc;
        }
      }
      phase_[v][k] = ph;
      dt_[v][k] = dtEnd[k];
      twinPh_[v][k] = twinEnd[k];
    }

    // ---- sub, drive, output ----
    float sph = subPh_[v];
    float sdt = subDt_[v];
    const float sdtInc = (subDtEnd - sdt) * invN;
    // ramp drive settings across the block so knob moves don't zipper
    float g = driveG_[v], mk = driveMk_[v], wet = driveWet_[v];
    const float gInc = (gEnd - g) * invN, mkInc = (mkEnd - mk) * invN, wetInc = (wetEnd - wet) * invN;
    const bool clean = wet <= 0.f && wetEnd <= 0.f;
    constexpr float kHead = 1.f / 3.f;  // stack + sub peaks ~3 -> ~1 into the clipper
    for (int i = 0; i < frames; i++) {
      const float x = out[i] * stackGain + subGain * sinUnit(sph);
      sph += sdt;
      if (sph >= 1.f) sph -= 1.f;
      sdt += sdtInc;
      float y = x;
      if (!clean) {
        const float shaped = softclip(x * kHead * g) * 3.f * mk;
        y = x + (shaped - x) * wet;
        g += gInc; mk += mkInc; wet += wetInc;
      }
      out[i] = y * outTrim;
    }
    driveG_[v] = gEnd; driveMk_[v] = mkEnd; driveWet_[v] = wetEnd;
    subPh_[v] = sph;
    subDt_[v] = subDtEnd;
  }
};

}  // namespace hoover
