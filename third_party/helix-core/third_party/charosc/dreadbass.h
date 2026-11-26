// Ported into HELIX from TwistedGenetics' own logue SDK unit (see README.md here); sample rate made settable.
#pragma once
/*
 * Dread Bass - jungle / DnB character-bass oscillator (DSP core)
 *
 * Sibling of Rollers (clean sub + Reese). Dread Bass covers the dirtier,
 * more percussive end: dread, donk, growl and hoover.
 *
 * One engine, two builds:
 *   DreadBass1 = Engine<float>  one voice (desktop tests, future NTS-1 port)
 *   DreadBass4 = Engine<v4f>    four voices, one per NEON lane (microKORG 2)
 * Both run exactly the same code.
 *
 * Per voice:
 *
 *   MODE   DREAD  the Dead Dred "Dred Bass" trick: a 303-style square
 *                 note played backwards. PolyBLEP square -> resonant 24 dB
 *                 low-pass (2 SVFs) whose cutoff RISES at a steady rate in
 *                 octaves over EG TIME, from about 2x to 16x the note (the
 *                 reversed filter decay), level held steady. Measured off a
 *                 reference loop. TIMBRE = how far it opens, CHARACTER =
 *                 resonance, DRIVE adds Amiga-style 8-bit / low-rate crunch
 *          DONK   square -> the same resonant 24 dB low-pass, Q up to ~25.
 *                 The cutoff opens ~1.3 octaves above the ring harmonic,
 *                 settles onto it within ~40 ms and rings there, then
 *                 the resonance dies away over EG TIME (the cutoff closing
 *                 only ~1/3 octave), so the ring fades in place.
 *                 No pitch drop: the "donk" is the resonance landing on a
 *                 harmonic (5th by default). Measured off a donk loop.
 *                 TIMBRE = ring harmonic (~2nd..8th, 5th at 700),
 *                 CHARACTER = resonance. Saturated between the two filter
 *                 stages, so the ring doesn't tower over the body and the
 *                 tail is a dark, rounded square.
 *          GROWL  2-op FM, 1:1 ratio, index follows the shape EG
 *                 (TIMBRE: index, CHARACTER: modulator feedback)
 *          HOOVER two detuned band-limited pulses with slow PWM
 *                 (TIMBRE: PWM depth, CHARACTER: detune)
 *   + KNOCK  short pitched sine blip on note-on
 *   -> DRIVE (cubic soft clip at 2x rate, gain compensated) -> DC blocker
 *   -> (DREAD) sample-and-hold + bit reduction
 *   + SUB    pure sine, 1 or 2 octaves down, after the drive (never dirty)
 *   x level  PLUCK decay (0 = sustain; in DREAD it starts once the sweep peaks)
 *   -> output limiter (transparent below 0.8, soft knee to 1)
 *
 *   SHAPE EG: one exponential envelope per voice, restarted on note-on.
 *   EG INT (semitones) bends the pitch with it (hoover dive, pitch drops);
 *   in GROWL it also lifts the FM index. In DREAD EG TIME is the length of
 *   the rising sweep, in DONK how long the filter takes to close.
 *
 * CPU rules (learned on Dusty Pads / Rollers, the microKORG 2 runs up to 8
 * voices of this):
 *   - everything slow (pitch, envelopes' block factors, coefficients) is
 *     worked out once per buffer per voice; the sample loops only step ramps
 *   - the per-sample work runs in short passes that each fit in the 16 NEON
 *     q registers (one big loop spills to the stack and crackles)
 *   - no compares in the sample loops: max(0, x) is 0.5 * (x + |x|), clamps
 *     use two abs; GCC 10 turns vector compares into scalar code on ARM
 *   - pulses and saws are polyBLEP (stateless, safe under fast PWM), sines
 *     are refined parabolas, no tables, no divides in the sample loop,
 *     no libm, no memory allocation
 *   - only the passes the current MODE needs run; KNOCK is skipped when off
 */

#include <stdint.h>
#include <string.h>

namespace dread {

// PAULA: MSVC has no GCC vector extensions; only the 1-lane float engine is
// used by Helix, so the 4-lane parts are left out there
#if (defined(_MSC_VER) && !defined(__clang__)) || defined(DB_FORCE_NO_V4)
#define DB_NO_V4 1
#ifdef _MSC_VER
#define DB_INLINE __forceinline
#else
#define DB_INLINE inline
#endif
#else
typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
typedef unsigned int v4u __attribute__((vector_size(16)));
#define DB_INLINE inline __attribute__((always_inline))
#endif

// HELIX: per thread, set to the rate of the engine being rendered (were constexpr 48 kHz)
inline thread_local float kSR = 48000.f;
inline thread_local float kInvSR = 1.f / 48000.f;
inline thread_local float kW0A4 = 440.f / 48000.f;  // MIDI 69, cycles per sample
inline void setSampleRate(float sr) { kSR = sr; kInvSR = 1.f / sr; kW0A4 = 440.f / sr; }
enum { kMaxBlock = 64 };

// ---------------------------------------------------------------------------
// Scalar helpers (control rate)
// ---------------------------------------------------------------------------

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }

