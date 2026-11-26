/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: QUADRA, an Alesis Quadraverb-flavoured multi-effect in one FX slot (0.16). Copyright (C) 2026 Jamie
 * (TwistedGenetics). An original design after the Quadraverb's idea (1989: one box, a few configurations of EQ, pitch,
 * delay and reverb in a chain), not a copy of its algorithms.
 *
 * The configurations (CONFIG):
 *   CHORUS   EQ (tilt) > chorus (two taps, quadrature LFOs) > delay > plate
 *   REVERB   EQ > predelay > plate
 *   5EQ>REV  a five-band EQ (100 / 400 / 1500 / 5000 Hz splits, +-12 dB) > plate
 *   FLANGE   EQ > flanger (short swept delay with regeneration) > delay > plate
 *   DETUNE   EQ > detune (a pitch shifter each side, up / down a few cents) > delay > plate
 *   DLY>REV  EQ > delay > plate, REV LVL setting the plate against the echoes
 * Each stage passes on what it was given plus its effect (the Quadraverb's chain), the slot returns the effects only.
 * The plate: two input allpasses into four lines mixed by a Hadamard matrix, damped, gains set for the DECAY's RT60
 * (0.2 .. 6 s); float, the lines int16 truncated toward zero (a tail ends in silence). The delay runs at half rate
 * (0.74 s), darker each repeat. 16-BIT ON: the wet held over pairs of samples and cut to 12 bits, the 1989 grain.
 * Memory 52 KB: smaller than Carnac's reverbs, one slot's worth. */
#include "dn_fx.h"
#include "dsp/fastmath.h"

#define QD_LEN 16384u                               /* the delay, half-rate samples */
#define QC_LEN 2048u                                /* the chorus / flanger / detune line */
#define QR_RING 8192u
#define QR_MASK (QR_RING - 1u)
static const uint16_t QR_LEN[4] = {1301, 1607, 1949, 2333};
#define QR_AP1 185u
#define QR_AP2 139u
#define QR_S_AP2 (QR_AP1 + 1u)
#define QR_S_L0 (QR_S_AP2 + QR_AP2 + 1u)
#define QR_S_L1 (QR_S_L0 + 1301u + 1u)
#define QR_S_L2 (QR_S_L1 + 1607u + 1u)
#define QR_S_L3 (QR_S_L2 + 1949u + 1u)
typedef char fxq_plate_fits[QR_S_L3 + 2333u + 1u <= QR_RING ? 1 : -1];
static const uint16_t QR_S[4] = {QR_S_L0, QR_S_L1, QR_S_L2, QR_S_L3};

enum { QC_CHORUS, QC_REVERB, QC_5EQ, QC_FLANGE, QC_DETUNE, QC_DLYREV };
const char *const DN_FXQ_CFG[DN_FXQ_NCFG] = {"CHORUS", "REVERB", "5EQ>REV", "FLANGE", "DETUNE", "DLY>REV"};

uint32_t dn_fxq_ram(void) { return (QD_LEN + QC_LEN + QR_RING) * 2u; }

const char *dn_fxq_pname(uint32_t cfg, uint32_t i)
{
    static const char *const N[DN_FXQ_NCFG][7] = {
        {"CONFIG", "DECAY", "TIME", "FEEDBACK", "MOD", "TONE", "16-BIT"},
        {"CONFIG", "DECAY", "PREDELAY", "-", "-", "TONE", "16-BIT"},
        {"CONFIG", "DECAY", "LOW", "LO MID", "MID", "HI MID", "HIGH"},
        {"CONFIG", "DECAY", "TIME", "FEEDBACK", "MOD", "TONE", "16-BIT"},
        {"CONFIG", "DECAY", "TIME", "FEEDBACK", "DETUNE", "TONE", "16-BIT"},
        {"CONFIG", "DECAY", "TIME", "FEEDBACK", "REV LVL", "TONE", "16-BIT"},
    };
    return i < 7u ? N[cfg % DN_FXQ_NCFG][i] : "-";
}

void dn_fxq_init(dn_fx_s_t *s)
{
    dn_fxq_t *q = &s->u.q;
    q->dl = (int16_t *)s->mem;
    q->ch = q->dl + QD_LEN;
    q->rv = q->ch + QC_LEN;
}

static int32_t fxq_sine(uint32_t ph)               /* parabolic sine, Q15 */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x;
    return (x * (32767 - ax)) >> 13;
}
/* a line read d samples (Q8) behind w, interpolated */
static float fxq_rd(const int16_t *b, uint32_t mask, uint32_t w, uint32_t d_q8)
{
    uint32_t i = (w - (d_q8 >> 8)) & mask;
    float f = (float)(d_q8 & 255u) * (1.f / 256.f), a = (float)b[i], c = (float)b[(i - 1u) & mask];
    return a + (c - a) * f;
}

