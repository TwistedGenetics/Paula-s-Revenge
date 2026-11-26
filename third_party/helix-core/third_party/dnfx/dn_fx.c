/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the FX rack's effects (see dn_fx.h): the list, their values, the send limiter, DELAY. Copyright (C)
 * 2026 Jamie (TwistedGenetics). */
#include "dn_fx.h"
#include "dn_dsp.h"

#define DN_DLY_LEN 24576u                           /* DELAY: half-rate samples a line, 1.11 s */

const char *const DN_FX_NAME[DN_FX_N] = {"OFF", "DELAY", "TAPE", "BBD", "SPRING", "TOMB", "MOD", "CELESTIAL",
                                         "DOLMEN", "TUMULUS", "QUADRA", "MASSIVE", "FILTER", "PHASER", "CHORUS",
                                         "CRUSH", "RING", "DRIVE"};
const char *const DN_FX_DIV_NAME[DN_FX_NDIV] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4",
                                                "1/2T", "1/4D", "1/2"};
const uint8_t DN_FX_DIV[DN_FX_NDIV][2] = {{1, 8}, {1, 6}, {1, 4}, {1, 3}, {3, 8}, {1, 2}, {2, 3}, {3, 4}, {1, 1},
                                          {4, 3}, {3, 2}, {2, 1}};

static int dn_fx_isrev(uint32_t t) { return t >= DN_FX_SPRING && t <= DN_FX_TUMULUS; }

uint32_t dn_fx_ram(uint32_t type)
{
    if (type == DN_FX_DELAY)
        return 2u * DN_DLY_LEN * sizeof(int16_t);
    if (type == DN_FX_QUADRA)
        return dn_fxq_ram();
    if (type == DN_FX_MASSIVE)
        return dn_fxm_ram();
    if (type >= DN_FX_FILTER)
        return dn_fxl_ram(type);
    return dn_fxc_ram(type);
}
int dn_fx_flex(uint32_t type) { return type == DN_FX_MASSIVE; }

uint32_t dn_fx_np(uint32_t type)
{
    return type == DN_FX_OFF ? 0u : type >= DN_FX_FILTER ? dn_fxl_np(type) : type == DN_FX_DELAY ? 3u
         : type <= DN_FX_BBD || dn_fx_isrev(type) ? 4u : 7u;
}

void dn_fx_default(dn_fx_p_t *p, uint32_t type)
{
    static const uint8_t D[DN_FX_FILTER][DN_FX_NP] = {
        {0},
        {7, 60, 70},                                /* DELAY: 1/8D, the jungle dub default */
        {7, 70, 36, 1},                             /* TAPE: 1/8D, ping-pong, Carnac's wear */
        {7, 70, 36, 1},                             /* BBD */
        {60, 40, 0, 30},                            /* SPRING */
        {70, 50, 10, 30},                           /* TOMB */
        {80, 50, 10, 30},                           /* MOD */
        {90, 40, 20, 40},                           /* CELESTIAL */
        {64, 30, 0, 30},                            /* DOLMEN */
        {90, 70, 20, 30},                           /* TUMULUS */
        {0, 70, 7, 40, 40, 64, 0},                  /* QUADRA: CHO-DLY-REV */
        {1, 89, 96, 108, 120, 50, 54},              /* MASSIVE: HALL, 250 ms, long, smooth, a little dark */
    };
    uint32_t i;
    p->type = (uint8_t)(type < DN_FX_N ? type : 0u);
    if (p->type >= DN_FX_FILTER) {
        dn_fxl_default(p);
        return;
    }
    for (i = 0; i < DN_FX_NP; i++)
        p->p[i] = D[p->type][i];
}

void dn_fx_value(dn_fx_p_t *p, uint32_t i, uint32_t v)
{
    /* QUADRA: a new CONFIG starts from its own values (a 5EQ>REV from the CHORUS's TIME and FEEDBACK as EQ bands was
     * a quiet, lopsided plate) */
    static const uint8_t QD[DN_FXQ_NCFG][5] = {{7, 40, 40, 64, 0}, {20, 0, 0, 64, 0}, {64, 64, 64, 64, 64},
                                               {7, 40, 60, 64, 0}, {7, 30, 50, 64, 0}, {7, 50, 64, 64, 0}};
    uint32_t k;
    if (i >= DN_FX_NP)
        return;
    if (p->type == DN_FX_QUADRA && i == 0u && v % DN_FXQ_NCFG != p->p[0] % DN_FXQ_NCFG)
        for (k = 0; k < 5u; k++)
            p->p[2u + k] = QD[v % DN_FXQ_NCFG][k];
    p->p[i] = (uint8_t)v;
}

