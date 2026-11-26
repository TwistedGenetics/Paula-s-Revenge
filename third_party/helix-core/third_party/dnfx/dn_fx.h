/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the FX rack's effects (0.12; the reverbs and the rest 0.16). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Two send slots (A, B). Each holds one effect, fed the tracks' sends, its output (wet only) added to the mix at its
 * return level. Their memory comes from one arena the app owns (dn_app.c), laid out whenever an effect changes: an
 * effect whose memory doesn't fit is refused. MASSIVE takes what the other slot leaves (its longest time follows).
 *
 *   DELAY      0.12's: a ping-pong at half rate (dark, dub-like), TIME synced, FEEDBACK, TONE.        (dn_fx.c)
 *   TAPE, BBD  Carnac OS's echoes: a tape loop (wow, flutter, head loss, saturation) and a bucket brigade (held
 *              buckets, compander breath); DEGRADE wears them, FEEDBACK past 100 runs away. (dn_fxc.c)
 *   SPRING, TOMB, MOD, CELESTIAL, DOLMEN, TUMULUS
 *              Carnac OS's reverbs: two dispersive springs; a Freeverb-style room; Dattorro's tank moving (MOD),
 *              with an octave-up shimmer (CELESTIAL), small and still (DOLMEN), long and dark (TUMULUS). (dn_fxc.c)
 *   QUADRA     an Alesis Quadraverb-style chain: configurations of EQ, chorus / flange / detune, delay and a small
 *              plate, with a 16-BIT switch for the 1989 grit. (dn_fxq.c)
 *   FILTER, PHASER, CHORUS, CRUSH, RING, DRIVE
 *              (0.41) the light ones: little memory, little CPU. (dn_fxl.c)
 *   MASSIVE    Jamie's Super-Massive ("Horizon", microKORG 2): an 8-line feedback delay network with ten modes, from a
 *              room to a self-oscillating dub echo, at half rate. (dn_fxm.c)
 *
 * Every effect limits its send first (Carnac's law: a peak over full scale ducks it, ~0.1 s back), so a loud chord
 * into a reverb never clips inside it. */
#pragma once
#include <stdint.h>

enum { DN_FX_OFF, DN_FX_DELAY, DN_FX_TAPE, DN_FX_BBD, DN_FX_SPRING, DN_FX_TOMB, DN_FX_MOD, DN_FX_CELEST, DN_FX_DOLMEN,
       DN_FX_TUMULUS, DN_FX_QUADRA, DN_FX_MASSIVE,
       DN_FX_FILTER, DN_FX_PHASER, DN_FX_CHORUS, DN_FX_CRUSH, DN_FX_RING, DN_FX_DRIVE,   /* (0.41) the light ones */
       DN_FX_N };
#define DN_FX_NDIV 12u
#define DN_FX_NP 7u                                 /* an effect's values (after its type): two pages of the FX page */
#define DN_FX_BLK 32u                               /* the effects' inner block */

typedef struct {
    uint8_t type;                                   /* DN_FX_* */
    uint8_t p[DN_FX_NP];                            /* 0..dn_fx_pmax (see dn_fx_pname) */
} dn_fx_p_t;

/* ---- each effect's state (in the slot; its lines in the arena) ---- */
typedef struct {                                    /* DELAY */
    int16_t *l, *r;
    uint32_t len, w, d_q8;
    int32_t lp_l, lp_r, in_acc, y_l, y_r, o_l, o_r;
    uint8_t ph;
} dn_fxd_t;

typedef struct {                                    /* TAPE, BBD (Carnac's TapeState) */
    int16_t *tape;
    uint32_t wpos_q16;
    int32_t gap_q16, gap_tgt_q16, speed_q24, speed_tgt_q24;
    uint32_t last_len, wow_ph, flutter_ph;
    int32_t aa_lp, loss_lp, loss2_lp;
    int32_t bb_aa1, bb_aa2, bb_rc1, bb_rc2, bb_rc1b, bb_rc2b, bb_env;
    float hp_lp, hp2_lp, bb_hp, bb_hpb;             /* the high-passes (float: an integer one this slow sticks up to
                                                     * 127 LSB off zero, a DC left for ever) */
    uint32_t bb_rng, seed_rng;
    int32_t run_lp;
} dn_fxt_t;

typedef struct { uint32_t w, c; int32_t aa1, aa2, hp, lp; } dn_fxsh_t;   /* CELESTIAL's octave-up shifter */
typedef struct {                                    /* the reverbs' common front: low cut, predelay */
    int16_t *pre;
    uint32_t pre_w;
    float lc;                                       /* LOW CUT's state */
} dn_fxf_t;
typedef struct {                                    /* MOD, CELESTIAL, DOLMEN, TUMULUS: Dattorro's tank */
    dn_fxf_t f;
    int16_t *ring, *sh_buf;
    uint32_t p;
    int32_t lp_l, lp_r, dc_l, dc_r;
    uint32_t ph_l, ph_r;
    int32_t wander[2], wander_tgt[2];
    uint32_t rng, wander_n;
    int32_t mod_l, mod_r, duck, duck_g, duck_step;
    dn_fxsh_t sh;
} dn_fxk_t;
#define DN_SP_STAGES 12
typedef struct { float lp; float h[DN_SP_STAGES / 2][3][3]; } dn_fxspr_t;
typedef struct {                                    /* SPRING */
    dn_fxf_t f;
    int16_t *ring;
    uint32_t p;
    float hp;
    dn_fxspr_t sp[2];
} dn_fxs_t;
typedef struct {                                    /* TOMB: 4 combs, 2 allpasses */
    dn_fxf_t f;
    int16_t *comb, *ap;
    uint32_t ci[4], ai[2];
    int32_t clp[4];
} dn_fxr_t;

typedef struct {                                    /* QUADRA */
    int16_t *dl, *ch, *rv;                          /* the delay (half rate), the chorus / flanger / shifter, the plate */
    uint32_t dw, d_q8, cw, rp;
    float eq[4];                                    /* the EQ's states */
    int32_t din, dy, dyo, cfb;
    uint32_t lfo, sh_c;
    float rlp[4];
    int32_t gh_l, gh_r;                             /* 16-BIT: the held sample */
    uint8_t dph, gph;
} dn_fxq_t;

#define DN_FXM_L 8
typedef struct {                                    /* MASSIVE (Horizon) */
    int16_t *mem;
    uint32_t cap;                                   /* its memory, int16s */
    int16_t *line[DN_FXM_L], *diff[DN_FXM_L];
    uint32_t llen[DN_FXM_L], dlen[DN_FXM_L], dreg[DN_FXM_L];
    uint32_t lw[DN_FXM_L], dw[DN_FXM_L];            /* each line's and diffuser's write place */
    float tmax;                                     /* the longest TIME the lines hold now, half-rate samples */
    uint8_t mode, pend;
    uint8_t ph, inited;
    float in_l, in_r, y_l, y_r, yp_l, yp_r;
    float lenT[DN_FXM_L], len[DN_FXM_L], gT[DN_FXM_L], g[DN_FXM_L], md[DN_FXM_L];
    float mat[DN_FXM_L][DN_FXM_L];                  /* mat[j][i]: x[j]'s part of line i */
    float blend, blendT, matBlend, diffG, diffGT, diffMix, diffMixT;
    float injL[DN_FXM_L], injR[DN_FXM_L], outL[DN_FXM_L], outR[DN_FXM_L];
    int8_t src[DN_FXM_L];
    float hp[DN_FXM_L], lp[DN_FXM_L], hpA, lpA, ohpA, olpA, ohp[2], olp[2];
    float satIn, satOut, inGain, outGain, glide;
    float s0, c0, s1, c1, k0, k1;
    float fade, fstep;
    uint8_t last[DN_FX_NP];                         /* the values the targets were made for */
    uint16_t last_bpm;
} dn_fxm_t;

typedef struct {                                    /* (0.41) the light effects (dn_fxl.c) */
    int16_t *buf;                                   /* CHORUS: its lines, L then R */
    uint32_t w, rng;
    float lph, shv, rph, hph;                       /* the LFO's phase (turns), S&H's value; RING's carrier; CRUSH's hold */
    float s1[2], s2[2], fb[2], hp[2], lp[2], dc[2], held[2];
    float ap[2][8];                                 /* PHASER's stages */
} dn_fxl_t;

typedef struct {
    uint8_t type;
    void *mem;
    uint32_t bytes;                                 /* its memory in the arena */
    float lim;                                      /* the send limiter's envelope */
    union { dn_fxd_t d; dn_fxt_t t; dn_fxk_t k; dn_fxs_t s; dn_fxr_t r; dn_fxq_t q; dn_fxm_t m; dn_fxl_t l; } u;
} dn_fx_s_t;

extern const char *const DN_FX_NAME[DN_FX_N];
extern const char *const DN_FX_DIV_NAME[DN_FX_NDIV];
extern const uint8_t DN_FX_DIV[DN_FX_NDIV][2];      /* beats: num / den */

uint32_t dn_fx_ram(uint32_t type);                  /* bytes an effect needs from the arena (MASSIVE: at least) */
int dn_fx_flex(uint32_t type);                      /* 1: it takes whatever more it's given (MASSIVE) */
void dn_fx_default(dn_fx_p_t *p, uint32_t type);
/* mem: bytes (>= dn_fx_ram(type)), 4-aligned, cleared here */
void dn_fx_init(dn_fx_s_t *s, uint32_t type, void *mem, uint32_t bytes);
/* n frames: in the send (stereo, Q15-ish int32), out added to (stereo), at return level ret (100 = unity) */
void dn_fx_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *in, int32_t *out, uint32_t n, uint32_t bpm,
               uint32_t ret);