void dn_fxq_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm)
{
    dn_fxq_t *q = &s->u.q;
    const uint32_t cfg = p->p[0] % DN_FXQ_NCFG;
    const int grit = cfg != QC_5EQ && p->p[6];
    uint32_t i, k;
    for (k = 0; k < 4u; k++)                        /* (no denormals: the states flushed once a block) */
        q->rlp[k] = q->rlp[k] < 1e-3f && q->rlp[k] > -1e-3f ? 0.f : q->rlp[k],
        q->eq[k] = q->eq[k] < 1e-3f && q->eq[k] > -1e-3f ? 0.f : q->eq[k];
    /* the plate's gains for its RT60 (0.2 .. 6 s), damping from TONE (darker: more) */
    float rt = 0.2f * fm_exp2f((float)p->p[1] * (4.907f / 127.f)), g[4];
    float tone = cfg == QC_5EQ ? 0.f : ((float)p->p[5] - 64.f) * (1.f / 64.f);
    float damp = 0.55f + 0.3f * tone;               /* the lines' low-pass: 0.25 (dark) .. 0.85 */
    for (k = 0; k < 4u; k++)
        g[k] = fm_exp2f(-9.966f * (float)QR_LEN[k] / (rt * 44100.f));
    /* the EQ: tilt (+-6 dB each side of ~800 Hz) or five bands (+-12 dB) */
    float gl = fm_exp2f(-tone), gh = fm_exp2f(tone), b5[5];
    for (k = 0; k < 5u; k++)
        b5[k] = cfg == QC_5EQ ? fm_exp2f(((float)p->p[2u + k] - 64.f) * (2.f / 64.f)) : 1.f;
    /* the delay */
    int del = cfg != QC_REVERB && cfg != QC_5EQ;
    uint32_t dtgt = cfg == QC_REVERB ? ((uint32_t)p->p[2] * 21u) << 8   /* PREDELAY: 0 .. 120 ms (half rate) */
                                     : del ? (dn_fx_sync_samples(p->p[2], bpm) / 2u) << 8 : 0u;
    while ((dtgt >> 8) > QD_LEN - 4u)
        dtgt >>= 1;                                 /* (too long at this tempo: half, on the beat) */
    float dfb = del ? (float)p->p[3] * (0.95f / 127.f) : 0.f;
    float mod = (float)p->p[4] * (1.f / 127.f);
    float rlvl = cfg == QC_DLYREV ? mod * 1.5f : cfg == QC_REVERB || cfg == QC_5EQ ? 1.6f : 0.8f;   /* (the plate's level) */
    float det = cfg == QC_DETUNE ? mod * 0.0173f : 0.f;   /* up to 30 cents */
    if (!q->d_q8)
        q->d_q8 = dtgt;
    for (i = 0; i < n; i++) {
        float x = (float)mono[i] * 0.5f, e, cl = 0.f, cr = 0.f, dout, u, y[4], lp, outl, outr;   /* (0.16.1: in at
                                                     * half, headroom for the chain: it summed to 4x full scale) */
        /* EQ */
        if (cfg == QC_5EQ) {
            static const float C[4] = {0.01415f, 0.05540f, 0.19253f, 0.51060f};   /* 100, 400, 1500, 5000 Hz */
            float l[4];
            for (k = 0; k < 4u; k++) {
                q->eq[k] += C[k] * (x - q->eq[k]);
                l[k] = q->eq[k];
            }
            e = l[0] * b5[0] + (l[1] - l[0]) * b5[1] + (l[2] - l[1]) * b5[2] + (l[3] - l[2]) * b5[3] +
                (x - l[3]) * b5[4];
        } else {
            float st = q->eq[0] += 0.1080f * (x - q->eq[0]);   /* ~800 Hz */
            e = st * gl + (x - st) * gh;
        }
        /* the modulation stage */
        q->ch[q->cw] = (int16_t)dn_fx_c16((int32_t)e + q->cfb);
        if (cfg == QC_CHORUS) {                     /* 7 ms +- up to 4 ms, 0.6 Hz, the sides in quadrature */
            q->lfo += 58434u;
            float dep = mod * 176.f;
            uint32_t a = (uint32_t)((309.f + dep * (float)fxq_sine(q->lfo) * (1.f / 32768.f)) * 256.f);
            uint32_t b = (uint32_t)((309.f + dep * (float)fxq_sine(q->lfo + 0x40000000u) * (1.f / 32768.f)) * 256.f);
            cl = fxq_rd(q->ch, QC_LEN - 1u, q->cw, a);
            cr = fxq_rd(q->ch, QC_LEN - 1u, q->cw, b);
            q->cfb = 0;
        } else if (cfg == QC_FLANGE) {              /* 1 .. 6 ms swept at 0.25 Hz, regenerating */
            q->lfo += 24348u;
            float sw = (float)(fxq_sine(q->lfo) + 32768) * (1.f / 65536.f), sw2 = 1.f - sw;
            float dep = 44.f + mod * 220.f;
            cl = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((2.f + dep * sw) * 256.f));
            cr = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((2.f + dep * sw2) * 256.f));
            q->cfb = (int32_t)(cl * 0.45f);
        } else if (cfg == QC_DETUNE) {              /* a shifter a side: two heads in a 1024-sample window */
            float ofs = (float)(q->sh_c & 0xFFFFFFu) * (1024.f / 16777216.f);
            uint32_t step = (uint32_t)(det * 16384.f);   /* (window fraction a sample, Q24 of 1024 samples) */
            float o2 = ofs + 512.f >= 1024.f ? ofs - 512.f : ofs + 512.f;
            float ga = (ofs < 512.f ? ofs : 1024.f - ofs) * (1.f / 512.f);
            /* up: the heads close in on the write (offset falls); down: the mirror */
            float u1 = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((1032.f - ofs) * 256.f));
            float u2 = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((1032.f - o2) * 256.f));
            float d1 = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((8.f + ofs) * 256.f));
            float d2 = fxq_rd(q->ch, QC_LEN - 1u, q->cw, (uint32_t)((8.f + o2) * 256.f));
            cl = u2 + (u1 - u2) * ga;
            cr = d2 + (d1 - d2) * ga;
            q->sh_c += step;
            q->cfb = 0;
        } else {
            q->cfb = 0;
        }
        q->cw = (q->cw + 1u) & (QC_LEN - 1u);
        float m = e + (cl + cr) * 0.35f;
        /* the delay (half rate) */
        dout = 0.f;
        if (dtgt) {
            if (!q->dph) {
                q->din = (int32_t)m;
            } else {
                if (q->d_q8 < dtgt)
                    q->d_q8 = q->d_q8 + 64u > dtgt ? dtgt : q->d_q8 + 64u;
                else if (q->d_q8 > dtgt)
                    q->d_q8 = q->d_q8 < dtgt + 64u ? dtgt : q->d_q8 - 64u;
                float yd = fxq_rd(q->dl, QD_LEN - 1u, q->dw, q->d_q8);
                float wv = ((float)q->din + m) * 0.5f + yd * dfb;
                q->dy += (int32_t)((wv - (float)q->dy) * 0.6f);   /* darker each repeat */
                {                           /* (written through a soft clip: a high FEEDBACK saturates, never
                                                     * clamps: 0.16.1) */
                    float c = (float)q->dy * (1.f / 21845.f);
                    c = c > 1.5f ? 1.5f : c < -1.5f ? -1.5f : c;
                    q->dl[q->dw] = (int16_t)((c - 0.148148148f * c * c * c) * 21845.f);
                }
                q->dw = (q->dw + 1u) & (QD_LEN - 1u);
                q->dyo = (int32_t)yd;
            }
            q->dph ^= 1u;
            dout = (float)q->dyo;
        }
        /* the plate: (a REVERB: the predelayed send only; otherwise all so far) */
        u = cfg == QC_REVERB ? dout * 0.25f : cfg == QC_5EQ ? m * 0.25f : (m + dout) * 0.15f;
        {
            uint32_t pp = q->rp;
            int16_t *rg = q->rv;
            float dl = (float)rg[(pp + QR_AP1) & QR_MASK], v = u + 0.7f * dl;
            rg[pp & QR_MASK] = (int16_t)dn_fx_c16((int32_t)v);
            u = dl - 0.7f * v;
            dl = (float)rg[(pp + QR_S_AP2 + QR_AP2) & QR_MASK];
            v = u + 0.7f * dl;
            rg[(pp + QR_S_AP2) & QR_MASK] = (int16_t)dn_fx_c16((int32_t)v);
            u = dl - 0.7f * v;
            for (k = 0; k < 4u; k++) {
                float o = (float)rg[(pp + QR_S[k] + QR_LEN[k]) & QR_MASK] * g[k];
                lp = q->rlp[k] += damp * (o - q->rlp[k]);
                y[k] = lp;
            }
            float h0 = (y[0] + y[1] + y[2] + y[3]) * 0.5f, h1 = (y[0] - y[1] + y[2] - y[3]) * 0.5f;
            float h2 = (y[0] + y[1] - y[2] - y[3]) * 0.5f, h3 = (y[0] - y[1] - y[2] + y[3]) * 0.5f;
            rg[(pp + QR_S_L0) & QR_MASK] = (int16_t)dn_fx_c16((int32_t)(u + h0));
            rg[(pp + QR_S_L1) & QR_MASK] = (int16_t)dn_fx_c16((int32_t)(-u + h1));
            rg[(pp + QR_S_L2) & QR_MASK] = (int16_t)dn_fx_c16((int32_t)(u + h2));
            rg[(pp + QR_S_L3) & QR_MASK] = (int16_t)dn_fx_c16((int32_t)(-u + h3));
            q->rp = (pp - 1u) & QR_MASK;
            outl = (y[0] - y[2] + y[1]) * rlvl;
            outr = (y[1] - y[3] + y[2]) * rlvl;
        }
        if (cfg != QC_REVERB) {                     /* (REVERB: its predelayed send is the plate's alone) */
            outl += cl * 0.5f + dout;
            outr += cr * 0.5f + dout;
        }
        if (grit) {                                 /* 16-BIT: held over pairs, cut to 12 bits */
            if (!q->gph)
                q->gh_l = (int32_t)outl & ~15, q->gh_r = (int32_t)outr & ~15;
            q->gph ^= 1u;
            wl[i] = q->gh_l;
            wr[i] = q->gh_r;
        } else {
            wl[i] = (int32_t)outl;
            wr[i] = (int32_t)outr;
        }
    }
}