const char *dn_fx_pname(const dn_fx_p_t *p, uint32_t i)
{
    static const char *const DL[3] = {"TIME", "FEEDBACK", "TONE"};
    static const char *const TP[4] = {"TIME", "FEEDBACK", "DEGRADE", "PINGPONG"};
    static const char *const RV[4] = {"TIME", "DAMP", "PREDELAY", "LOW CUT"};
    static const char *const MS[7] = {"MODE", "TIME", "FEEDBACK", "WARP", "DENSITY", "MOD", "TONE"};
    uint32_t t = p->type;
    if (i >= dn_fx_np(t))
        return "-";
    if (t >= DN_FX_FILTER)
        return dn_fxl_pname(t, i);
    if (t == DN_FX_DELAY)
        return DL[i];
    if (t == DN_FX_TAPE || t == DN_FX_BBD)
        return TP[i];
    if (dn_fx_isrev(t))
        return RV[i];
    if (t == DN_FX_QUADRA)
        return dn_fxq_pname(p->p[0], i);
    return MS[i];
}

int32_t dn_fx_pmax(const dn_fx_p_t *p, uint32_t i)
{
    uint32_t t = p->type;
    const char *n = dn_fx_pname(p, i);
    if (n[0] == '-')
        return 0;
    if (t >= DN_FX_FILTER)
        return dn_fxl_pmax(t, i);
    if (t == DN_FX_DELAY || t == DN_FX_TAPE || t == DN_FX_BBD)
        return i == 0u ? (int32_t)DN_FX_NDIV - 1 : i == 3u ? 1 : 127;
    if (t == DN_FX_QUADRA) {
        if (i == 0u)
            return DN_FXQ_NCFG - 1;
        if (n[0] == 'T' && n[1] == 'I')             /* TIME */
            return DN_FX_NDIV - 1;
        if (n[0] == '1')                            /* 16-BIT */
            return 1;
        return 127;
    }
    if (t == DN_FX_MASSIVE)
        return i == 0u ? (int32_t)DN_FXM_NMODE - 1 : 127;
    return 127;
}

uint32_t dn_fx_sync_samples(uint32_t div, uint32_t bpm)
{
    const uint8_t *d = DN_FX_DIV[div < DN_FX_NDIV ? div : 8u];
    return 2646000u * d[0] / ((bpm ? bpm : 120u) * d[1]);   /* 44100 x 60 x beats / bpm */
}

static void dn_fx_str(char out[8], const char *w)
{
    uint32_t i;
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void dn_fx_num(char out[8], int32_t v, const char *unit)
{
    char t[12];
    uint32_t n = 0, i = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do
        t[n++] = (char)('0' + u % 10u), u /= 10u;
    while (u && n < 10u);
    if (v < 0)
        out[i++] = '-';
    while (n && i < 7u)
        out[i++] = t[--n];
    while (*unit && i < 7u)
        out[i++] = *unit++;
    out[i] = 0;
}

void dn_fx_ptext(const dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t i, uint32_t bpm, char out[8])
{
    uint32_t t = p->type, v = i < DN_FX_NP ? p->p[i] : 0u;
    const char *n = dn_fx_pname(p, i);
    if (n[0] == '-') {
        dn_fx_str(out, "-");
        return;
    }
    if (dn_fxl_ptext(t, i, v, out))
        return;
    if ((t == DN_FX_DELAY || t == DN_FX_TAPE || t == DN_FX_BBD) && i == 0u) {
        dn_fx_str(out, DN_FX_DIV_NAME[v < DN_FX_NDIV ? v : 8u]);
        return;
    }
    if ((t == DN_FX_TAPE || t == DN_FX_BBD) && i == 3u) {
        dn_fx_str(out, v ? "ON" : "OFF");
        return;
    }
    if (dn_fx_isrev(t) && i == 2u) {                /* PREDELAY: 35 samples a step */
        dn_fx_num(out, (int32_t)(v * 35u * 10u / 441u), "ms");
        return;
    }
    if (dn_fx_isrev(t) && i == 3u) {                /* LOW CUT: off, 30 Hz .. 800 Hz */
        if (!v)
            dn_fx_str(out, "OFF");
        else
            dn_fx_num(out, (int32_t)(30u + v * v * 770u / 16129u), "Hz");
        return;
    }
    if (t == DN_FX_QUADRA) {
        if (i == 0u)
            dn_fx_str(out, DN_FXQ_CFG[v % DN_FXQ_NCFG]);
        else if (n[0] == 'T' && n[1] == 'I')
            dn_fx_str(out, DN_FX_DIV_NAME[v < DN_FX_NDIV ? v : 8u]);
        else if (n[0] == '1')
            dn_fx_str(out, v ? "ON" : "OFF");
        else if ((n[0] == 'T' && n[1] == 'O') || (n[0] == 'L' && n[1] == 'O') || (n[0] == 'M' && n[1] == 'I') ||
                 (n[0] == 'H' && n[1] == 'I'))      /* TONE and the EQ's bands: about the middle */
            dn_fx_num(out, (int32_t)v - 64, "");
        else
            dn_fx_num(out, (int32_t)v, "");
        return;
    }
    if (t == DN_FX_MASSIVE) {
        if (i == 0u)
            dn_fx_str(out, DN_FXM_MODE[v % DN_FXM_NMODE]);
        else if (i == 1u) {
            static const char *const SD[13] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
                                               "1/4", "1/2T", "1/4D", "1/2", "1/2D"};
            float ms = dn_fxm_time_ms(s, p, bpm), nom = v < 13u ? DN_FXM_BEATS[v] * 60000.f / (float)(bpm ? bpm : 120u) : 0.f;
            if (v < 13u && ms > nom - 1.f && ms < nom + 1.f) {
                dn_fx_str(out, SD[v]);              /* (synced: the division; halved to fit: in ms) */
            } else {
                dn_fx_num(out, (int32_t)(ms + 0.5f), "ms");
            }
        } else if (i == 6u)
            dn_fx_num(out, (int32_t)v - 64, "");
        else
            dn_fx_num(out, (int32_t)v, "");
        return;
    }
    dn_fx_num(out, (int32_t)v, "");
}