// 2^x, about 1e-6 relative error (same as Dusty Pads)
static inline float exp2f_fast(float x) {
  x = clampf(x, -60.f, 60.f);
  const float fl = (float)(int32_t)(x < 0.f ? x - 1.f : x);
  const float xi = (x == fl + 1.f) ? x : fl;
  const float f = x - xi;
  const float p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
  union { float f; int32_t i; } u;
  u.i = ((int32_t)xi + 127) << 23;
  return p * u.f;
}

// tan(x) for 0 <= x <= 0.3
static inline float tanSmall(float x) {
  const float x2 = x * x;
  return x * (1.f + x2 * (0.33333333f + x2 * (0.13333333f + x2 * 0.05396825f)));
}

// tan(x) for 0 <= x <= 1.2, Pade [5/4] (filter cutoffs up to ~18 kHz)
static inline float tanPade(float x) {
  const float x2 = x * x;
  return x * (945.f - 105.f * x2 + x2 * x2) / (945.f - 420.f * x2 + 15.f * x2 * x2);
}

// TPT state-variable filter coefficients for a cutoff in cycles per sample
// and damping k = 1 / Q
static inline void svfCoefs(float fc, float k, float &a1, float &a2, float &a3) {
  const float g = tanPade(3.14159265f * clampf(fc, 10.f * (1.f / 48000.f), 0.36f));
  a1 = 1.f / (1.f + g * (g + k));
  a2 = g * a1;
  a3 = g * a2;
}

// cubic soft clip, scalar version of sat() below
static inline float satf(float x) {
  const float c = clampf(x, -1.f, 1.f);
  return c * (1.5f - 0.5f * c * c);
}

// ---------------------------------------------------------------------------
// Lane helpers: the same names for float (1 lane) and v4f (4 lanes)
// ---------------------------------------------------------------------------

template <class V> struct LaneInfo;
template <> struct LaneInfo<float> { enum { N = 1 }; };
#ifndef DB_NO_V4
template <> struct LaneInfo<v4f> { enum { N = 4 }; };
#endif

static DB_INLINE float lget(float x, int) { return x; }
#ifndef DB_NO_V4
static DB_INLINE float lget(v4f x, int j) { return x[j]; }
#endif
static DB_INLINE void lset(float &x, int, float v) { x = v; }
#ifndef DB_NO_V4
static DB_INLINE void lset(v4f &x, int j, float v) { x[j] = v; }
#endif

template <class V> static DB_INLINE V splat(float v);
template <> DB_INLINE float splat<float>(float v) { return v; }
#ifndef DB_NO_V4
template <> DB_INLINE v4f splat<v4f>(float v) { return (v4f){v, v, v, v}; }
#endif

#ifdef DB_NO_V4
static DB_INLINE float absv(float x) { return x < 0.f ? -x : x; }
#else
static DB_INLINE float absv(float x) { return __builtin_fabsf(x); }
static DB_INLINE v4f absv(v4f x) { return (v4f)((v4u)x & 0x7FFFFFFFu); }
#endif

static DB_INLINE float truncv(float x) { return (float)(int32_t)x; }
#ifndef DB_NO_V4
static DB_INLINE v4f truncv(v4f x) {
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 9)
  return __builtin_convertvector(__builtin_convertvector(x, v4i), v4f);
#else
  v4f r;
  for (int j = 0; j < 4; ++j) r[j] = (float)(int32_t)x[j];
  return r;
#endif
}
#endif

// fractional part, t >= 0
template <class V> static DB_INLINE V fracp(V t) { return t - truncv(t); }
// max(0, x) without a compare
template <class V> static DB_INLINE V relu(V x) { return 0.5f * (x + absv(x)); }
// clamp to [-1, 1] with two abs
template <class V> static DB_INLINE V clamp1(V x) { return 0.5f * (absv(x + 1.f) - absv(x - 1.f)); }
// cubic soft clip: odd harmonics, flat at |x| >= 1
template <class V> static DB_INLINE V sat(V x) {
  const V c = clamp1(x);
  return c * (1.5f - 0.5f * c * c);
}

// |m| with the sign of s
#ifdef DB_NO_V4
static DB_INLINE float copysignv(float m, float s) { const float a = m < 0.f ? -m : m; return s < 0.f || (s == 0.f && 1.f / s < 0.f) ? -a : a; }
#else
static DB_INLINE float copysignv(float m, float s) { return __builtin_copysignf(m, s); }
static DB_INLINE v4f copysignv(v4f m, v4f s) { return (v4f)(((v4u)m & 0x7FFFFFFFu) | ((v4u)s & 0x80000000u)); }
#endif

