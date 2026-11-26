/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the sound engine, after the Modal CRAFTsynth 2.0 (its manual, OS 1.4): a monophonic wavetable synth
 * of 8 oscillators (4 on Wave 1, 4 on Wave 2), 16 oscillator modifiers (Wave 1 the carrier, Wave 2 the modulator),
 * SPREAD into unison or chord shapes, a 2-pole state-variable filter morphing low-pass -> band-pass -> high-pass,
 * three ADSR envelopes (filter, amp, mod), two LFOs (LFO 2 up to audio rate, locked to the note), an 8-slot mod
 * matrix (one slot per source), glide with legato, a waveshaping distortion and a delay; and from 0.4 a modifier
 * for OSC B, 12 filter types, and EraCrush, a phaser, a chorus, a reverb (Horizon in small) and OTT.
 * Single-precision float, no libm. The editor runs this same file built to WebAssembly.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_patch.h"
#include "cr_wt.h"
#include "cr_filter.h"
#include "cr_fx.h"

#define CR_CTRL 4u                     /* modulation, envelopes and pitches: every 4 samples (11 kHz: LFO 2 reaches audio rate) */
#define CR_PAIRS 4u                    /* a pair: one Wave 1 and one Wave 2 oscillator (8 in all) */
#define CR_DLY_LEN 32768u              /* the delay's memory, 16-bit mono: 0.74 s at 44.1 kHz (0.4: the reverb has the rest) */
#define CR_SYN_MEM (CR_DLY_LEN + CR_FX_MEM + CR_COMB_LEN)   /* int16 samples the synth is given: 144 KB */
#define CR_NHELD 16u

typedef struct {
    uint8_t st;                        /* 0 off, 1 attack, 2 decay / sustain, 3 release */
    float lv, rel0;
} cr_env_t;

typedef struct {
    float p1, p2, pm;                  /* Wave 1's phase, Wave 2's, the note's master phase (the sync modes) */
    float inc1, inc2, incm;
    float hold, hacc;                  /* DE-REZ: the held sample */
    float lasta;                       /* Wave 1's last output (OSC B's modifier reads it) */
    uint32_t lv1, lv2;                 /* the mip level each reads */
} cr_pair_t;

typedef struct {
    float base[CR_NP];                 /* the patch, 0..1 */
    uint8_t raw[CR_NP];
    float eff[CR_ND];                  /* this block's values after the matrix, by destination */
    const cr_wtbank_t *t1, *t2;
    uint32_t noise2;                   /* Wave 2 is the sine / noise bank */
    cr_pair_t pr[CR_PAIRS];
    uint32_t npair;                    /* 1 when SPREAD is 0 (one pair does: the others would be the same) */
    /* notes */
    uint8_t held[CR_NHELD], nheld, note, gate, legato_ok;
    float vel, pitch, target, bend;    /* pitch in semitones (MIDI note numbers) */
    float modw, expr, aftt;
    /* envelopes, LFOs */
    cr_env_t env[3];                   /* filter, amp, mod */
    float lph[2], lval[2], lsh[2], lstop[2];
    uint32_t rng;
    float tempo;
    uint8_t voice_only;                /* 0.18: a poly host's voice: render() gives the dry voice, L = R (cr_synth_fx) */
    /* the audio path */
    cr_filt_t filt;
    cr_fx_t fx;
    float amp, amp_prev, noise_lp, dist_g, dist_mix;
    float sub_p, sub_inc, sub_lv;                 /* the sub oscillator (0.12): phase, increment, level */
    uint32_t sub_w;                               /* its wave: 0 sine, 1 triangle, 2 square */
    int16_t *dl;
    uint32_t dl_w;
    float dl_pos, dl_target, dl_fb, dl_mix, dl_lp, dl_hp;
    float out_gain;
} cr_synth_t;

void cr_synth_init(cr_synth_t *s, int16_t *mem /* CR_SYN_MEM */);
void cr_synth_patch(cr_synth_t *s, const cr_patch_t *p);       /* all of a patch's values */
void cr_synth_param(cr_synth_t *s, uint32_t id, uint32_t v);   /* one value (a knob, a CC) */
/* a parameter's value after the mod matrix, 0..127 (for what the host runs outside the synth: the wavetable banks it
 * builds, the arp-seq's on/off, tempo and rate) */
uint32_t cr_synth_eff(const cr_synth_t *s, uint32_t id);
/* the patch as the arp-seq should see it: ARP, TEMPO and ARPRATE after the matrix */
void cr_synth_arp_view(const cr_synth_t *s, const cr_patch_t *p, cr_patch_t *out);
void cr_synth_tables(cr_synth_t *s, const cr_wtbank_t *w1, const cr_wtbank_t *w2, int w2_noise);
void cr_synth_tempo(cr_synth_t *s, float bpm);
void cr_synth_on(cr_synth_t *s, uint32_t note, uint32_t vel);
void cr_synth_off(cr_synth_t *s, uint32_t note);
void cr_synth_all_off(cr_synth_t *s);
void cr_synth_ctl(cr_synth_t *s, uint32_t src, float v);       /* CR_S_MODW / EXPR / AFTT: 0..1 */
void cr_synth_bend(cr_synth_t *s, float semis);
void cr_synth_fx(cr_synth_t *s, float *lr, uint32_t n);   /* the effects on voices summed in lr (L = R) */
/* writes n stereo frames (L R; the CRAFTsynth is mono: both the same), -1..1 */
void cr_synth_render(cr_synth_t *s, float *lr, uint32_t n);
int cr_synth_sounding(const cr_synth_t *s);