uint32_t dn_fx_delay_samples(const dn_fx_p_t *p, uint32_t bpm);   /* DELAY's time, full-rate samples (clamped) */

/* the values: how many, each one's name (by the effect, and for QUADRA its configuration), top, text */
uint32_t dn_fx_np(uint32_t type);
const char *dn_fx_pname(const dn_fx_p_t *p, uint32_t i);           /* "-": not used */
int32_t dn_fx_pmax(const dn_fx_p_t *p, uint32_t i);
void dn_fx_ptext(const dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t i, uint32_t bpm, char out[8]);
uint32_t dn_fx_sync_samples(uint32_t div, uint32_t bpm);           /* a DN_FX_DIV at the tempo, full-rate samples */
void dn_fx_value(dn_fx_p_t *p, uint32_t i, uint32_t v);            /* set value i (QUADRA's CONFIG: its values too) */

/* the parts (dn_fxc.c, dn_fxq.c, dn_fxm.c): mono = the limited send (L + R) / 2 where they take mono; out (stereo)
 * gets the wet (Q15-ish, before the return) */
void dn_fxc_init(dn_fx_s_t *s);
void dn_fxc_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm);
uint32_t dn_fxc_ram(uint32_t type);
void dn_fxq_init(dn_fx_s_t *s);
void dn_fxq_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm);
uint32_t dn_fxq_ram(void);
const char *dn_fxq_pname(uint32_t cfg, uint32_t i);
#define DN_FXQ_NCFG 6u
extern const char *const DN_FXQ_CFG[DN_FXQ_NCFG];
void dn_fxm_init(dn_fx_s_t *s);
void dn_fxm_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *st, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm);
uint32_t dn_fxm_ram(void);
float dn_fxm_time_ms(const dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t bpm);   /* TIME as it plays */
#define DN_FXM_NMODE 10u
extern const char *const DN_FXM_MODE[DN_FXM_NMODE];
extern const float DN_FXM_BEATS[13];                /* TIME 0..12: the synced divisions, in beats */