// output limiter: transparent below 0.8, soft knee above, never exceeds 1
// (same curve idea as Rollers, without compares)
template <class V> static DB_INLINE V limit(V x) {
  const V a = absv(x);
  const V o = relu(a - 0.8f);
  return copysignv(a - o + 0.2f * sat(3.3333333f * o), x);
}

// sin(2 pi t) for t in [0, 1): refined parabola (~0.1 % error)
template <class V> static DB_INLINE V sin2pi(V t) {
  const V y = 2.f * t - 1.f;
  const V s0 = 4.f * y - 4.f * y * absv(y);
  const V s = 0.225f * (s0 * absv(s0) - s0) + s0;
  return -s;
}

// polyBLEP rising saw, t in [0, 1), idt = 1 / phase increment
template <class V> static DB_INLINE V blepSaw(V t, V idt) {
  const V a = relu(1.f - t * idt);          // just after the wrap
  const V b = relu(1.f - (1.f - t) * idt);  // just before it
  return 2.f * t - 1.f + a * a - b * b;
}

// ---------------------------------------------------------------------------
// Parameters (same order as microkorg2/header.c)
// ---------------------------------------------------------------------------

struct Params {
  enum Id {
    kTimbre = 0,  // 0..1023  per-mode main timbre
    kDrive,       // 0..100 %
    kMode,        // DREAD, DONK, GROWL, HOOVER
    kChar,        // 0..100 %  per-mode secondary control
    kSub,         // 0..100 %  sine sub level
    kSubOct,      // -1 OCT, -2 OCT
    kKnock,       // 0..100 %  note-on blip
    kPluck,       // 0..100 %  0 = sustain, up = shorter decay
    kEgInt,       // -24..48 semitones, pitch bend of the shape EG
    kEgTime,      // 1..1000 ms
    kRetrig,      // off / on: restart the EGs on legato (stolen) notes
    kSemi,        // -24..24
    kFine,        // -50..50 cents
    kNumParams
  };
  enum { kDread = 0, kDonk, kGrowl, kHoover, kNumModes };

  static const char *modeName(int v) {
    static const char *const n[] = {"DREAD", "DONK", "GROWL", "HOOVER"};
    return n[clampi(v, 0, kNumModes - 1)];
  }
  static const char *subOctName(int v) { return v ? "-2 OCT" : "-1 OCT"; }
};

// Output level per mode so a MODE change stays at about the same loudness
// (from the desktop tests' RMS measurements, see test/render.cc)
struct ModeGain {
  static constexpr float kDread = 0.77f;
  static constexpr float kDonk = 0.85f;
  static constexpr float kGrowl = 0.86f;
  static constexpr float kHoover = 0.80f;
};

constexpr float kSubAmp = 0.9f;       // sub sine amplitude at SUB 100 %
constexpr float kOutLevel = 0.55f;    // before the output limiter
constexpr float kKnockTau = 0.006f;   // knock decay, seconds
constexpr float kAttackCoef = 0.015f; // de-click when a legato note restarts PLUCK (~1.4 ms)
constexpr float kDcCoef = 0.99935f;   // DC blocker pole, ~5 Hz
constexpr float kDonkSnapDepth = 1.3f;  // DONK cutoff starts this many octaves above where it rings
constexpr float kDonkSink = 0.3f;       // ... and closes this many octaves below it over EG TIME
constexpr float kDonkSatIn = 0.9f;      // DONK in-filter saturation: input gain
constexpr float kDonkSatOut = 1.1f;     // ... and output level
constexpr float kDonkHold = 0.05f;      // share of EG TIME it rings before closing

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

template <class V>
class Engine {
 public:
  enum { N = LaneInfo<V>::N };

  void init() {
    p_timbre_ = 700.f / 1023.f;
    p_drive_ = 0.f;
    mode_ = Params::kDread;
    p_char_ = 0.55f;
    p_sub_ = 0.4f;
    subOct_ = 0;
    p_knock_ = 0.f;
    p_pluck_ = 0.f;
    egInt_ = 0.f;
    egTimeMs_ = 300.f;
    retrig_ = 1;
    semi_ = 0.f;
    fine_ = 0.f;
    updateEgCoefs();
    for (int j = 0; j < N; ++j) {
      note_[j] = 36.f;
      mod_timbre_[j] = mod_drive_[j] = mod_char_[j] = mod_sub_[j] = mod_pitch_[j] = 0.f;
      lfo_[j] = 0.f;
      jump_[j] = false;
    }
    resetState();
  }

  // Silence every lane and clear all audio state (unit_reset)
  void resetState() {
    const V z = splat<V>(0.f);
    t_ = t2_ = ts_ = tk_ = z;
    mprev_ = z;
    s1_ = s2_ = s3_ = s4_ = z;
    dcx_ = dcy_ = osx_ = osy_ = z;
    hold_ = hacc_ = z;
    for (int j = 0; j < N; ++j) {
      sw_[j] = ef_[j] = es_[j] = 0.f;
      started_[j] = false;  // no PLUCK decay until the first note start
    }
    penv_ = kenv_ = z;
    dec_ = att_ = splat<V>(1.f);
    first_ = true;
  }

