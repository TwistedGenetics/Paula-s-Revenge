/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the engine as one instance (cr_inst.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_inst.h"
#include "cr_presets.h"

static void zero(void *d, uint32_t n)
{
    uint8_t *b = d;
    while (n--)
        *b++ = 0;
}
static void copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
}
#define INST_OF(a) ((cr_inst_t *)(a)->user)

static void a_on(cr_arp_t *a, uint32_t n, uint32_t v) { cr_synth_on(&INST_OF(a)->syn, n, v); }
static void a_off(cr_arp_t *a, uint32_t n) { cr_synth_off(&INST_OF(a)->syn, n); }
static const cr_arp_out_t AOUT = {a_on, a_off};

/* ---- the user area ---- */
int cr_user_saved(const uint8_t *user, uint32_t slot)
{
    const uint8_t *r;
    if (!user || slot < CR_NFACTORY || slot >= CR_NPRESET)
        return 0;
    r = user + CR_FL_PRESETS + (slot - CR_NFACTORY) * CR_PATCH_BYTES;
    return r[109] == CR_PATCH_MAGIC && r[110] == CR_PATCH_VER;
}
int cr_preset_get(const uint8_t *user, uint32_t slot, cr_patch_t *out)
{
    slot %= CR_NPRESET;
    if (slot >= CR_NFACTORY) {
        if (cr_user_saved(user, slot)) {
            copy(out, user + CR_FL_PRESETS + (slot - CR_NFACTORY) * CR_PATCH_BYTES, CR_PATCH_BYTES);
            if (cr_patch_check(out) == 0)
                return 1;
        }
        cr_patch_init(out, "INIT");
        return 0;
    }
    copy(out, CR_FACTORY[slot], CR_PATCH_BYTES);
    (void)cr_patch_check(out);
    return 1;
}
const cr_ubank_t *cr_user_bank(const uint8_t *user, uint32_t u)
{
    const cr_ubank_t *b;
    if (!user || u >= CR_NUBANK)
        return 0;
    b = (const cr_ubank_t *)(const void *)(user + CR_FL_BANKS + u * CR_SECTOR);
    return b->magic == CR_UBANK_MAGIC && b->ver == 1u ? b : 0;
}
void cr_preset_label(uint32_t slot, char *b)
{
    uint32_t k = 0, bank;
    slot %= CR_NPRESET;
    if (slot >= CR_NFACTORY) {
        b[k++] = 'U';
        slot -= CR_NFACTORY;
    }
    bank = slot / 8u + 1u;
    if (bank >= 10u)
        b[k++] = (char)('0' + bank / 10u);
    b[k++] = (char)('0' + bank % 10u);
    b[k++] = '-';
    b[k++] = (char)('1' + slot % 8u);
    b[k] = 0;
}

/* ---- the wavetables: the two banks the patch names, built when they change ---- */
static const int16_t (*bank_base(const cr_inst_t *in, uint32_t b))[CR_WT_N]
{
    if (b >= CR_NBANK) {
        const cr_ubank_t *u = cr_user_bank(in->user, b - CR_NBANK);
        return u ? (const int16_t (*)[CR_WT_N])u->w : cr_wt_factory(0);   /* an empty user bank plays bank 1 */
    }
    return cr_wt_factory(b);
}
static void tables_ex(cr_inst_t *in, int defer)
{
    uint32_t want[2], k;
    want[0] = cr_bank_of(cr_synth_eff(&in->syn, CR_W1BANK), 0);   /* the banks as the matrix leaves them */
    want[1] = cr_bank_of(cr_synth_eff(&in->syn, CR_W2BANK), 1);
    for (k = 0; k < 2u; k++) {
        if (in->file[k]) {                      /* a wavetable file: no bank, the matrix can't move it */
            if (in->tb[k] != 0xFEu || in->tb_gen[k] != in->file_gen[k]) {
                if (defer && in->tb[k] != 0xFFu) {
                    in->tb_pending = 1;
                    continue;
                }
                cr_wt_build(&in->tab[k], in->file[k]);
                in->tb[k] = 0xFEu;
                in->tb_gen[k] = in->file_gen[k];
            }
            continue;
        }
        if (in->tb[k] != want[k]) {
            if (defer && in->tb[k] != 0xFFu) {  /* the old bank plays on until the host builds this one */
                in->tb_pending = 1;
                continue;
            }
            cr_wt_build(&in->tab[k], bank_base(in, want[k]));
            in->tb[k] = (uint8_t)want[k];
        }
    }
    cr_synth_tables(&in->syn, &in->tab[0], &in->tab[1], in->tb[1] == CR_BANK_NOISE);   /* the bank that plays */
}
static void tables(cr_inst_t *in)
{
    tables_ex(in, in->defer_tables);
}
int cr_inst_tables_pending(const cr_inst_t *in)
{
    return in->tb_pending;
}
void cr_inst_tables(cr_inst_t *in)
{
    in->tb_pending = 0;
    tables_ex(in, 0);
}
void cr_inst_file_table(cr_inst_t *in, uint32_t k, const int16_t (*base)[CR_WT_N])
{
    if (k > 1u)
        return;
    in->file[k] = base;
    in->file_gen[k]++;
    if (!base && in->tb[k] == 0xFEu)
        in->tb[k] = 0xFDu;                      /* no bank is built: build the patch's again */
    in->tb_pending = 0;
    tables_ex(in, 0);
}