void dn_fx_init(dn_fx_s_t *s, uint32_t type, void *mem, uint32_t bytes)
{
    uint32_t i, n = bytes / 4u;
    uint32_t *m = (uint32_t *)mem;
    uint8_t *z = (uint8_t *)&s->u;
    for (i = 0; m && i < n; i++)
        m[i] = 0;
    for (i = 0; i < sizeof s->u; i++)
        z[i] = 0;
    s->type = (uint8_t)type;
    s->mem = mem;
    s->bytes = mem ? bytes : 0u;
    s->lim = 32767.f;
    if (!mem || type == DN_FX_OFF) {
        s->mem = 0;
        return;
    }
    if (type == DN_FX_DELAY) {
        s->u.d.l = (int16_t *)mem;
        s->u.d.r = s->u.d.l + DN_DLY_LEN;
        s->u.d.len = DN_DLY_LEN;
    } else if (type == DN_FX_QUADRA) {
        dn_fxq_init(s);
    } else if (type == DN_FX_MASSIVE) {
        dn_fxm_init(s);
    } else if (type >= DN_FX_FILTER) {
        dn_fxl_init(s);
    } else {
        dn_fxc_init(s);
    }
}

uint32_t dn_fx_delay_samples(const dn_fx_p_t *p, uint32_t bpm)
{
    uint32_t n = dn_fx_sync_samples(p->p[0], bpm);
    return n > 2u * (DN_DLY_LEN - 4u) ? 2u * (DN_DLY_LEN - 4u) : n;
}

/* the line at d (half-rate samples << 8) behind the write point, linearly interpolated */
static int32_t dn_tap(const int16_t *b, uint32_t len, uint32_t w, uint32_t d_q8)
{
    uint32_t di = d_q8 >> 8, f = d_q8 & 255u, i0 = w >= di ? w - di : w + len - di, i1 = i0 ? i0 - 1u : len - 1u;
    return b[i0] + (((b[i1] - b[i0]) * (int32_t)f) >> 8);
}

static inline int32_t dn_fxd_z12(int32_t t) { return (t + ((t >> 31) & 4095)) >> 12; }
/* DELAY (0.12): two lines at half rate, the input into the left, the left's output into the right, the right's back
 * into the left at FEEDBACK (100 = unity; above it builds up, soft-clipped so it howls rather than wraps), a low-pass
 * in the loop (TONE). A new TIME glides there (tape-like), never clicks. */