  void setParam(int id, int32_t v) {
    switch (id) {
      case Params::kTimbre: p_timbre_ = clampi(v, 0, 1023) * (1.f / 1023.f); break;
      case Params::kDrive: p_drive_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kMode: mode_ = clampi(v, 0, Params::kNumModes - 1); break;
      case Params::kChar: p_char_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kSub: p_sub_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kSubOct: subOct_ = clampi(v, 0, 1); break;
      case Params::kKnock: p_knock_ = clampi(v, 0, 100) * 0.01f; break;
      case Params::kPluck: p_pluck_ = clampi(v, 0, 100) * 0.01f; updateEgCoefs(); break;
      case Params::kEgInt: egInt_ = (float)clampi(v, -24, 48); break;
      case Params::kEgTime: egTimeMs_ = (float)clampi(v, 1, 1000); updateEgCoefs(); break;
      case Params::kRetrig: retrig_ = clampi(v, 0, 1); break;
      case Params::kSemi: semi_ = (float)clampi(v, -24, 24); break;
      case Params::kFine: fine_ = clampi(v, -50, 50) * 0.01f; break;
      default: break;
    }
  }

  // Fractional MIDI note per lane
  void setNote(int lane, float note) { note_[lane] = note; }

  // Per-voice modulation: added to the normalised parameter; pitch in semitones
  void setMod(int lane, float timbre, float drive, float chr, float sub, float pitchSemis) {
    mod_timbre_[lane] = timbre;
    mod_drive_[lane] = drive;
    mod_char_[lane] = chr;
    mod_sub_[lane] = sub;
    mod_pitch_[lane] = pitchSemis;
  }

  // New note on one lane.
  //   fresh  = the voice was silent: phases start at zero (every hit has the
  //            same punch, the sub is phase-locked to the main osc) and
  //            filters are cleared
  //   legato = the previous note on this voice is still held: the envelopes
  //            restart only when RETRIG is on
  // A voice that is still sounding keeps its phases so it doesn't click.
  void noteOn(int lane, bool fresh, bool legato = false) {
    if (fresh) {
      lset(t_, lane, 0.f);
      lset(t2_, lane, 0.f);
      lset(ts_, lane, 0.f);
      lset(mprev_, lane, 0.f);
      lset(s1_, lane, 0.f);
      lset(s2_, lane, 0.f);
      lset(s3_, lane, 0.f);
      lset(s4_, lane, 0.f);
      lset(hold_, lane, 0.f);
      lset(hacc_, lane, 0.f);
      lset(dcx_, lane, 0.f);
      lset(dcy_, lane, 0.f);
      lset(osx_, lane, 0.f);
      lset(osy_, lane, 0.f);
      lset(att_, lane, 1.f);
      lfo_[lane] = 0.f;  // hoover PWM starts the same on every hit
    } else {
      if (legato && !retrig_) {
        jump_[lane] = true;  // new pitch, envelopes carry on
        return;
      }
      // PLUCK restarts from where it is and rises to full in ~1.4 ms
      lset(att_, lane, lget(dec_, lane) * lget(att_, lane));
    }
    lset(tk_, lane, 0.f);
    lset(penv_, lane, 1.f);
    lset(kenv_, lane, 1.f);
    lset(dec_, lane, 1.f);
    sw_[lane] = 0.f;  // DREAD sweep position
    started_[lane] = true;
    ef_[lane] = 1.f;  // DONK snap
    es_[lane] = 0.f;  // DONK ring position
    jump_[lane] = true;  // pitch jumps to the new note instead of ramping
  }