void cr_inst_init(cr_inst_t *in)
{
    zero(in, sizeof *in);
    cr_synth_init(&in->syn, in->mem);
    cr_arp_init(&in->arp, &AOUT);
    in->arp.user = in;
    cr_globals_init(&in->g);
    in->tb[0] = in->tb[1] = 0xFFu;
    in->ppq = -1.0;
    cr_inst_load(in, 0);
}
void cr_inst_set_user(cr_inst_t *in, const uint8_t *user)
{
    in->user = user;
    cr_inst_user_changed(in);
}
void cr_inst_user_changed(cr_inst_t *in)
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        if (in->tb[k] != 0xFFu && in->tb[k] >= CR_NBANK)
            in->tb[k] = 0xFFu;
    tables(in);
}

/* ---- patches and values ---- */
void cr_inst_use(cr_inst_t *in, const cr_patch_t *p)
{
    in->p = *p;
    (void)cr_patch_check(&in->p);
    cr_synth_patch(&in->syn, &in->p);
    if (in->p.p[CR_ARP] < 64u)
        cr_arp_release_all(&in->arp);
    tables(in);
    in->patch_changed = 1;
}
void cr_inst_load(cr_inst_t *in, uint32_t slot)
{
    cr_patch_t t;
    (void)cr_preset_get(in->user, slot, &t);
    in->cur = (uint16_t)(slot % CR_NPRESET);
    in->dirty = 0;
    cr_inst_use(in, &t);
}
void cr_inst_param(cr_inst_t *in, uint32_t id, uint32_t v)
{
    if (id >= CR_NP)
        return;
    v &= 0x7Fu;
    if (in->p.p[id] == v)
        return;
    in->p.p[id] = (uint8_t)v;
    cr_synth_param(&in->syn, id, v);
    if (id == CR_W1BANK || id == CR_W2BANK)
        tables(in);
    if (id == CR_ARP && v < 64u)
        cr_arp_release_all(&in->arp);
    in->dirty = 1;
}

