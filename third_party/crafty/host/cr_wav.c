/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: wavetables from WAV files (see cr_wav.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_wav.h"

static uint32_t wv_u16(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8); }
static uint32_t wv_u32(const uint8_t *b) { return wv_u16(b) | (wv_u16(b + 2) << 16); }
static int wv_tag(const uint8_t *b, const char *t) { return b[0] == (uint8_t)t[0] && b[1] == (uint8_t)t[1] && b[2] == (uint8_t)t[2] && b[3] == (uint8_t)t[3]; }

/* cos and sin of 2 pi num / den (num < den), to float's precision: the quadrant, then the series */
static void wv_sincos(uint32_t num, uint32_t den, float *c, float *s)
{
    uint32_t q = (4u * num) / den;
    float x = (float)(4u * num - q * den) / (float)den * 1.57079633f, y, x2, cx, sx;
    int swap = x > 0.785398163f;
    if (swap)
        x = 1.57079633f - x;
    x2 = x * x;
    sx = x * (1.f - x2 * (1.f / 6.f - x2 * (1.f / 120.f - x2 * (1.f / 5040.f - x2 * (1.f / 362880.f)))));
    cx = 1.f - x2 * (0.5f - x2 * (1.f / 24.f - x2 * (1.f / 720.f - x2 * (1.f / 40320.f - x2 * (1.f / 3628800.f)))));
    if (swap) {
        y = sx;
        sx = cx;
        cx = y;
    }
    switch (q & 3u) {
    case 0: *c = cx; *s = sx; break;
    case 1: *c = -sx; *s = cx; break;
    case 2: *c = -cx; *s = -sx; break;
    default: *c = sx; *s = -cx; break;
    }
}

int cr_wav_open(cr_wav_t *w, cr_wav_read_t read, void *ctx, uint32_t size)
{
    uint8_t h[40];
    uint32_t o = 12, n, step, clm = 0;
    int fmt = 0, data = 0;
    w->read = read;
    w->ctx = ctx;
    w->size = size;
    if (size < 12u || read(ctx, 0, h, 12) != 12u || !wv_tag(h, "RIFF") || !wv_tag(h + 8, "WAVE"))
        return 0;
    while (o + 8u <= size) {
        uint32_t len, body = o + 8u;
        if (read(ctx, o, h, 8) != 8u)
            break;
        len = wv_u32(h + 4);
        if (wv_tag(h, "fmt ")) {
            uint32_t m = len < 40u ? len : 40u;
            if (m < 16u || read(ctx, body, h, m) != m)
                return 0;
            w->format = (uint16_t)wv_u16(h);
            w->ch = (uint16_t)wv_u16(h + 2);
            w->bits = (uint16_t)wv_u16(h + 14);
            if (w->format == 0xFFFEu && m >= 26u)           /* WAVE_FORMAT_EXTENSIBLE: its sub-format */
                w->format = (uint16_t)wv_u16(h + 24);
            fmt = 1;
        } else if (wv_tag(h, "data")) {
            w->data_off = body;
            w->data_len = len < size - body ? len : size - body;
            data = 1;
        } else if (wv_tag(h, "clm ")) {                     /* Serum: "<!>2048 ..." */
            uint32_t m = len < 16u ? len : 16u, i;
            if (read(ctx, body, h, m) == m)
                for (i = 0; i + 3u < m; i++)
                    if (h[i] == '<' && h[i + 1] == '!' && h[i + 2] == '>') {
                        for (i += 3; i < m && h[i] >= '0' && h[i] <= '9'; i++)
                            clm = clm * 10u + (uint32_t)(h[i] - '0');
                        break;
                    }
        }
        if (len > size - body)
            break;
        o = body + len + (len & 1u);
    }
    if (!fmt || !data || !w->ch)
        return 0;
    if (!(w->format == 1u && (w->bits == 8u || w->bits == 16u || w->bits == 24u || w->bits == 32u)) &&
        !(w->format == 3u && w->bits == 32u))
        return 0;
    step = (uint32_t)(w->bits / 8u) * w->ch;
    n = w->data_len / step;
    w->frame_size = clm ? clm : CR_WAV_FRAME;
    if (n < w->frame_size)
        w->frame_size = n;                                  /* one cycle, whatever its length */
    if (w->frame_size < 8u || w->frame_size > CR_WAV_MAXFRAME)
        return 0;
    w->frames = n / w->frame_size;
    if (!w->frames)
        w->frames = 1;
    return 1;
}

void cr_wav_picks(uint32_t frames, uint32_t picks[5])
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        picks[k] = (frames ? 2u * k * (frames - 1u) + 4u : 0u) / 8u;   /* round(k (frames - 1) / 4) */
}