  // Render len (<= kMaxBlock) samples of all lanes into out
  void render(V *out, int len) {
    if (len <= 0) return;
    if (len > kMaxBlock) len = kMaxBlock;
    const float invLen = 1.f / (float)len;
    const float fP = exp2f_fast(lP_ * (float)len);  // shape EG over this block
    const float fK = exp2f_fast(lK_ * (float)len);  // knock EG over this block

    // ---- control rate, per lane ----
    V wEnd, idtEnd, tim, drv, chr, subl, idxEnd, wkEnd, pwEnd;
    V fa1, fa2, fa3, da1, da2, da3, fb1, fb2, fb3, db1, db2, db3, fgain, cAv, crR, crQ, crIq;
    fa1 = fa2 = fa3 = da1 = da2 = da3 = fb1 = fb2 = fb3 = db1 = db2 = db3 = splat<V>(0.f);
    fgain = splat<V>(1.f);
    float fc0[N], fc1[N], kq[N], kq0[N];
    const float egSamples = maxf(egTimeMs_ * 0.001f * kSR, 1.f);
    const float swRate = 1.f / egSamples;
    // DONK: ring length = EG TIME; the snap (6 ms + 4 % of EG TIME)
    // DONK: filter snap (10 ms + 4 % of EG TIME), ring fades over EG TIME
    // (scaled so the default EG TIME of 300 ms gives the reference's ~220 ms)
    const float donkEg = 0.73f * egSamples;
    const float duRing = (float)len / donkEg;
    const float fF = exp2f_fast(-1.44269504f * (float)len / (0.010f * kSR + 0.04f * donkEg));
    float hRatio2 = 1.f, hRatio1 = 1.f;
    if (mode_ == Params::kHoover) {
      const float cents = 4.f + 26.f * clampf(p_char_, 0.f, 1.f);
      hRatio1 = exp2f_fast(-cents * (1.f / 2400.f));
      hRatio2 = exp2f_fast(cents * (1.f / 2400.f)) / hRatio1;  // osc 2 relative to osc 1
    }
    float anyKnock = 0.f;
    const float hMax = maxf(hRatio2, 1.f);  // hoover osc 2 runs faster: its BLEP width
    for (int j = 0; j < N; ++j) {
      const float pe0 = lget(penv_, j);
      const float pe = pe0 * fP;
      lset(penv_, j, pe);
      const float note = note_[j] + semi_ + fine_ + mod_pitch_[j];
      const float w0 = kW0A4 * exp2f_fast((note - 69.f) * (1.f / 12.f));
      float w = w0 * exp2f_fast(egInt_ * pe * (1.f / 12.f)) * hRatio1;
      w = clampf(w, 1e-5f, 0.24f);
      lset(wEnd, j, w);
      lset(idtEnd, j, 1.f / (w * hMax));
      if (jump_[j]) {
        // new note: start this block at the top of the shape EG (the GROWL
        // index ramps up over the block instead, so legato hits don't click)
        const float ws = clampf(w0 * exp2f_fast(egInt_ * pe0 * (1.f / 12.f)) * hRatio1, 1e-5f, 0.24f);
        lset(w_, j, ws);
        lset(idt_, j, 1.f / (ws * hMax));
        lset(wk_, j, clampf(w0 * 6.f, 350.f * kInvSR, 2400.f * kInvSR) * (1.f + 1.5f * lget(kenv_, j)));
        jump_[j] = false;
      }

      const float tm = clampf(p_timbre_ + mod_timbre_[j], 0.f, 1.f);
      const float dv = clampf(p_drive_ + mod_drive_[j], 0.f, 1.f);
      const float ch = clampf(p_char_ + mod_char_[j], 0.f, 1.f);
      lset(tim, j, tm);
      lset(drv, j, dv);
      lset(chr, j, ch);
      lset(subl, j, kSubAmp * clampf(p_sub_ + mod_sub_[j], 0.f, 1.f));

      // GROWL: FM index in cycles, lifted by the shape EG
      lset(idxEnd, j, 1.4f * tm * tm * (1.f + pe));

      // KNOCK: a blip about 2.5 octaves over the note, sweeping down
      const float ke = lget(kenv_, j) * fK;
      anyKnock = maxf(anyKnock, lget(kenv_, j));
      const float wk = clampf(w0 * 6.f, 350.f * kInvSR, 2400.f * kInvSR) * (1.f + 1.5f * ke);
      lset(wkEnd, j, wk);

      // pulse width: DONK = TIMBRE, HOOVER = slow PWM scaled by TIMBRE
      float pw;
      if (mode_ == Params::kHoover) {
        lfo_[j] += 0.55f * (float)len * kInvSR;
        lfo_[j] -= (float)(int32_t)lfo_[j];
        pw = 0.5f - 0.42f * tm * (0.5f + 0.5f * sin2pi<float>(lfo_[j]));
      } else {
        pw = 0.5f - 0.42f * tm;
      }
      lset(pwEnd, j, pw);

      // DREAD / DONK low-pass: cutoff (as log2 multiple of the note) at the
      // start and end of this block
      if (mode_ == Params::kDread) {
        // rises at a steady rate in octaves over EG TIME, then holds
        const float u0 = sw_[j];
        float u1 = u0 + swRate * (float)len;
        if (u1 > 1.f) u1 = 1.f;
        sw_[j] = u1;
        const float lo = 1.0f, hi = 1.8f + 3.5f * tm;
        fc0[j] = w0 * exp2f_fast(lo + (hi - lo) * u0);
        fc1[j] = w0 * exp2f_fast(lo + (hi - lo) * u1);
        lset(cAv, j, (u1 < 1.f || !started_[j]) ? 1.f : cA_);  // PLUCK starts once the sweep peaks
        kq[j] = kq0[j] = 1.f / (0.7f + 12.f * ch * ch);
      } else if (mode_ == Params::kDonk) {
        // resonant low-pass on a square, as measured on a donk loop: opens
        // ~9x the note, snaps down onto the ring harmonic (5th at TIMBRE
        // 700) within ~30 ms and rings there, then closes below it so the
        // ring fades. The resonance only ever lands on harmonics of the
        // note, so it reads as a "donk", not a whistle.
        const float lt = 0.95f + 2.f * tm;  // log2 of the ring harmonic: ~2nd..8th
        const float f0e = ef_[j], u0 = es_[j];
        const float f1e = f0e * fF;
        float u1 = u0 + duRing;
        if (u1 > 1.f) u1 = 1.f;
        ef_[j] = f1e;
        es_[j] = u1;
        const float k0 = relu<float>(u0 - kDonkHold) * (1.f / (1.f - kDonkHold));
        const float k1 = relu<float>(u1 - kDonkHold) * (1.f / (1.f - kDonkHold));
        fc0[j] = w0 * exp2f_fast(lt + kDonkSnapDepth * f0e - kDonkSink * k0);
        fc1[j] = w0 * exp2f_fast(lt + kDonkSnapDepth * f1e - kDonkSink * k1);
        // the resonance itself dies away after the hold ((1 - k)^4 fade)
        // (the sweet spot measured off the reference, Q ~25, sits at the
        // default CHARACTER of 55 %)
        const float qp = 85.f * ch * ch;
        const float r0 = (1.f - k0) * (1.f - k0), r1 = (1.f - k1) * (1.f - k1);
        kq0[j] = 1.f / (0.7f + qp * r0 * r0);
        kq[j] = 1.f / (0.7f + qp * r1 * r1);
        lset(cAv, j, started_[j] ? cA_ : 1.f);
      } else {
        lset(cAv, j, started_[j] ? cA_ : 1.f);
      }
      if (mode_ == Params::kDread || mode_ == Params::kDonk) {
        // stage 1: resonant SVF, stage 2: Butterworth SVF at the same cutoff
        float b1, b2, b3, e1, e2, e3;
        svfCoefs(fc0[j], kq0[j], b1, b2, b3);
        svfCoefs(fc1[j], kq[j], e1, e2, e3);
        lset(fa1, j, b1); lset(fa2, j, b2); lset(fa3, j, b3);
        lset(da1, j, (e1 - b1) * invLen); lset(da2, j, (e2 - b2) * invLen); lset(da3, j, (e3 - b3) * invLen);
        svfCoefs(fc0[j], 1.4142136f, b1, b2, b3);
        svfCoefs(fc1[j], 1.4142136f, e1, e2, e3);
        lset(fb1, j, b1); lset(fb2, j, b2); lset(fb3, j, b3);
        lset(db1, j, (e1 - b1) * invLen); lset(db2, j, (e2 - b2) * invLen); lset(db3, j, (e3 - b3) * invLen);
        // resonance takes some level out of the fundamental: put some back
        // (DONK: none, its resonance changes over the note and the body
        // has to stay put)
        lset(fgain, j, mode_ == Params::kDonk ? 1.f : 1.f + 0.35f * (1.f / kq[j] - 0.7f) * 0.1f);
        // DREAD crunch: hold rate 48 kHz -> ~10 kHz, 12 -> 6 bits with DRIVE
        lset(crR, j, exp2f_fast(-2.3f * dv));
        const float q = exp2f_fast(12.f - 6.f * dv);
        lset(crQ, j, q);
        lset(crIq, j, 1.f / q);
      }
    }
    const bool knockOn = p_knock_ > 0.f && anyKnock > 1e-4f;

    if (first_) {  // nothing to ramp from yet
      w_ = wEnd;
      idt_ = idtEnd;
      tim_ = tim;
      idx_ = idxEnd;
      wk_ = wkEnd;
      pw_ = pwEnd;
      drv_ = drv;
      sub_ = subl;
      first_ = false;
    }

    // ---- pass 1: phases ----
    {
      V t = t_, ts = ts_, w = w_;
      const V winc = (wEnd - w) * invLen;
      const float subMul = subOct_ ? (0.25f / hRatio1) : (0.5f / hRatio1);
      for (int i = 0; i < len; ++i) {
        w += winc;
        t = fracp(t + w);
        ts = fracp(ts + w * subMul);
        T_[i] = t;
        S_[i] = ts;
        W_[i] = w;
      }
      t_ = t;
      ts_ = ts;
      w_ = wEnd;
      if (mode_ == Params::kHoover) {
        V t2 = t2_;
        for (int i = 0; i < len; ++i) {
          t2 = fracp(t2 + W_[i] * hRatio2);
          T2_[i] = t2;
        }
        t2_ = t2;
      }
    }

    // ---- pass 2: the mode's core waveform -> C_ ----
    switch (mode_) {
      case Params::kDread:
      case Params::kDonk: {
        // band-limited square
        V idt = idt_;
        const V iinc = (idtEnd - idt) * invLen;
        for (int i = 0; i < len; ++i) {
          idt += iinc;
          const V t = T_[i];
          C_[i] = 0.8f * (blepSaw(t, idt) - blepSaw(fracp(t + 0.5f), idt));
        }
        // stage 1: resonant TPT SVF low-pass. The state is soft-limited:
        // DREAD squelches at +-10, DONK gets room for its much higher Q
        V s1 = s1_, s2 = s2_, c1 = fa1, c2 = fa2, c3 = fa3;
        const float lim = (mode_ == Params::kDonk) ? 80.f : 10.f;
        const float la = 1.3333333f / lim, lb = 0.6666667f / lim;
        for (int i = 0; i < len; ++i) {
          c1 += da1;
          c2 += da2;
          c3 += da3;
          const V v3 = C_[i] - s2;
          const V v1 = c1 * s1 + c2 * v3;
          const V v2 = s2 + c2 * s1 + c3 * v3;
          s1 = lim * sat(la * v1 - lb * s1);  // unity slope, limits at +-lim
          s2 = 2.f * v2 - s2;
          C_[i] = v2;
        }
        // DONK: saturate between the stages (as in the reference): the ring
        // and the body come out at about the same peak level, and stage 2
        // rounds the clipped tail back into a dark square
        if (mode_ == Params::kDonk)
          for (int i = 0; i < len; ++i) C_[i] = kDonkSatOut * sat(kDonkSatIn * C_[i]);
        s1_ = s1;
        s2_ = s2;
        // stage 2: Butterworth SVF at the same cutoff (24 dB / oct in total)
        V s3 = s3_, s4 = s4_;
        c1 = fb1;
        c2 = fb2;
        c3 = fb3;
        for (int i = 0; i < len; ++i) {
          c1 += db1;
          c2 += db2;
          c3 += db3;
          const V v3 = C_[i] - s4;
          const V v1 = c1 * s3 + c2 * v3;
          const V v2 = s4 + c2 * s3 + c3 * v3;
          s3 = 2.f * v1 - s3;
          s4 = 2.f * v2 - s4;
          C_[i] = fgain * v2;
        }
        s3_ = s3;
        s4_ = s4;
        break;
      }
      case Params::kGrowl: {
        V idx = idx_, mp = mprev_;
        const V iinc = (idxEnd - idx) * invLen;
        const V fb = 0.17f * chr;  // above ~0.2 cycles the edges get sharp enough to alias
        for (int i = 0; i < len; ++i) {
          idx += iinc;
          const V t = T_[i];
          const V m = sin2pi(fracp(t + fb * mp + 2.f));
          mp = 0.5f * (m + mp);  // two-sample average keeps feedback tame
          C_[i] = sin2pi(fracp(t + idx * m + 4.f));
        }
        mprev_ = mp;
        break;
      }
      case Params::kHoover:
      default: {
        V idt = idt_, pw = pw_;
        const V iinc = (idtEnd - idt) * invLen;
        const V pinc = (pwEnd - pw) * invLen;
        for (int i = 0; i < len; ++i) {
          idt += iinc;
          pw += pinc;
          const V ta = T_[i], tb = T2_[i];
          const V p1 = blepSaw(ta, idt) - blepSaw(fracp(ta + pw), idt);
          const V p2 = blepSaw(tb, idt) - blepSaw(fracp(tb + pw), idt);
          C_[i] = 0.62f * (p1 + p2);
        }
        break;
      }
    }
    idt_ = idtEnd;
    pw_ = pwEnd;
    idx_ = idxEnd;
    tim_ = tim;

    // ---- pass 3: knock (skipped while silent) ----
    if (knockOn) {
      V tk = tk_, wk = wk_, ke = kenv_;
      const V kinc = (wkEnd - wk) * invLen;
      const float amt = 0.8f * p_knock_;
      const float cK = cK_;
      for (int i = 0; i < len; ++i) {
        wk += kinc;
        tk = fracp(tk + wk);
        C_[i] += amt * ke * sin2pi(tk);
        ke *= cK;
      }
      tk_ = tk;
      kenv_ = ke;
    } else {
      kenv_ *= fK;
    }
    wk_ = wkEnd;

    // ---- pass 4: drive (gain-compensated soft clip) + DC blocker ----
    {
      V b, sb, invn, gEnd, wetEnd;
      for (int j = 0; j < N; ++j) {
        const float bj = 0.f;
        const float d = lget(drv, j);
        const float g = 1.f + 11.f * d * d;
        const float sbj = satf(bj);
        const float n = maxf(satf(g + bj) - sbj, sbj - satf(bj - g));
        lset(b, j, bj);
        lset(sb, j, sbj);
        lset(invn, j, 1.f / n);
        lset(gEnd, j, g);
        lset(wetEnd, j, clampf(3.f * d + 2.f * bj, 0.f, 1.f));
      }
      // ramp drive from the previous block's value
      V gz, wz;
      for (int j = 0; j < N; ++j) {
        const float d = lget(drv_, j);
        lset(gz, j, 1.f + 11.f * d * d);
        const float bj = lget(b, j);
        lset(wz, j, clampf(3.f * d + 2.f * bj, 0.f, 1.f));
      }
      const V ginc = (gEnd - gz) * invLen;
      const V winc = (wetEnd - wz) * invLen;
      // The clipper runs at 2x rate: the in-between sample is linearly
      // interpolated, both are shaped, and a [1/4 1/2 1/4] filter brings it
      // back down. Cheap, and keeps heavy drive clean higher up the keyboard.
      V g = gz, wet = wz, xp = osx_, yp = osy_;
      for (int i = 0; i < len; ++i) {
        g += ginc;
        wet += winc;
        const V x = C_[i];
        const V xm = 0.5f * (x + xp);
        const V ym = xm + wet * ((sat(g * xm + b) - sb) * invn - xm);
        const V yx = x + wet * ((sat(g * x + b) - sb) * invn - x);
        C_[i] = 0.25f * (yp + yx) + 0.5f * ym;
        xp = x;
        yp = yx;
      }
      osx_ = xp;
      osy_ = yp;
      V x1 = dcx_, y1 = dcy_;
      for (int i = 0; i < len; ++i) {
        const V y = C_[i];
        const V o = y - x1 + kDcCoef * y1;
        x1 = y;
        y1 = o;
        C_[i] = o;
      }
      dcx_ = x1;
      dcy_ = y1;
      drv_ = drv;
    }

    // ---- pass 4b: DREAD crunch (sample-and-hold + bit reduction, no compares) ----
    if (mode_ == Params::kDread) {
      V h = hold_, acc = hacc_;
      for (int i = 0; i < len; ++i) {
        acc += crR;
        const V m = truncv(acc);  // 1 when a new sample is due, else 0
        h += m * (C_[i] - h);
        acc -= m;
        C_[i] = (truncv(h * crQ + 16384.5f) - 16384.f) * crIq;
      }
      hold_ = h;
      hacc_ = acc;
    }

    // ---- pass 5: sub, pluck, level, ceiling ----
    {
      float mg;
      switch (mode_) {
        case Params::kDread: mg = ModeGain::kDread; break;
        case Params::kDonk: mg = ModeGain::kDonk; break;
        case Params::kGrowl: mg = ModeGain::kGrowl; break;
        default: mg = ModeGain::kHoover; break;
      }
      V sl = sub_;
      const V sinc = (subl - sl) * invLen;
      V dec = dec_, att = att_;
      for (int i = 0; i < len; ++i) {
        sl += sinc;
        att += kAttackCoef * (1.f - att);
        const V v = (mg * C_[i] + sl * sin2pi(S_[i])) * (dec * att);
        dec *= cAv;
        out[i] = clamp1(limit(kOutLevel * v));  // clamp: float rounding in the knee
      }
      sub_ = subl;
      dec_ = dec;
      att_ = att;
    }
  }