void dn_fxl_init(dn_fx_s_t *s);
void dn_fxl_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *st, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm);
uint32_t dn_fxl_ram(uint32_t type);
uint32_t dn_fxl_np(uint32_t type);
void dn_fxl_default(dn_fx_p_t *p);
const char *dn_fxl_pname(uint32_t type, uint32_t i);
int32_t dn_fxl_pmax(uint32_t type, uint32_t i);
int dn_fxl_ptext(uint32_t type, uint32_t i, uint32_t v, char out[8]);   /* 1: written */

/* shared helpers */
#ifdef DN_FX_CLIPS                                  /* (tests: how often a line clamps) */
extern uint32_t dn_fx_clips;
static inline int32_t dn_fx_c16(int32_t v)
{
    if (v > 32767 || v < -32768)
        dn_fx_clips++;
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}
#else
static inline int32_t dn_fx_c16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
#endif
/* Q15 multiply rounding toward zero (Carnac: a feedback gain under 1 then always shrinks a value, so tails die) */
static inline int32_t dn_fx_mz(int32_t a, int32_t b)
{
    int32_t t = a * b;
    return (t + ((t >> 31) & 32767)) >> 15;
}
static inline int32_t dn_fx_mq(int32_t a, int32_t b) { return (a * b) >> 15; }
