// Ported into HELIX from TwistedGenetics' own logue SDK unit (see README.md here); sample rate made settable.
#pragma once
/*
 * Rollers - jungle / drum & bass bass oscillator
 * for the Korg NTS-1 mkII and microKORG 2 (logue SDK)
 *
 * Signal flow:
 *
 *   note-on ──> pitch-drop envelope (whole oscillator) ──┐
 *                                                        v
 *   SUB   : sine / triangle / folded sine  ──────────────────────(clean)──┐
 *   UPPER : 2-4 detuned PolyBLEP saws with drift (REESE)                  │
 *           or PWM saw stack + attack pitch dip (HOOVER)                  ├─> blend ─> soft limit
 *           -> pitch-tracked high-pass (keeps the fundamental for the sub)│
 *           -> DRIVE (tanh) -> CRUNCH (sample-rate + bit reduction) ──────┘
 *
 * The key rule: drive and crunch only ever touch the upper band. The sub
 * stays clean so the low end holds up on a big system.
 *
 * Shared by the NTS-1 mkII unit (one mono voice) and the microKORG 2 unit
 * (one Rollers instance per voice). Self-contained (no SDK headers, no
 * tables), so it also builds on a desktop for testing (see test/render.cc).
 */

#include <stdint.h>

// Anonymous namespace: keeps DSP symbols internal so the unit only exports
// the logue SDK callbacks (same as Korg's examples).
namespace {

namespace rmath {

// HELIX: per thread, set to the rate of the engine being rendered (were constexpr 48 kHz)
inline thread_local float kSR = 48000.f;
inline thread_local float kInvSR = 1.f / 48000.f;
inline void setSampleRate(float sr) { kSR = sr; kInvSR = 1.f / sr; }

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float absf(float x) { return x < 0.f ? -x : x; }

inline float wrap01(float x) {
  x -= (float)(int32_t)x;
  if (x < 0.f) x += 1.f;
  return x;
}

// 2^x, ~2e-6 relative error (good enough for tuning; the SDK's fasterpow2f
// is ~3% out, which is half a semitone).
inline float pow2(float x) {
  if (x < -120.f) return 0.f;
  if (x > 120.f) x = 120.f;
  int32_t xi = (int32_t)x;
  if (x < (float)xi) --xi;
  const float g = x - (float)xi - 0.5f;  // -0.5 .. 0.5
  const float l = 0.6931472f * g;
  const float p = 1.f + l * (1.f + l * (0.5f + l * (0.16666667f + l * (0.041666668f + l * (0.008333334f + l * 0.0013888889f)))));
  union { float f; int32_t i; } s;
  s.i = (xi + 127) << 23;
  return p * s.f * 1.4142135f;
}

// sin(2*pi*x), x in cycles (any range). Error < 4e-6.
inline float sin2pi(float x) {
  x = wrap01(x);
  float sign = 1.f;
  if (x > 0.5f) {
    x -= 0.5f;
    sign = -1.f;
  }
  if (x > 0.25f) x = 0.5f - x;
  const float u = 6.2831853f * x;
  const float u2 = u * u;
  const float s = u * (1.f + u2 * (-0.16666667f + u2 * (0.008333334f + u2 * (-1.984127e-4f + u2 * 2.7557319e-6f))));
  return sign * s;
}

// Rational tanh approximation, hard-limited at +-1
inline float softclip(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Output limiter: transparent below 0.8, soft knee above, never exceeds 1
inline float limit(float x) {
  const float a = absf(x);
  if (a <= 0.8f) return x;
  const float s = 0.8f + 0.2f * softclip((a - 0.8f) * 5.f);
  return x < 0.f ? -s : s;
}

// PolyBLEP residual for a saw discontinuity (t: phase 0..1, dt: increment)
inline float blep(float t, float dt) {
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

// PolyBLAMP residual for a slope discontinuity
inline float blamp(float t, float dt) {
  if (t < dt) {
    t = t / dt - 1.f;
    return -(1.f / 3.f) * t * t * t;
  }
  if (t > 1.f - dt) {
    t = (t - 1.f) / dt + 1.f;
    return (1.f / 3.f) * t * t * t;
  }
  return 0.f;
}

// Band-limited saw, -1..1
inline float saw(float t, float dt) { return 2.f * t - 1.f - blep(t, dt); }

// Band-limited triangle, -1..1 (corners at t = 0 and t = 0.5)
inline float tri(float t, float dt) {
  float y = 1.f - 4.f * absf(t - 0.5f);
  y += 4.f * dt * (-blamp(t, dt) + blamp(wrap01(t + 0.5f), dt));
  return y;
}

inline float flushDenorm(float x) { return absf(x) < 1e-20f ? 0.f : x; }

}  // namespace rmath

class Rollers {
 public:
  enum Param {
    kBlend = 0,  // Knob A
    kDetune,     // Knob B
    kSubShape,   // Edit 1
    kDrop,       // Edit 2
    kDropTime,   // Edit 3
    kVoices,     // Edit 4
    kDrift,      // Edit 5
    kDrive,      // Edit 6
    kCrunch,     // Edit 7
    kMode,       // Edit 8
    kNumParams
  };

  enum Mode { kModeSub = 0, kModeReese, kModeSubReese, kModeHoover, kNumModes };
  enum SubShape { kSine = 0, kTriangle, kFolded, kNumShapes };

  static constexpr int kMaxVoices = 4;
  static constexpr int kMinVoices = 2;

  // switch rather than pointer tables: no data relocations in the unit
  static const char *modeName(int i) {
    switch (i) {
      case kModeSub: return "SUB";
      case kModeReese: return "REESE";
      case kModeSubReese: return "SUB+RS";
      default: return "HOOVER";
    }
  }
  static const char *shapeName(int i) {
    switch (i) {
      case kSine: return "SINE";
      case kTriangle: return "TRI";
      default: return "FOLD";
    }
  }

  void init(uint32_t seed = 0x1234567u) {
    rng_ = seed ? seed : 0x1234567u;
    p_[kBlend] = 0.55f;
    p_[kDetune] = 0.4f;
    p_[kDrop] = 0.2f;
    p_[kDropTime] = 0.3f;
    p_[kDrift] = 0.3f;
    p_[kDrive] = 0.25f;
    p_[kCrunch] = 0.f;
    p_[kSubShape] = p_[kVoices] = p_[kMode] = 0.f;  // unused (ints below)
    shape_ = kSine;
    voices_ = want_voices_ = 3;
    mode_ = want_mode_ = kModeSubReese;
    fade_in_ = false;
    w0_ = 55.f * rmath::kInvSR;
    lfo_ = 0.f;
    mod_blend_ = mod_detune_ = mod_drive_ = mod_crunch_ = 0.f;
    first_block_ = true;
    // Free-running Reese phases start spread out so the first note doesn't
    // begin with all saws in phase (a big, comb-filtered transient).
    for (int i = 0; i < kMaxVoices; ++i) {
      ph_[i] = rand01();
      pw_ph_[i] = rand01();
      drift_t_[i] = rand01();
      drift_a_[i] = rand11();
      drift_b_[i] = rand11();
      ratio_z_[i] = 1.f;
    }
    reset();
  }

  void reset() {
    sub_ph_ = 0.f;
    old_ph_ = xfade_ = 0.f;
    drop_env_ = 0.f;
    hoov_env_ = 0.f;
    hp_ic1_ = hp_ic2_ = 0.f;
    dc_x1_ = dc_y1_ = 0.f;
    lp_z_ = 0.f;
    hold_acc_ = 1.f;
    hold_val_ = 0.f;
  }

  // Note-on: fire the pitch drop and hoover dip, and restart the sub at zero
  // phase so every note has the same punch. If the previous note is still
  // sounding (legato), the old sub keeps running and is crossfaded out over
  // 8 ms so the restart doesn't click.
  void noteOn() {
    old_ph_ = sub_ph_;
    xfade_ = 1.f;
    sub_ph_ = 0.f;
    drop_env_ = 1.f;
    hoov_env_ = 1.f;
  }

  // w0: fundamental in cycles per sample
  void setPitch(float w0) { w0_ = w0; }
  // Blend modulation, -1..1 (NTS-1 shape LFO)
  void setShapeLfo(float lfo) { lfo_ = lfo; }
  // microKORG 2 virtual patch offsets, roughly -1..1
  void setMod(float blend, float detune, float drive, float crunch) {
    mod_blend_ = blend;
    mod_detune_ = detune;
    mod_drive_ = drive;
    mod_crunch_ = crunch;
  }

  void setParam(int id, int32_t value) {
    switch (id) {
      case kBlend:
      case kDetune:
        p_[id] = value * (1.f / 1023.f);
        break;
      case kSubShape:
        shape_ = rmath::clampi(value, 0, kNumShapes - 1);
        break;
      // MODE and VOICES switch whole signal paths on and off, so the change
      // is deferred: process() fades out, switches, then fades back in.
      case kVoices:
        want_voices_ = rmath::clampi(value, kMinVoices, kMaxVoices);
        break;
      case kMode:
        want_mode_ = rmath::clampi(value, 0, kNumModes - 1);
        break;
      default:
        if (id > kSubShape && id < kNumParams) p_[id] = value * 0.01f;
        break;
    }
  }

  // Writes frames samples to out[0], out[stride], ... scaled by gain.
  void process(float *out, uint32_t frames, uint32_t stride = 1, float gain = 1.f) {
    using namespace rmath;
    if (frames == 0) return;
    const float inv_frames = 1.f / (float)frames;
    if (first_block_) {
      mode_ = want_mode_;
      voices_ = want_voices_;
    }
    // Pending MODE / VOICES change: fade this block out, switch afterwards
    const bool switching = want_mode_ != mode_ || want_voices_ != voices_;
    float fade = 1.f, d_fade = 0.f;
    if (switching) {
      d_fade = -inv_frames;
    } else if (fade_in_) {
      fade = 0.f;
      d_fade = inv_frames;
      fade_in_ = false;
    }
    const int mode = mode_;
    const int nv = voices_;
    const bool has_sub = mode != kModeReese;
    const bool has_upper = mode != kModeSub;
    const bool hoover = mode == kModeHoover;
    const float w0 = clampf(w0_, 0.f, 0.45f);

    // ---- Envelopes: pitch drop (whole osc) and hoover attack dip ----
    // Drop time 0..100% -> time constant 5 ms .. ~800 ms
    const float drop_oct = p_[kDrop] * 2.f;  // up to 2 octaves above the note
    const float tau = 0.005f * pow2(p_[kDropTime] * 7.3f);
    const float drop_k = pow2(-1.442695f * kInvSR / tau);  // exp(-1/(tau*sr))
    const float hoov_k = 0.99970241f;                      // exp(-1/(0.07*sr)): 70 ms
    const float hoov_semi = -7.f;                          // starts a fifth below

    // ---- Reese detune + drift (per voice, per block) ----
    const float d = clampf(p_[kDetune] + mod_detune_, 0.f, 1.f);
    const float det_cents = 0.5f + 45.f * d * d;
    const float drift = p_[kDrift];
    float drift_hz = 0.f;
    if (drift > 0.f) drift_hz = 0.05f * pow2(drift * 6.3f);  // 0.05 .. ~4 Hz
    const float drift_cents = 2.f + 0.5f * det_cents;
    // fixed-size arrays copied with explicit stores: a variable-length copy
    // loop compiles to a memcpy call, which the unit shouldn't depend on
    float ratio_t[kMaxVoices] = {1.f, 1.f, 1.f, 1.f};
    for (int v = 0; v < nv; ++v) {
      float c = det_cents * spread(nv, v);
      if (drift_hz > 0.f) {
        // each voice wanders at its own slightly different speed
        drift_t_[v] += drift_hz * (0.8f + 0.13f * v) * frames * kInvSR;
        if (drift_t_[v] >= 1.f) {
          drift_t_[v] -= (float)(int32_t)drift_t_[v];
          drift_a_[v] = drift_b_[v];
          drift_b_[v] = rand11();
        }
        const float t = drift_t_[v];
        const float s = t * t * (3.f - 2.f * t);  // smoothstep between random targets
        c += drift_cents * (drift_a_[v] + s * (drift_b_[v] - drift_a_[v]));
      }
      ratio_t[v] = pow2(c * (1.f / 1200.f));
    }
    if (first_block_) copy4(ratio_z_, ratio_t);
    float ratio[kMaxVoices], d_ratio[kMaxVoices];
    copy4(ratio, ratio_z_);
    d_ratio[0] = (ratio_t[0] - ratio[0]) * inv_frames;
    d_ratio[1] = (ratio_t[1] - ratio[1]) * inv_frames;
    d_ratio[2] = (ratio_t[2] - ratio[2]) * inv_frames;
    d_ratio[3] = (ratio_t[3] - ratio[3]) * inv_frames;

    // Hoover PWM LFOs (one per voice, follows the drift rate). Slow, so the
    // pulse widths are updated once per block.
    const float pwm_hz = 0.25f + drift_hz * 0.5f;
    float pw[kMaxVoices] = {0.5f, 0.5f, 0.5f, 0.5f};
    if (hoover) {
      for (int v = 0; v < nv; ++v) {
        pw[v] = 0.5f + 0.38f * sin2pi(pw_ph_[v]);
        pw_ph_[v] += pwm_hz * (1.f + 0.31f * v) * frames * kInvSR;
        pw_ph_[v] -= (float)(int32_t)pw_ph_[v];
      }
    }

    // Upper-band gain by voice count (keeps the level steady when it changes)
    const float upper_norm = hoover ? (nv == 2 ? 0.42f : (nv == 3 ? 0.34f : 0.29f))
                                    : (nv == 2 ? 0.62f : (nv == 3 ? 0.52f : 0.45f));

    // ---- Pitch-tracked split: high-pass the upper band just above the note
    // so the sub owns the fundamental (SUB+REESE and HOOVER only).
    const bool split = has_sub && has_upper;
    float hp_a1 = 0.f, hp_a2 = 0.f, hp_a3 = 0.f;
    const float hp_k = 1.4142135f;
    if (split) {
      // follow the pitch drop too, so the sub keeps the fundamental mid-glide
      const float wd = drop_env_ > 1e-4f ? w0 * pow2(drop_oct * drop_env_) : w0;
      const float fc = clampf(wd * 1.4f, 20.f * kInvSR, 1000.f * kInvSR);
      const float x = 3.1415927f * fc;
      const float g = x * (1.f + x * x * (0.33333334f + x * x * 0.13333334f));  // tan(x)
      hp_a1 = 1.f / (1.f + g * (g + hp_k));
      hp_a2 = g * hp_a1;
      hp_a3 = g * hp_a2;
    }

    // ---- Block-ramped controls ----
    float blend_t = clampf(p_[kBlend] + lfo_ * 0.5f + mod_blend_, 0.f, 1.f);
    const float drive_t = clampf(p_[kDrive] + mod_drive_, 0.f, 1.f);
    const float crunch_t = clampf(p_[kCrunch] + mod_crunch_, 0.f, 1.f);
    // REESE mode: Knob A is a tone control (one-pole low-pass, 150 Hz .. open)
    float tone_t = 1.f;
    if (mode == kModeReese) {
      const float fc = 150.f * pow2(blend_t * 7.f);
      tone_t = clampf(1.f - pow2(-9.0647f * fc * kInvSR), 0.f, 1.f);  // 1 - exp(-2*pi*fc/sr)
    }
    if (first_block_) {
      blend_z_ = blend_t;
      drive_z_ = drive_t;
      crunch_z_ = crunch_t;
      tone_z_ = tone_t;
      first_block_ = false;
    }
    float blend = blend_z_, drv = drive_z_, crn = crunch_z_, tone = tone_z_;
    const float d_blend = (blend_t - blend) * inv_frames;
    const float d_drv = (drive_t - drv) * inv_frames;
    const float d_crn = (crunch_t - crn) * inv_frames;
    const float d_tone = (tone_t - tone) * inv_frames;

    float sub_ph = sub_ph_;
    float old_ph = old_ph_, xfade = xfade_;
    float drop_env = drop_env_, hoov_env = hoov_env_;
    float ic1 = hp_ic1_, ic2 = hp_ic2_;
    float dcx = dc_x1_, dcy = dc_y1_, lpz = lp_z_;
    float hacc = hold_acc_, hval = hold_val_;
    float ph[kMaxVoices];
    copy4(ph, ph_);

    // Per-block drive and crunch settings (the wet amounts still ramp per sample)
    const float drv_g = 1.f + drive_t * 9.f;
    const float drv_norm = 1.f / softclip(drv_g);
    const float drv_comp = 1.f / (1.f + 0.6f * drive_t);  // saturation raises RMS: pull it back
    const float crn_rate = pow2(-crunch_t * 3.6f);        // 48 kHz .. ~4 kHz
    const float crn_q = pow2(15.f - crunch_t * 11.f);     // 16 bits .. 5 bits
    const float crn_inv_q = 1.f / crn_q;

    for (uint32_t i = 0; i < frames; ++i) {
      blend += d_blend;
      drv += d_drv;
      crn += d_crn;
      tone += d_tone;

      // pitch drop: multiplier 2^(oct * env), env decays to 0
      float pm = 1.f;
      if (drop_env > 1e-4f) {
        if (drop_oct > 0.f) pm = pow2(drop_oct * drop_env);
        drop_env *= drop_k;
      } else {
        drop_env = 0.f;
      }
      const float w = clampf(w0 * pm, 0.f, 0.45f);

      // ---------------- SUB ----------------
      float sub = 0.f;
      if (has_sub) {
        sub = subWave(sub_ph, w);
        if (xfade > 0.f) {
          // legato restart: fade the still-running previous cycle out
          sub += xfade * (subWave(old_ph, w) - sub);
          old_ph += w;
          if (old_ph >= 1.f) old_ph -= 1.f;
          xfade -= 1.f / 384.f;  // 8 ms
        }
        if (mode == kModeSub && blend > 0.f) {
          // SUB mode: Knob A adds a clean 2nd harmonic ("body")
          sub = (sub + 0.5f * blend * sin2pi(2.f * sub_ph)) * (1.f / (1.f + 0.35f * blend));
        }
        sub_ph += w;
        if (sub_ph >= 1.f) sub_ph -= 1.f;
      }

      // ---------------- UPPER ----------------
      float up = 0.f;
      if (has_upper) {
        float wu = w;
        if (hoover) {
          if (hoov_env > 1e-4f) {
            wu = clampf(w * pow2(hoov_semi * (1.f / 12.f) * hoov_env), 0.f, 0.45f);
            hoov_env *= hoov_k;
          } else {
            hoov_env = 0.f;
          }
        }
        for (int v = 0; v < nv; ++v) {
          ratio[v] += d_ratio[v];
          const float dt = clampf(wu * ratio[v], 1e-6f, 0.45f);
          const float t = ph[v];
          float s = saw(t, dt);
          if (hoover) {
            // PWM: saw minus a phase-shifted saw is a pulse; width wanders per voice
            float t2 = t + pw[v];
            if (t2 >= 1.f) t2 -= 1.f;
            const float pulse = s - saw(t2, dt);
            s = s + 0.5f * pulse;
          }
          up += s;
          float tn = t + dt;
          if (tn >= 1.f) tn -= 1.f;
          ph[v] = tn;
        }
        up *= upper_norm;

        if (split) {
          // 2-pole SVF high-pass (Simper), Q = 0.707
          const float v3 = up - ic2;
          const float v1 = hp_a1 * ic1 + hp_a2 * v3;
          const float v2 = ic2 + hp_a2 * ic1 + hp_a3 * v3;
          ic1 = 2.f * v1 - ic1;
          ic2 = 2.f * v2 - ic2;
          up = up - hp_k * v1 - v2;
        } else if (mode == kModeReese) {
          // REESE only: keep the full low end; Knob A is a tone control
          lpz += tone * (up - lpz);
          up = lpz;
        }

        // DRIVE: tanh on the upper band only, gain compensated
        if (drv > 0.f) {
          const float sat = softclip(drv_g * up) * drv_norm;
          const float wet = clampf(drv * 5.f, 0.f, 1.f);
          up += wet * (sat - up);
          up *= 1.f + wet * (drv_comp - 1.f);
        }

        // CRUNCH: sample-and-hold rate reduction + bit reduction (S950-ish).
        // The aliasing here is deliberate.
        if (crn > 0.001f) {
          hacc += crn_rate;
          if (hacc >= 1.f) {
            hacc -= (float)(int32_t)hacc;
            const float x = up * crn_q;
            const int32_t xi = (int32_t)(x + (x >= 0.f ? 0.5f : -0.5f));
            hval = (float)xi * crn_inv_q;
          }
          // fade in the crunch over the first few percent so it never jumps
          const float wet = clampf(crn * 20.f, 0.f, 1.f);
          up += wet * (hval - up);
        } else {
          hacc = 1.f;
          hval = up;
        }

        // DC blocker (~10 Hz): drive on an asymmetric (high-passed) saw
        // stack creates a DC offset, so take it out last.
        const float y = up - dcx + 0.9987f * dcy;
        dcx = up;
        dcy = y;
        up = y;
      }

      // ---------------- MIX ----------------
      float y;
      if (mode == kModeSub) {
        y = sub * 0.85f;
      } else if (mode == kModeReese) {
        y = up * 0.9f;
      } else {
        const float sg = clampf(2.f * (1.f - blend), 0.f, 1.f);
        const float ug = clampf(2.f * blend, 0.f, 1.f);
        y = (0.8f * sub * sg + up * ug) * 0.6f;  // headroom: keep the sum out of the limiter
      }
      fade += d_fade;
      out[i * stride] = limit(y) * fade * gain;
    }

    // ---- store state ----
    sub_ph_ = sub_ph;
    old_ph_ = old_ph;
    xfade_ = xfade > 0.f ? xfade : 0.f;
    drop_env_ = drop_env;
    hoov_env_ = hoov_env;
    hp_ic1_ = flushDenorm(ic1);
    hp_ic2_ = flushDenorm(ic2);
    dc_x1_ = flushDenorm(dcx);
    dc_y1_ = flushDenorm(dcy);
    lp_z_ = flushDenorm(lpz);
    hold_acc_ = hacc;
    hold_val_ = hval;
    copy4(ph_, ph);
    copy4(ratio_z_, ratio_t);
    if (switching) {
      mode_ = want_mode_;
      voices_ = want_voices_;
      hp_ic1_ = hp_ic2_ = dc_x1_ = dc_y1_ = 0.f;
      lp_z_ = 0.f;
      fade_in_ = true;
    }
    blend_z_ = blend_t;
    drive_z_ = drive_t;
    crunch_z_ = crunch_t;
    tone_z_ = tone_t;
  }

 private:
  inline float subWave(float ph, float w) const {
    using namespace rmath;
    switch (shape_) {
      case kTriangle:
        return tri(ph, w);
      case kFolded:
        // light sine fold: adds odd harmonics so the sub reads on small speakers
        return sin2pi(0.25f * 1.25f * sin2pi(ph));
      default:
        return sin2pi(ph);
    }
  }

  static inline void copy4(float *dst, const float *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
  }

  // Symmetric detune positions, -1..1
  static inline float spread(int n, int v) {
    if (n == 2) return v == 0 ? -1.f : 1.f;
    if (n == 3) return (float)(v - 1);
    return -1.f + (float)v * (2.f / 3.f);
  }

  inline uint32_t rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }
  inline float rand01() { return (float)(rnd() >> 8) * (1.f / 16777216.f); }
  inline float rand11() { return rand01() * 2.f - 1.f; }

  float p_[kNumParams];
  int shape_, voices_, mode_, want_voices_, want_mode_;
  bool fade_in_;
  float w0_, lfo_;
  float mod_blend_, mod_detune_, mod_drive_, mod_crunch_;
  bool first_block_;
  uint32_t rng_;

  float sub_ph_;
  float old_ph_, xfade_;
  float ph_[kMaxVoices], pw_ph_[kMaxVoices];
  float drift_t_[kMaxVoices], drift_a_[kMaxVoices], drift_b_[kMaxVoices];
  float ratio_z_[kMaxVoices];
  float drop_env_, hoov_env_;
  float hp_ic1_, hp_ic2_;
  float dc_x1_, dc_y1_, lp_z_;
  float hold_acc_, hold_val_;
  float blend_z_, drive_z_, crunch_z_, tone_z_;
};

}  // namespace