/* ---- notes ---- */
static void play(cr_inst_t *in, uint32_t n, uint32_t v)
{
    if (cr_synth_eff(&in->syn, CR_ARP) >= 64u)
        cr_arp_key(&in->arp, n, v);
    else if (v)
        cr_synth_on(&in->syn, n, v);
    else
        cr_synth_off(&in->syn, n);
}
void cr_inst_note(cr_inst_t *in, uint32_t n, uint32_t v)
{
    n &= 0x7Fu;
    if (!v && in->sus) {
        in->sus_bits[n >> 5] |= 1u << (n & 31u);
        return;
    }
    if (v)
        in->sus_bits[n >> 5] &= ~(1u << (n & 31u));
    play(in, n, v);
}
void cr_inst_all_off(cr_inst_t *in)
{
    in->sus = 0;
    in->sus_bits[0] = in->sus_bits[1] = in->sus_bits[2] = in->sus_bits[3] = 0;
    cr_arp_release_all(&in->arp);
    cr_synth_all_off(&in->syn);
}
void cr_inst_midi(cr_inst_t *in, const uint8_t *m, uint32_t len)
{
    uint32_t t, d1, d2;
    int p;
    if (len < 1u)
        return;
    t = m[0] & 0xF0u;
    d1 = len > 1u ? m[1] & 0x7Fu : 0u;
    d2 = len > 2u ? m[2] & 0x7Fu : 0u;
    switch (t) {
    case 0x90u:
        cr_inst_note(in, d1, d2 ? (in->g.velo ? d2 : 100u) : 0u);
        break;
    case 0x80u:
        cr_inst_note(in, d1, 0);
        break;
    case 0xE0u:
        cr_synth_bend(&in->syn, (float)(((int32_t)d2 << 7 | (int32_t)d1) - 8192) * (float)in->g.bend / 8192.f);
        break;
    case 0xD0u:
        cr_synth_ctl(&in->syn, CR_S_AFTT, (float)d1 / 127.f);
        break;
    case 0xC0u:
        cr_inst_load(in, d1);                    /* 0-63 the factory presets, 64-127 U1-1 .. U8-8, as the FM-1 */
        break;
    case 0xB0u:
        if (d1 == 1u)
            cr_synth_ctl(&in->syn, CR_S_MODW, (float)d2 / 127.f);
        else if (d1 == 11u)
            cr_synth_ctl(&in->syn, CR_S_EXPR, (float)d2 / 127.f);
        else if (d1 == 2u && in->g.breath)
            cr_synth_ctl(&in->syn, in->g.breath == 1u ? CR_S_MODW : in->g.breath == 2u ? CR_S_AFTT : CR_S_EXPR,
                         (float)d2 / 127.f);
        else if (d1 == 64u) {
            in->sus = d2 >= 64u;
            if (!in->sus) {
                uint32_t i;
                for (i = 0; i < 128u; i++)
                    if (in->sus_bits[i >> 5] & (1u << (i & 31u)))
                        play(in, i, 0);
                in->sus_bits[0] = in->sus_bits[1] = in->sus_bits[2] = in->sus_bits[3] = 0;
            }
        } else if (d1 == 120u || d1 == 123u)
            cr_inst_all_off(in);
        else if ((p = cr_param_of_cc(d1)) >= 0 && in->p.p[p] != d2) {   /* a parameter, by the CRAFTsynth's chart */
            cr_inst_param(in, (uint32_t)p, d2);
            in->changed[(uint32_t)p >> 5] |= 1u << ((uint32_t)p & 31u);
        }
        break;
    default:
        break;
    }
}
uint32_t cr_inst_take_changed(cr_inst_t *in)
{
    uint32_t w, b;
    for (w = 0; w < 4u; w++)
        if (in->changed[w])
            for (b = 0; b < 32u; b++)
                if (in->changed[w] & (1u << b)) {
                    in->changed[w] &= ~(1u << b);
                    return w * 32u + b + 1u;
                }
    return 0;
}
int cr_inst_take_patch_changed(cr_inst_t *in)
{
    int r = in->patch_changed;
    in->patch_changed = 0;
    return r;
}

/* ---- time ---- */
void cr_inst_transport(cr_inst_t *in, double ppq, float bpm, int playing)
{
    if (playing && !in->playing)
        in->tick = -1;                          /* the transport started: the next tick lands a step */
    in->ppq = ppq;
    in->bpm = bpm > 0.f ? bpm : 0.f;
    in->playing = playing;
}
void cr_inst_render(cr_inst_t *in, float *lr, uint32_t n)
{
    cr_patch_t ap;                              /* the patch as the arp-seq sees it (TEMPO, RATE, ARP modulated) */
    if (n > CR_INST_FRAMES)
        n = CR_INST_FRAMES;
    tables(in);                                 /* a bank the matrix moved (or, deferred, a note to build it) */
    cr_synth_arp_view(&in->syn, &in->p, &ap);
    if (in->playing && in->ppq >= 0.0 && in->bpm > 0.f) {
        /* locked to the host: MIDI-clock ticks (24 a beat) placed from its song position, a step on each multiple */
        double end = in->ppq + (double)n * (double)in->bpm / (60.0 * (double)CR_INST_RATE);
        int64_t last = (int64_t)(end * 24.0), t = cr_arp_ticks(&ap);
        int64_t k = in->tick < 0 || in->tick > last || last - in->tick > 96 ? (int64_t)(in->ppq * 24.0) - 1 : in->tick;
        for (k++; k <= last; k++) {
            if (k < 0)
                continue;
            in->arp.ticks = (uint32_t)((k + t - 1) % t);   /* so the step falls on k % t == 0 */
            cr_arp_clock(&in->arp, &ap);
        }
        in->tick = last;
        in->arp.bpm = in->bpm;
    } else {
        in->arp.bpm = in->bpm;
        cr_arp_run(&in->arp, &ap, n);
    }
    cr_synth_tempo(&in->syn, in->bpm > 0.f ? in->bpm : cr_arp_bpm(&ap));
    cr_synth_render(&in->syn, lr, n);
}