static void dn_fxd_run(dn_fxd_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                       uint32_t bpm)
{
    uint32_t i, tgt = (dn_fx_delay_samples(p, bpm) / 2u) << 8;
    int32_t fb = (int32_t)p->p[1] * 32768 / 100;    /* Q15: 100 = unity */
    int32_t a = 150 + (int32_t)p->p[2] * 3946 / 127;   /* the loop's low-pass, Q12 */
    if (!s->d_q8)
        s->d_q8 = tgt;
    for (i = 0; i < n; i++) {
        int32_t x = mono[i];
        if (!s->ph) {
            s->in_acc = x;
        } else {                                    /* the pair: the lines move on one (half-rate) sample */
            int32_t xi = dn_clamp((s->in_acc + x) >> 1, -65536, 65536), fl, fr;
            if (s->d_q8 < tgt)                      /* TIME glides: a quarter sample a step, like tape */
                s->d_q8 = s->d_q8 + 64u > tgt ? tgt : s->d_q8 + 64u;
            else if (s->d_q8 > tgt)
                s->d_q8 = s->d_q8 < tgt + 64u ? tgt : s->d_q8 - 64u;
            s->y_l = dn_tap(s->l, s->len, s->w, s->d_q8);
            s->y_r = dn_tap(s->r, s->len, s->w, s->d_q8);
            fl = xi + dn_fx_mz(s->y_r, fb);         /* ping-pong: in and the right's repeat into the left */
            fr = s->y_l;                            /* the left's into the right */
            s->lp_l = fl + dn_fxd_z12((s->lp_l - fl) * (4096 - a));   /* (0.16: the state's distance from its
                                                     * input rounded toward zero: a tail ends in silence) */
            s->lp_r = fr + dn_fxd_z12((s->lp_r - fr) * (4096 - a));
            s->l[s->w] = (int16_t)dn_softclip(s->lp_l);
            s->r[s->w] = (int16_t)dn_clamp(s->lp_r, -32767, 32767);
            s->w = s->w + 1u >= s->len ? 0u : s->w + 1u;
        }
        s->ph ^= 1u;
        s->o_l += (s->y_l - s->o_l) / 2;            /* (the held pair, softened) */
        s->o_r += (s->y_r - s->o_r) / 2;
        wl[i] = s->o_l;
        wr[i] = s->o_r;
    }
}

/* (Paula's Revenge's copy) the send limiter, written afresh: each block's loudest sample sets a gain that brings it
 * back to full scale, and the gain comes back up slowly (about a tenth of a second) when the peaks fall. */
#define DN_LIM_FULL 32767.f
static void dn_fx_limit(float *gainp, const int32_t *in, int32_t *st, int32_t *mono, uint32_t n)
{
    float peak = 0.f, g = *gainp <= 0.f || *gainp > 1.f ? 1.f : *gainp, want;
    uint32_t i;
    for (i = 0; i < 2u * n; i++) { float a = (float)(in[i] < 0 ? -in[i] : in[i]); if (a > peak) peak = a; }
    want = peak > DN_LIM_FULL ? DN_LIM_FULL / peak : 1.f;
    for (i = 0; i < n; i++) {
        g = want < g ? want : g + (want - g) * 0.00025f;
        st[2u * i] = dn_fx_c16((int32_t)((float)in[2u * i] * g));
        st[2u * i + 1u] = dn_fx_c16((int32_t)((float)in[2u * i + 1u] * g));
        mono[i] = (st[2u * i] + st[2u * i + 1u]) >> 1;
    }
    *gainp = g;
}

void dn_fx_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *in, int32_t *out, uint32_t n, uint32_t bpm,
               uint32_t ret)
{
    int32_t st[2u * DN_FX_BLK], mono[DN_FX_BLK], wl[DN_FX_BLK], wr[DN_FX_BLK];
    int32_t g = (int32_t)ret * 32768 / 100;     /* (32-bit: no 64-bit divide on the FM-1) */
    uint32_t i, k;
    if (s->type == DN_FX_OFF || !s->mem || p->type != s->type)
        return;
    while (n) {
        k = n < DN_FX_BLK ? n : DN_FX_BLK;
        dn_fx_limit(&s->lim, in, st, mono, k);
        if (s->type == DN_FX_DELAY)
            dn_fxd_run(&s->u.d, p, mono, wl, wr, k, bpm);
        else if (s->type == DN_FX_QUADRA)
            dn_fxq_run(s, p, mono, wl, wr, k, bpm);
        else if (s->type == DN_FX_MASSIVE)
            dn_fxm_run(s, p, st, wl, wr, k, bpm);
        else if (s->type >= DN_FX_FILTER)
            dn_fxl_run(s, p, st, wl, wr, k, bpm);
        else
            dn_fxc_run(s, p, mono, wl, wr, k, bpm);
        for (i = 0; i < k; i++) {
            out[2u * i] += (int32_t)(((int64_t)wl[i] * g) >> 15);
            out[2u * i + 1u] += (int32_t)(((int64_t)wr[i] * g) >> 15);
        }
        in += 2u * k;
        out += 2u * k;
        n -= k;
    }
}
