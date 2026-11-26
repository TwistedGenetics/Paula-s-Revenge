/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the sound engine (see cr_synth.h). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Every CR_CTRL (4) samples: the mod sources (LFOs, mod envelope, note, velocity, wheels), the matrix (each source's
 * depth times its value, added to its destination's 0..1 value: LFOs at full depth sweep the whole range from the
 * middle, the others from 0, as the CRAFTsynth's manual says), then the envelopes, the glide, each oscillator
 * pair's increments and mip levels, the filter's coefficients, the amp's target, the delay's time.
 * Every sample: 4 pairs of oscillators (one when SPREAD is 0) through the modifier, the filter, the distortion, the
 * amp, the delay. */
#include "cr_synth.h"
#include "cr_math.h"

#define FS 44100.f
#define DT ((float)CR_CTRL / FS)

/* each destination's patch parameter (-1: PITCH and OSC2 PITCH, which have none: they start from the middle) */
static const int8_t DEST_P[CR_ND] = {
    -1, CR_W1POS, CR_W2POS, CR_MIX, -1, -1, CR_OMAMT, CR_L1RATE, CR_L2RATE, CR_L1SHAPE, CR_L2SHAPE, CR_CUT, CR_RES,
    CR_MORPH, CR_FEGAMT, CR_FA, CR_FD, CR_FS, CR_FR, CR_AEGAMT, CR_AA, CR_AD, CR_AS, CR_AR, CR_VOL, CR_MA, CR_MD,
    CR_MS, CR_MR, CR_DIST, CR_DMIX, CR_DTIME, CR_DFB, CR_SPREAD, CR_GLIDE, CR_DEP0, CR_DEP1, CR_DEP2,
    CR_BAMT, CR_VMIX, CR_CHMIX, CR_PHMIX, CR_CRUSH, CR_OTT,
    CR_W1BANK, CR_W2BANK, CR_OMMODE, CR_TUNE, CR_FINE, CR_OCT, CR_L1MODE, CR_L1SYNC, CR_L2MODE, CR_L2SYNC, CR_DSYNC,
    CR_DEP3, CR_DEP4, CR_DEP5, CR_DEP6, CR_DEP7, CR_ARP, CR_TEMPO, CR_ARPRATE, CR_BMODE, CR_FTYPE, CR_VMODE, CR_VSIZE,
    CR_VDECAY, CR_VWARP, CR_VDENS, CR_VMOD, CR_CHRATE, CR_PHRATE, CR_ERA, CR_SUBLVL, CR_SUBOCT, CR_SUBWAVE};
_Static_assert(sizeof DEST_P / sizeof DEST_P[0] == CR_ND, "a parameter for every destination");

/* polyBLEP: the correction that takes the step out of an edge at phase 0 (the sub's square) */
static float blep(float t, float dt)
{
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

/* a destination's value after the matrix as a patch value 0..127 (the stepped ones: banks, modes, types) */
static uint32_t er(const cr_synth_t *s, uint32_t d) { return (uint32_t)(s->eff[d] * 127.f + 0.5f); }
/* which source's depth a destination is (-1: none) */
static int32_t dep_of(uint32_t d)
{
    if (d >= CR_D_L1DEP && d <= CR_D_MEGDEP)
        return (int32_t)(d - CR_D_L1DEP);
    if (d >= CR_D_NOTEDEP && d <= CR_D_AFTTDEP)
        return (int32_t)(d - CR_D_NOTEDEP) + CR_S_NOTE;
    return -1;
}

/* SPREAD's chord shapes: semitones of the four pairs (a few cents apart, so they beat a little) */
static const float SHAPE[10][4] = {
    {0.f, 0.f, 0.f, 0.f},     {0.f, 4.03f, 7.f, 11.97f},  {0.f, 3.03f, 7.f, 11.97f},  {0.f, 4.03f, 7.f, 9.02f},
    {0.f, 5.03f, 7.f, 11.97f}, {0.f, 7.02f, -0.04f, 6.97f}, {0.f, 7.02f, 12.03f, 18.98f}, {0.f, 12.03f, 23.97f, 0.05f},
    {0.f, 12.03f, -11.97f, 0.05f}, {0.f, -11.97f, -24.03f, 0.05f}};

static const float L1_SYNC[16] = {1.f / 16, 1.f / 8, 3.f / 16, 1.f / 4, 3.f / 8, 1.f / 2, 3.f / 4, 1.f, 1.5f, 2.f, 3.f,
                                  4.f, 6.f, 8.f, 12.f, 16.f};      /* cycles a bar */
static const float L2_SYNC[8] = {1.f / 16, 1.f / 8, 1.f / 4, 1.f / 2, 1.f, 1.25f, 2.f, 4.f};   /* cycles a beat */
static const float L2_ROOT[8] = {0.125f, 0.25f, 0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f};         /* times the note */
static const float D_SYNC[9] = {0.25f, 1.f / 3, 0.5f, 0.75f, 1.f, 2.f, 3.f, 4.f, 6.f};     /* beats */

static float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
static uint32_t rnd(cr_synth_t *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return s->rng;
}
static float rndf(cr_synth_t *s) { return (float)(int32_t)rnd(s) * (1.f / 2147483648.f); }   /* -1..1 */

static float env_time(float v) { return 0.001f + 4.f * v * v * v; }               /* 0..4 s, as the CRAFTsynth */
static float note_hz(float n) { return 440.f * cr_exp2f((n - 69.f) * (1.f / 12.f)); }

void cr_synth_init(cr_synth_t *s, int16_t *mem)
{
    uint8_t *b = (uint8_t *)s;
    uint32_t i;
    cr_patch_t p;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    s->dl = mem;
    for (i = 0; i < CR_DLY_LEN; i++)
        mem[i] = 0;
    cr_fx_init(&s->fx, mem + CR_DLY_LEN);
    cr_filt_init(&s->filt, mem + CR_DLY_LEN + CR_FX_MEM);
    s->rng = 0x1234567u;
    s->tempo = 120.f;
    s->note = 48;
    s->pitch = s->target = 48.f;
    s->vel = 0.8f;
    s->expr = 1.f;
    s->dl_pos = s->dl_target = 4410.f;
    cr_patch_init(&p, "INIT");
    cr_synth_patch(s, &p);
}

void cr_synth_param(cr_synth_t *s, uint32_t id, uint32_t v)
{
    if (id >= CR_NP)
        return;
    s->raw[id] = (uint8_t)(v & 0x7Fu);
    s->base[id] = (float)(v & 0x7Fu) * (1.f / 127.f);
    if (id >= CR_DST0 && id <= CR_DST7 && (s->raw[id] >= CR_ND || !cr_dest_ok(id - CR_DST0, s->raw[id])))
        s->raw[id] = CR_D_NONE;
    {   /* the destination's value until the next control block works it out with the matrix */
        uint32_t d;
        for (d = 1; d < CR_ND; d++)
            if (DEST_P[d] == (int8_t)id)
                s->eff[d] = s->base[id];
    }
}

void cr_synth_arp_view(const cr_synth_t *s, const cr_patch_t *p, cr_patch_t *out)
{
    *out = *p;
    out->p[CR_ARP] = (uint8_t)er(s, CR_D_ARP);
    out->p[CR_TEMPO] = (uint8_t)er(s, CR_D_TEMPO);
    out->p[CR_ARPRATE] = (uint8_t)er(s, CR_D_ARPRATE);
}

uint32_t cr_synth_eff(const cr_synth_t *s, uint32_t id)
{
    uint32_t d;
    if (id >= CR_NP)
        return 0;
    for (d = 1; d < CR_ND; d++)
        if (DEST_P[d] == (int8_t)id)
            return er(s, d);
    return s->raw[id];
}

void cr_synth_patch(cr_synth_t *s, const cr_patch_t *p)
{
    uint32_t i;
    for (i = 0; i < CR_NP; i++)
        cr_synth_param(s, i, p->p[i]);
}

void cr_synth_tables(cr_synth_t *s, const cr_wtbank_t *w1, const cr_wtbank_t *w2, int w2_noise)
{
    s->t1 = w1;
    s->t2 = w2;
    s->noise2 = (uint32_t)w2_noise;
}

void cr_synth_tempo(cr_synth_t *s, float bpm) { s->tempo = bpm < 20.f ? 20.f : bpm > 400.f ? 400.f : bpm; }
void cr_synth_bend(cr_synth_t *s, float semis) { s->bend = semis; }
void cr_synth_ctl(cr_synth_t *s, uint32_t src, float v)
{
    v = clamp01(v);
    if (src == CR_S_MODW)
        s->modw = v;
    else if (src == CR_S_EXPR)
        s->expr = v;
    else if (src == CR_S_AFTT)
        s->aftt = v;
}

/* ------------------------------------------------------------------------------------------------ notes --- */
static void trigger(cr_synth_t *s)
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        s->env[i].st = 1;              /* from where it is: no click */
    for (i = 0; i < 2u; i++) {
        uint32_t m = er(s, i ? CR_D_L2MODE : CR_D_L1MODE);
        if (m < 42u || m >= 84u) {     /* retrigger, single: from the start */
            s->lph[i] = 0.f;
            s->lstop[i] = 0.f;
        }
    }
}

static void go_to(cr_synth_t *s, uint32_t note, int legato)
{
    uint32_t g = s->raw[CR_GLIDE];
    s->note = (uint8_t)note;
    s->target = (float)note;
    /* glide: above the middle always, below it only from a held note (fingered); in the middle none */
    if (g == 64u || (g < 64u && !legato))
        s->pitch = s->target;
}

void cr_synth_on(cr_synth_t *s, uint32_t note, uint32_t vel)
{
    uint32_t i, j, was = s->nheld;
    int legato;
    note &= 0x7Fu;
    for (i = j = 0; i < s->nheld; i++)
        if (s->held[i] != note)
            s->held[j++] = s->held[i];
    s->nheld = (uint8_t)j;
    if (s->nheld >= CR_NHELD) {
        for (i = 1; i < CR_NHELD; i++)
            s->held[i - 1u] = s->held[i];
        s->nheld--;
    }
    s->held[s->nheld++] = (uint8_t)note;
    legato = was > 0u && s->gate;
    go_to(s, note, legato);
    s->vel = (float)(vel & 0x7Fu) * (1.f / 127.f);
    if (!(legato && s->raw[CR_GLIDE] != 64u))
        trigger(s);                    /* the glide in the middle: every note; else legato (only the first) */
    s->gate = 1;
}

void cr_synth_off(cr_synth_t *s, uint32_t note)
{
    uint32_t i, j, top = s->nheld ? s->held[s->nheld - 1u] : 0xFFu;
    note &= 0x7Fu;
    for (i = j = 0; i < s->nheld; i++)
        if (s->held[i] != note)
            s->held[j++] = s->held[i];
    s->nheld = (uint8_t)j;
    if (note != top)
        return;
    if (s->nheld) {                    /* back to the note still held (last-note priority) */
        go_to(s, s->held[s->nheld - 1u], 1);
        if (s->raw[CR_GLIDE] == 64u)
            trigger(s);
        return;
    }
    s->gate = 0;
    for (i = 0; i < 3u; i++)
        if (s->env[i].st && s->env[i].st != 3u) {
            s->env[i].st = 3;
            s->env[i].rel0 = s->env[i].lv;
        }
}

void cr_synth_all_off(cr_synth_t *s)
{
    uint32_t i;
    s->nheld = 0;
    s->gate = 0;
    for (i = 0; i < 3u; i++)
        if (s->env[i].st && s->env[i].st != 3u) {
            s->env[i].st = 3;
            s->env[i].rel0 = s->env[i].lv;
        }
}

int cr_synth_sounding(const cr_synth_t *s)
{
    return s->env[1].st != 0u || s->raw[CR_AEGAMT] != 127u;
}

/* ------------------------------------------------------------------------------------------- the blocks --- */
static void env_run(cr_env_t *e, float a, float d, float sus, float r)
{
    switch (e->st) {
    case 1:
        e->lv += DT / env_time(a);
        if (e->lv >= 1.f) {
            e->lv = 1.f;
            e->st = 2;
        }
        break;
    case 2:
        e->lv = sus + (e->lv - sus) * cr_exp2f(-DT * 9.97f / env_time(d));
        break;
    case 3:
        e->lv *= cr_exp2f(-DT * 9.97f / env_time(r));
        if (e->lv < 1e-4f) {
            e->lv = 0.f;
            e->st = 0;
        }
        break;
    default:
        break;
    }
}

static float lfo_shape(cr_synth_t *s, uint32_t i, float sh)
{
    float ph = s->lph[i], x = clamp01(sh) * 4.f, v[5], f;
    uint32_t k = (uint32_t)x;
    if (k > 3u)
        k = 3u;
    f = x - (float)k;
    v[0] = cr_sin_turn(ph);
    v[1] = ph < 0.25f ? 4.f * ph : ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f;
    v[2] = 2.f * ph - 1.f;
    v[3] = ph < 0.5f ? 1.f : -1.f;
    v[4] = s->lsh[i];
    return v[k] + (v[k + 1u] - v[k]) * f;
}

static float lfo_hz(cr_synth_t *s, uint32_t i, float r)
{
    uint32_t sync = er(s, i ? CR_D_L2SYNC : CR_D_L1SYNC) >= 64u;
    if (!i)
        return sync ? L1_SYNC[(uint32_t)(r * 15.99f)] * s->tempo * (1.f / 240.f)
                    : 0.02f * cr_exp2f(r * 10.64f);
    if (r >= 0.5f)                     /* LFO 2's second half: a ratio of the note (into audio rate) */
        return L2_ROOT[(uint32_t)((r - 0.5f) * 15.99f)] * note_hz(s->pitch);
    return sync ? L2_SYNC[(uint32_t)(r * 15.99f)] * s->tempo * (1.f / 60.f) : 0.02f * cr_exp2f(r * 2.f * 10.64f);
}

static uint32_t mip(float inc)
{
    float x = inc * 256.f;
    uint32_t l = 0;
    while (x > 1.f && l < CR_WT_LV - 1u) {
        x *= 0.5f;
        l++;
    }
    return l;
}

static void control(cr_synth_t *s)
{
    float src[CR_NSRC], dep[CR_NSRC], mod[CR_ND];
    uint32_t i, d;
    float *e = s->eff;
    /* the sources (the LFOs and the mod envelope run on the last block's rates and times) */
    for (i = 0; i < 2u; i++) {
        float hz = lfo_hz(s, i, e[i ? CR_D_L2RATE : CR_D_L1RATE]);
        if (s->lstop[i] == 0.f) {
            s->lph[i] += hz * DT;
            if (s->lph[i] >= 1.f) {
                s->lph[i] -= (float)(int32_t)s->lph[i];
                s->lsh[i] = rndf(s);
                if (er(s, i ? CR_D_L2MODE : CR_D_L1MODE) >= 84u) {   /* single: one cycle, then it stays */
                    s->lph[i] = 0.9999f;
                    s->lstop[i] = 1.f;
                }
            }
        }
        s->lval[i] = lfo_shape(s, i, e[i ? CR_D_L2SHAPE : CR_D_L1SHAPE]);
    }
    env_run(&s->env[0], e[CR_D_FA], e[CR_D_FD], e[CR_D_FS], e[CR_D_FR]);
    env_run(&s->env[1], e[CR_D_AA], e[CR_D_AD], e[CR_D_AS], e[CR_D_AR]);
    env_run(&s->env[2], e[CR_D_MA], e[CR_D_MD], e[CR_D_MS], e[CR_D_MR]);
    src[CR_S_LFO1] = s->lval[0] * 0.5f;           /* full depth: the whole range from the middle */
    src[CR_S_LFO2] = s->lval[1] * 0.5f;
    src[CR_S_MEG] = s->env[2].lv;
    src[CR_S_NOTE] = (float)s->note * (1.f / 127.f);
    src[CR_S_VELO] = s->vel;
    src[CR_S_MODW] = s->modw;
    src[CR_S_EXPR] = s->expr;
    src[CR_S_AFTT] = s->aftt;
    /* the depths (the first three can be modulated themselves) */
    for (i = 0; i < CR_NSRC; i++)
        dep[i] = (float)((int32_t)s->raw[CR_DEP0 + i] - 64) * (1.f / 63.f);
    for (i = 0; i < CR_NSRC; i++) {
        int32_t k = dep_of(s->raw[CR_DST0 + i]);
        if (k >= 0) {
            float *t = &dep[k];
            *t += src[i] * dep[i] * 2.f;
            *t = *t < -1.f ? -1.f : *t > 1.f ? 1.f : *t;
        }
    }
    for (d = 0; d < CR_ND; d++)
        mod[d] = 0.f;
    for (i = 0; i < CR_NSRC; i++)
        mod[s->raw[CR_DST0 + i]] += src[i] * dep[i];
    for (d = 1; d < CR_ND; d++)
        e[d] = clamp01((DEST_P[d] < 0 ? 0.5f : s->base[(uint32_t)DEST_P[d]]) + mod[d]);

    /* glide */
    {
        float g = e[CR_D_GLIDE] - 0.5f, t;
        if (g < 0.f)
            g = -g;
        t = 2.5f * (g * 2.f) * (g * 2.f);         /* 0..2.5 s */
        if (s->raw[CR_GLIDE] == 64u || t < 0.002f)
            s->pitch = s->target;
        else
            s->pitch = s->target + (s->pitch - s->target) * cr_exp2f(-DT * 6.f / t);
    }
    /* the pairs: pitches, increments, mip levels, which waves */
    {
        uint32_t sp = (uint32_t)(e[CR_D_SPREAD] * 127.f + 0.5f), shape = cr_spread_shape(sp), oct;
        float n1, n2, off[4], u;
        oct = (er(s, CR_D_OCT) * 7u) >> 7;
        n1 = s->pitch + 12.f * ((float)oct - 2.f) + s->bend + (e[CR_D_PITCH] - 0.5f) * 48.f;
        n2 = n1 + (float)cr_semi_of(er(s, CR_D_TUNE)) + (e[CR_D_FINE] - 0.5f) * 2.f * (127.f / 126.f) +
             (e[CR_D_O2PITCH] - 0.5f) * 48.f;
        if (shape) {
            for (i = 0; i < 4u; i++)
                off[i] = SHAPE[shape][i];
            s->npair = 4;
        } else {
            u = (float)sp * (1.f / 63.f);
            u = u * u * 0.45f;
            off[0] = -u;
            off[1] = -u * 0.33f;
            off[2] = u * 0.31f;
            off[3] = u;
            if (sp == 0u)
                s->npair = 1;
            else if (s->npair <= 1u) {
                for (i = 1; i < CR_PAIRS; i++)    /* the others start where the first is: no jump */
                    s->pr[i] = s->pr[0];
                s->npair = 4;
            } else
                s->npair = 4;
        }
        for (i = 0; i < s->npair; i++) {
            cr_pair_t *p = &s->pr[i];
            float f1 = note_hz(n1 + off[i]), f2 = note_hz(n2 + off[i]);
            p->inc1 = p->incm = f1 * (1.f / FS);
            p->inc2 = f2 * (1.f / FS);
            if (p->inc1 > 0.49f)
                p->inc1 = p->incm = 0.49f;
            if (p->inc2 > 0.49f)
                p->inc2 = 0.49f;
            p->lv1 = mip(p->inc1);
            p->lv2 = mip(p->inc2);
        }
        /* the sub: one or two octaves under OSC A's note (its middle, without the spread), its own wave */
        {
            float lv = e[CR_D_SUBLVL];
            s->sub_inc = note_hz(n1 - 12.f * (float)(1u + cr_band(er(s, CR_D_SUBOCT), 2))) * (1.f / FS);
            if (s->sub_inc > 0.25f)
                s->sub_inc = 0.25f;
            s->sub_lv = lv * lv * 0.9f;
            s->sub_w = cr_band(er(s, CR_D_SUBWAVE), 3);
        }
    }
    /* the filter (its type, cutoff with the filter EG, resonance, morph) */
    {
        float cut = clamp01(e[CR_D_CUT] + (e[CR_D_FEGAMT] - 0.5f) * 2.f * s->env[0].lv);
        cr_filt_set(&s->filt, cr_band(er(s, CR_D_FTYPE), CR_NFTYPE), 20.f * cr_exp2f(cut * 10.f), e[CR_D_RES], e[CR_D_MORPH]);
    }
    /* the amp: AMP EG amount (bipolar; at 0 it drones) times the volume */
    {
        float a = (e[CR_D_AEGAMT] - 0.5f) * 2.f, lv = s->env[1].lv, g, vol = e[CR_D_VOL];
        if (a >= 0.f)
            g = (1.f - a) + a * lv;
        else if (s->env[1].st == 3u || s->env[1].st == 0u)   /* inverted: the release still falls to 0 */
            g = (1.f + a + (-a) * (1.f - s->env[1].rel0)) * (s->env[1].rel0 > 1e-4f ? lv / s->env[1].rel0 : 0.f);
        else
            g = 1.f + a + (-a) * (1.f - lv);
        s->amp = g * vol * vol;
    }
    /* distortion, delay */
    {
        float d = e[CR_D_DIST];
        s->dist_g = 1.f + 30.f * d * d;
        s->dist_mix = d * 8.f > 1.f ? 1.f : d * 8.f;
        if (er(s, CR_D_DSYNC) >= 64u)
            s->dl_target = D_SYNC[(uint32_t)(e[CR_D_DTIME] * 8.99f)] * 60.f / s->tempo * FS;
        else
            s->dl_target = 0.0005f * cr_exp2f(e[CR_D_DTIME] * 10.55f) * FS;
        if (s->dl_target > (float)(CR_DLY_LEN - 8u))
            s->dl_target = (float)(CR_DLY_LEN - 8u);
        if (s->dl_target < 1.f)
            s->dl_target = 1.f;
        s->dl_fb = e[CR_D_DFB] * 0.9f;
        s->dl_mix = e[CR_D_DMIX];
    }
    cr_fx_set(&s->fx, cr_band(er(s, CR_D_ERA), 6), e[CR_D_CRUSH], e[CR_D_PHMIX], e[CR_D_PHRATE], e[CR_D_CHMIX],
              e[CR_D_CHRATE], cr_band(er(s, CR_D_VMODE), 6), e[CR_D_VSIZE], e[CR_D_VDECAY], e[CR_D_VMIX],
              e[CR_D_VWARP], e[CR_D_VDENS], e[CR_D_VMOD], e[CR_D_OTT]);
}

/* ------------------------------------------------------------------------------------------ the samples --- */
static inline float rd(const int16_t *a, const int16_t *b, float m, float ph)
{
    float x = ph * 256.f, f, va, vb;
    int32_t k = (int32_t)x;
    uint32_t k0, k1;
    f = x - (float)k;
    k0 = (uint32_t)k & 255u;
    k1 = (k0 + 1u) & 255u;
    va = (float)a[k0] + (float)(a[k1] - a[k0]) * f;
    vb = (float)b[k0] + (float)(b[k1] - b[k0]) * f;
    return va + (vb - va) * m;
}

static inline float wrap1(float p)
{
    if (p >= 1.f)
        p -= 1.f;
    if (p < 0.f)
        p += 1.f;
    if (p >= 1.f || p < 0.f)
        p = cr_frac(p);
    return p;
}
static inline float hann(float p) { return 0.5f - 0.5f * cr_sin_turn(p + 0.25f < 1.f ? p + 0.25f : p - 0.75f); }
static inline float ror(float p, float m) { return p < m ? 0.5f * p / m : 0.5f + 0.5f * (p - m) / (1.f - m); }

static void fx_tail(cr_synth_t *s, float y, float *out);
void cr_synth_render(cr_synth_t *s, float *lr, uint32_t n)
{
    const float sc = cr_wt_scale();
    uint32_t done = 0;
    while (done < n) {
        uint32_t k, len = n - done < CR_CTRL ? n - done : CR_CTRL, i, om, np;
        float *e = s->eff, w1x, w2x, mix, a, ratio, crush_l, ncol, ngain, amp0, damp, ba, bsync;
        uint32_t bm;
        uint32_t wa1, wb1, wa2, wb2;
        float m1, m2;
        control(s);
        if (!s->t1 || !s->t2) {
            for (k = 0; k < 2u * len; k++)
                lr[2u * done + k] = 0.f;
            done += len;
            continue;
        }
        w1x = e[CR_D_W1] * 4.f;
        wa1 = (uint32_t)w1x;
        wa1 = wa1 > 3u ? 3u : wa1;
        m1 = w1x - (float)wa1;
        wb1 = wa1 + 1u;
        w2x = e[CR_D_W2] * 4.f;
        wa2 = (uint32_t)w2x;
        wa2 = wa2 > 3u ? 3u : wa2;
        m2 = w2x - (float)wa2;
        wb2 = wa2 + 1u;
        mix = e[CR_D_MIX];
        a = e[CR_D_OMAMT];
        om = cr_om_of(er(s, CR_D_OMMODE));
        ratio = 1.f + a * 7.f;
        crush_l = cr_exp2f(15.f - 14.f * a);
        ncol = e[CR_D_W2] < 0.5f ? 0.03f : 0.03f + 0.97f * (e[CR_D_W2] - 0.5f) * 2.f;
        ngain = cr_exp2f(-0.5f * cr_log2f(ncol)) * 0.6f;
        amp0 = s->amp_prev;
        damp = (s->amp - amp0) / (float)len;
        bm = cr_band(er(s, CR_D_BMODE), 5);       /* OSC B's modifier: A's last sample modulates B */
        ba = e[CR_D_BAMT];
        bsync = 1.f + ba * 3.f;                  /* SYNC TO A: B up to 2 octaves above, restarted with A */
        np = s->npair;
        for (k = 0; k < len; k++) {
            float sum = 0.f, y, nz = 0.f;
            if (s->noise2) {
                float wn = rndf(s);
                s->noise_lp += ncol * (wn - s->noise_lp);
                nz = s->noise_lp * ngain;
            }
            for (i = 0; i < np; i++) {
                cr_pair_t *p = &s->pr[i];
                const int16_t *A1 = s->t1->w[wa1][p->lv1], *B1 = s->t1->w[wb1][p->lv1];
                float w2, x, ph;
                /* Wave 2 (the modulator), through its own modifier (0.4): A's last sample is its modulator */
                {
                    float rp, la = p->lasta;
                    int wrapped = 0;
                    p->pm += p->incm;              /* the note's master phase (the sync modes' counter) */
                    if (p->pm >= 1.f) {
                        p->pm -= 1.f;
                        wrapped = 1;
                    }
                    if (ba > 0.f && bm == 0u)      /* FM FROM A: through zero */
                        p->p2 = wrap1(p->p2 + p->inc2 * (1.f + ba * 6.f * la));
                    else if (ba > 0.f && bm == 2u) {   /* SYNC TO A */
                        p->p2 = wrap1(p->p2 + p->inc2 * bsync);
                        if (wrapped)
                            p->p2 = 0.f;
                    } else {
                        p->p2 += p->inc2;
                        if (p->p2 >= 1.f)
                            p->p2 -= 1.f;
                    }
                    rp = (ba > 0.f && bm == 1u) ? wrap1(p->p2 + ba * 1.5f * la) : p->p2;   /* PM FROM A */
                    if (s->noise2) {
                        float sn = rd(s->t2->w[0][0], s->t2->w[0][0], 0.f, rp) * sc, nm = e[CR_D_W2] * 2.f;
                        nm = nm > 1.f ? 1.f : nm;
                        w2 = sn + (nz - sn) * nm;
                    } else
                        w2 = rd(s->t2->w[wa2][p->lv2], s->t2->w[wb2][p->lv2], m2, rp) * sc;
                    if (ba > 0.f && bm == 3u)      /* RING WITH A */
                        w2 += (w2 * la - w2) * ba;
                    else if (ba > 0.f && bm == 4u) /* WAVEFOLD: B folded on itself */
                        w2 = cr_sin_turn(cr_frac(w2 * (1.f + ba * 8.f) * 0.25f));
                }
                /* Wave 1 through the modifier */
                switch (om) {
                case CR_OM_PM:
                    p->p1 = wrap1(p->p1 + p->inc1);
                    x = rd(A1, B1, m1, wrap1(p->p1 + a * 1.5f * w2)) * sc;
                    break;
                case CR_OM_FM:
                    p->p1 = wrap1(p->p1 + p->inc1 * (1.f + a * 6.f * w2));
                    x = rd(A1, B1, m1, p->p1) * sc;
                    break;
                case CR_OM_WSYNC:
                    ph = p->pm * ratio;
                    x = rd(A1, B1, m1, ph - (float)(int32_t)ph) * sc * hann(p->pm);
                    break;
                case CR_OM_HSYNC:
                    ph = p->pm * ratio;
                    x = rd(A1, B1, m1, ph - (float)(int32_t)ph) * sc;
                    break;
                case CR_OM_WAMP:
                    p->p1 = wrap1(p->p1 + p->inc1);
                    ph = p->p1 * ratio;
                    x = rd(A1, B1, m1, p->p1) * sc * hann(ph - (float)(int32_t)ph);
                    break;
                case CR_OM_VOCAL: {
                    float q = ror(p->pm, 0.5f - 0.2f * a);
                    ph = q * ratio;
                    x = rd(A1, B1, m1, ph - (float)(int32_t)ph) * sc;
                    x = x * (x < 0.f ? -x : x) * hann(q);
                    break;
                }
                case CR_OM_ROR:
                    p->p1 = wrap1(p->p1 + p->inc1);
                    x = rd(A1, B1, m1, ror(p->p1, 0.5f - a * 0.48f)) * sc;
                    break;
                case CR_OM_SCRUNCH: {
                    float q, q2, q4, q8, r, ee = a * 3.f;
                    p->p1 = wrap1(p->p1 + p->inc1);
                    q = 1.f - p->p1;
                    q2 = q * q;
                    q4 = q2 * q2;
                    q8 = q4 * q4;
                    r = ee < 1.f ? q + (q2 - q) * ee : ee < 2.f ? q2 + (q4 - q2) * (ee - 1.f) : q4 + (q8 - q4) * (ee - 2.f);
                    x = rd(A1, B1, m1, wrap1(1.f - r)) * sc;
                    break;
                }
                case CR_OM_LOFI: {
                    float qn = (float)(256u >> (uint32_t)(a * 5.f));
                    p->p1 = wrap1(p->p1 + p->inc1);
                    ph = wrap1(p->p1 - a * 0.12f * cr_sin_turn(p->p1));
                    ph = (float)(int32_t)(ph * qn) / qn;
                    x = rd(A1, B1, m1, ph) * sc;
                    break;
                }
                default:                       /* the rest change Wave 1's output, not how it is read */
                    p->p1 = wrap1(p->p1 + p->inc1);
                    x = rd(A1, B1, m1, p->p1) * sc;
                    switch (om) {
                    case CR_OM_RING: x += (x * w2 - x) * a; break;
                    case CR_OM_MIN: x += ((x < w2 ? x : w2) - x) * a; break;
                    case CR_OM_TFOLD: {
                        float t = (x * (1.f + a * 8.f) + 1.f) * 0.25f;
                        t -= (float)(int32_t)t;
                        if (t < 0.f)
                            t += 1.f;
                        x = 1.f - 4.f * (t < 0.5f ? 0.5f - t : t - 0.5f);
                        x = x < -1.f ? -1.f : x;
                        break;
                    }
                    case CR_OM_SFOLD: x = cr_sin_turn(cr_frac(x * (1.f + a * 8.f) * 0.25f)); break;
                    case CR_OM_DEREZ:
                        p->hacc += cr_exp2f(-a * 8.f);
                        if (p->hacc >= 1.f) {
                            p->hacc -= 1.f;
                            p->hold = x;
                        }
                        x = p->hold;
                        break;
                    case CR_OM_CRUSH: x = (float)(int32_t)(x * crush_l + (x < 0.f ? -0.5f : 0.5f)) / crush_l; break;
                    case CR_OM_GLITCH: {
                        float q = cr_sin_turn(cr_frac(x * (0.25f + a * 0.5f))), lv = 3.f + (1.f - a) * 29.f;
                        q = (float)(int32_t)(q * lv + (q < 0.f ? -0.5f : 0.5f)) / lv;
                        x += (q - x) * a;
                        break;
                    }
                    default: break;
                    }
                    break;
                }
                p->lasta = x;
                sum += x + (w2 - x) * mix;
            }
            y = np > 1u ? sum * 0.5f : sum;
            if (s->sub_lv > 0.f) {             /* the sub, before the filter: it is part of the sound */
                float f = s->sub_p, dt = s->sub_inc, v;
                s->sub_p += dt;
                if (s->sub_p >= 1.f)
                    s->sub_p -= 1.f;
                if (s->sub_w == 0u)
                    v = cr_sin_turn(f);
                else if (s->sub_w == 1u)
                    v = 1.f - 4.f * (f < 0.5f ? 0.5f - f : f - 0.5f);
                else {                         /* a square, its two edges smoothed (polyBLEP) */
                    float g = f + 0.5f;
                    g -= g >= 1.f ? 1.f : 0.f;
                    v = (f < 0.5f ? 1.f : -1.f) + blep(f, dt) - blep(g, dt);
                    v *= 0.7f;                 /* as loud as the sine */
                }
                y += v * s->sub_lv;
            }
            /* the filter (its type: cr_filter.c) */
            y = cr_filt_run(&s->filt, y);
            /* the distortion: a waveshaping overdrive */
            if (s->dist_mix > 0.f) {
                float d = cr_tanh(y * s->dist_g) * (1.f - 0.25f * s->dist_mix);
                y += (d - y) * s->dist_mix;
            }
            if (!s->voice_only)
                y = cr_fx_era(&s->fx, y);      /* EraCrush: the sampler's converter */
            y *= amp0 + damp * (float)k;
            if (s->voice_only) {               /* a voice of a polyphonic host: the effects come after the sum */
                lr[2u * (done + k)] = lr[2u * (done + k) + 1u] = y;
                continue;
            }
            fx_tail(s, y, lr + 2u * (done + k));
        }
        s->amp_prev = s->amp;
        done += len;
    }
}

/* the effects after the voice (phaser, delay, chorus, reverb, OTT, the soft clip): out[0] L, out[1] R */
static void fx_tail(cr_synth_t *s, float y, float *out)
{
    y = cr_fx_phaser(&s->fx, y);
    /* the delay (mono, its time glides: moving it bends the echoes like tape) */
    {
        float rp, f, wet, a0, a1;
        uint32_t r0;
        s->dl_pos += (s->dl_target - s->dl_pos) * 0.0004f;
        rp = (float)s->dl_w - s->dl_pos;
        if (rp < 0.f)
            rp += (float)CR_DLY_LEN;
        r0 = (uint32_t)rp;
        f = rp - (float)r0;
        a0 = (float)s->dl[r0 & (CR_DLY_LEN - 1u)];
        a1 = (float)s->dl[(r0 + 1u) & (CR_DLY_LEN - 1u)];
        wet = (a0 + (a1 - a0) * f) * (1.f / 16384.f);
        s->dl_lp += 0.55f * (wet - s->dl_lp);
        s->dl_hp += 0.002f * (s->dl_lp - s->dl_hp);
        {
            float wv = (y + (s->dl_lp - s->dl_hp) * s->dl_fb) * 16384.f;
            wv = wv > 32767.f ? 32767.f : wv < -32767.f ? -32767.f : wv;
            s->dl[s->dl_w] = (int16_t)wv;
        }
        s->dl_w = (s->dl_w + 1u) & (CR_DLY_LEN - 1u);
        y += (wet - y) * s->dl_mix;
    }
    /* from here stereo: the chorus, the reverb, OTT */
    {
        float L, R;
        cr_fx_chorus(&s->fx, y, &L, &R);
        cr_fx_reverb(&s->fx, &L, &R);
        cr_fx_ott(&s->fx, &L, &R);
        out[0] = cr_soft(L * 0.8f);
        out[1] = cr_soft(R * 0.8f);
    }
}

/* a polyphonic host's effects: its voices (voice_only) summed in lr (L = R), through this synth's effects as the
 * patch and its own modulation set them (its control blocks run; it plays no note) */
void cr_synth_fx(cr_synth_t *s, float *lr, uint32_t n)
{
    uint32_t done = 0, k;
    while (done < n) {
        uint32_t len = n - done < CR_CTRL ? n - done : CR_CTRL;
        control(s);
        for (k = 0; k < len; k++) {
            float *o = lr + 2u * (done + k);
            fx_tail(s, cr_fx_era(&s->fx, o[0]), o);
        }
        done += len;
    }
}