 private:
  void updateEgCoefs() {
    // shape EG: about 95 % of the way down after EG TIME
    const float tau = maxf(egTimeMs_ * 0.001f / 3.f, 1e-4f);
    lP_ = -1.44269504f / (tau * kSR);
    lK_ = -1.44269504f / (kKnockTau * kSR);
    cK_ = exp2f_fast(lK_);
    // PLUCK: 0 = sustain, else time constant 0.9 s .. ~23 ms
    if (p_pluck_ <= 0.f) {
      cA_ = 1.f;
    } else {
      const float tA = 0.9f * exp2f_fast(-5.3f * p_pluck_);
      cA_ = exp2f_fast(-1.44269504f / (tA * kSR));
    }
  }

  // parameters
  float p_timbre_, p_drive_, p_char_, p_sub_, p_knock_, p_pluck_;
  float egInt_, egTimeMs_, semi_, fine_;
  int mode_, subOct_, retrig_;
  float lP_, lK_, cK_, cA_;

  // per lane, control rate
  float note_[N];
  float mod_timbre_[N], mod_drive_[N], mod_char_[N], mod_sub_[N], mod_pitch_[N];
  float lfo_[N];
  float sw_[N];          // DREAD sweep position 0..1
  float ef_[N], es_[N];  // DONK filter snap envelope / close position 0..1
  bool started_[N];      // a note start has been seen (PLUCK only decays after one)
  bool jump_[N] = {};
  bool first_;

  // per lane, audio state
  V t_, t2_, ts_, tk_, mprev_, s1_, s2_, s3_, s4_, dcx_, dcy_, osx_, osy_, hold_, hacc_;
  V penv_, kenv_, dec_, att_;
  // ramp start values (end of the previous block)
  V w_, idt_, tim_, idx_, wk_, pw_, drv_, sub_;

  // scratch buffers for the passes
  V T_[kMaxBlock], T2_[kMaxBlock], S_[kMaxBlock], W_[kMaxBlock], C_[kMaxBlock];
};

typedef Engine<float> DreadBass1;
#ifndef DB_NO_V4
typedef Engine<v4f> DreadBass4;
#endif

}  // namespace dread