/* frame f's first channel into k->frame */
static int wv_frame(const cr_wav_t *w, uint32_t f, cr_wav_work_t *k)
{
    uint32_t bps = w->bits / 8u, step = bps * w->ch, per = sizeof k->io / step, done = 0;
    uint32_t off = w->data_off + f * w->frame_size * step;
    while (done < w->frame_size) {
        uint32_t want = w->frame_size - done < per ? w->frame_size - done : per, i;
        if (w->read(w->ctx, off + done * step, k->io, want * step) != want * step)
            return 0;
        for (i = 0; i < want; i++) {
            const uint8_t *p = k->io + i * step;
            float x;
            if (w->format == 3u) {
                union { uint32_t u; float f; } c;
                c.u = wv_u32(p);
                x = c.f;
            } else if (w->bits == 16u)
                x = (float)(int16_t)wv_u16(p) * (1.f / 32768.f);
            else if (w->bits == 24u)
                x = (float)((int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8) * (1.f / 8388608.f);
            else if (w->bits == 32u)
                x = (float)(int32_t)wv_u32(p) * (1.f / 2147483648.f);
            else
                x = (float)((int32_t)p[0] - 128) * (1.f / 128.f);
            if (!(x == x))                                  /* NaN */
                x = 0.f;
            k->frame[done + i] = x;
        }
        done += want;
    }
    return 1;
}

/* the frame (any length) to 256 samples with at most 128 harmonics, DC removed */
static void wv_resample(uint32_t L, const cr_wav_work_t *k, float *out)
{
    uint32_t h, i;
    for (i = 0; i < CR_WT_N; i++)
        out[i] = 0.f;
    for (h = 1; h <= 128u && 2u * h < L; h++) {
        float re = 0.f, im = 0.f;
        uint32_t idx = 0, j = 0;
        for (i = 0; i < L; i++) {
            re += k->frame[i] * k->cs[idx];
            im += k->frame[i] * k->sn[idx];
            idx += h;
            if (idx >= L)
                idx -= L;
        }
        re *= 2.f / (float)L;
        im *= 2.f / (float)L;
        if (h == 128u) {                                    /* Nyquist at 256: its cosine, half of it */
            re *= 0.5f;
            im = 0.f;
        }
        if (re * re + im * im < 1e-12f)
            continue;
        for (i = 0; i < CR_WT_N; i++) {
            out[i] += re * k->c256[j] + im * k->s256[j];
            j = (j + h) & (CR_WT_N - 1u);
        }
    }
}

int cr_wav_bank(const cr_wav_t *w, const uint32_t picks[5], int16_t out[5][CR_WT_N], cr_wav_work_t *k)
{
    uint32_t i, f, pk[5];
    float peak = 0.f, g;
    if (!picks) {
        cr_wav_picks(w->frames, pk);
        picks = pk;
    }
    for (i = 0; i < w->frame_size; i++)
        wv_sincos(i, w->frame_size, &k->cs[i], &k->sn[i]);
    for (i = 0; i < CR_WT_N; i++)
        wv_sincos(i, CR_WT_N, &k->c256[i], &k->s256[i]);
    for (f = 0; f < 5u; f++) {
        if (!wv_frame(w, picks[f] < w->frames ? picks[f] : w->frames - 1u, k))
            return 0;
        wv_resample(w->frame_size, k, k->wave[f]);
    }
    for (f = 0; f < 5u; f++)
        for (i = 0; i < CR_WT_N; i++) {
            float a = k->wave[f][i] < 0.f ? -k->wave[f][i] : k->wave[f][i];
            if (a > peak)
                peak = a;
        }
    if (!(peak > 1e-9f))
        return 0;
    g = (float)CR_WAV_PEAK / peak;
    for (f = 0; f < 5u; f++)
        for (i = 0; i < CR_WT_N; i++) {
            float v = k->wave[f][i] * g;
            out[f][i] = (int16_t)(v < 0.f ? v - 0.5f : v + 0.5f);
        }
    return 1;
}

void cr_wav_name(const char *file, char name[14])
{
    uint32_t n = 0, c = 0, i;
    while (file[n])
        n++;
    if (n > 4u && file[n - 4] == '.' && (file[n - 3] | 32) == 'w' && (file[n - 2] | 32) == 'a' && (file[n - 1] | 32) == 'v')
        n -= 4u;
    for (i = 0; i < n && c < 13u; i++) {
        char ch = file[i];
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        if (ch >= ' ' && ch <= '_')
            name[c++] = ch;
    }
    while (c < 14u)
        name[c++] = 0;
}

void cr_ubank_make(cr_ubank_t *b, const char *name, const int16_t w[5][CR_WT_N])
{
    uint8_t *p = (uint8_t *)b;
    uint32_t i, j;
    for (i = 0; i < sizeof *b; i++)
        p[i] = 0;
    b->magic = CR_UBANK_MAGIC;
    b->ver = 1u;
    for (i = 0; i < 13u && name[i]; i++)
        b->name[i] = name[i];
    for (i = 0; i < 5u; i++)
        for (j = 0; j < CR_WT_N; j++)
            b->w[i][j] = w[i][j];
}
