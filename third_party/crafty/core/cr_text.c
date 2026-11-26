/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: a parameter's value as text (cr_text.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_text.h"
#include "cr_arp.h"
#include "cr_math.h"

static const char *const TX_OM_SHORT[CR_NOM] = {"PM", "WSYNC", "RING", "TFOLD", "DEREZ", "R/RUN", "GLTCH", "WAMP",
    "FM", "SYNC", "MIN", "SFOLD", "CRUSH", "SCRUN", "LO-FI", "VOCAL"};
static const char *const TX_SHAPE_SHORT[10] = {"", "MAJ", "MIN", "MAJ6", "SUS4", "5TH", "5+8", "8+15", "8-8", "-8-15"};
static const char *const TX_LSHAPE[5] = {"SINE", "TRI", "SAW", "SQUARE", "S+H"};
static const char *const TX_L1SYNC[16] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1", "3/2", "2", "3",
                                          "4", "6", "8", "12", "16"};
static const char *const TX_L2SYNC[8] = {"1/16", "1/8", "1/4", "1/2", "1", "5/4", "2", "4"};
static const char *const TX_L2ROOT[8] = {"R/8", "R/4", "R/2", "R", "Rx1.5", "Rx2", "Rx2.5", "Rx3"};
static const char *const TX_DSYNC[9] = {"1/16", "1/12", "1/8", "1/8D", "1/4", "1/2", "3/4", "4/4", "6/4"};

static void tx_cpy(char *b, const char *s)
{
    while ((*b++ = *s++))
        ;
}
static uint32_t tx_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void tx_cat(char *b, const char *s) { tx_cpy(b + tx_len(b), s); }
static void tx_num(char *b, int32_t v, int sign)
{
    char t[12];
    uint32_t n = 0, k = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        b[k++] = '-';
    else if (sign && v > 0)
        b[k++] = '+';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        b[k++] = t[--n];
    b[k] = 0;
}
static void tx_dec(char *b, float v, uint32_t places)   /* v with 1 or 2 decimals, no sign */
{
    uint32_t sc = places == 2u ? 100u : 10u, t = (uint32_t)(v * (float)sc + 0.5f), k;
    tx_num(b, (int32_t)(t / sc), 0);
    k = tx_len(b);
    b[k++] = '.';
    if (places == 2u)
        b[k++] = (char)('0' + t / 10u % 10u);
    b[k++] = (char)('0' + t % 10u);
    b[k] = 0;
}
static void tx_time(char *b, float s)
{
    if (s < 1.f) {
        tx_num(b, (int32_t)(s * 1000.f + 0.5f), 0);
        tx_cat(b, "MS");
    } else {
        tx_dec(b, s, 1);
        tx_cat(b, "S");
    }
}

void cr_param_text(const cr_patch_t *p, uint32_t id, char *b, int lng, const char *(*uname)(void *ctx, uint32_t u),
                   void *ctx)
{
    uint32_t v;
    b[0] = 0;
    if (id >= CR_NP)
        return;
    v = p->p[id];
    switch (CR_P[id].kind) {
    case CR_K_BIP: tx_num(b, (int32_t)v - 64, 1); break;
    case CR_K_MIX:
        if (v == 0u) tx_cpy(b, "W1");
        else if (v == 127u) tx_cpy(b, "W2");
        else tx_num(b, (int32_t)v, 0);
        break;
    case CR_K_TIME: { float t = (float)v / 127.f; tx_time(b, 0.001f + 4.f * t * t * t); break; }
    case CR_K_PCT: tx_num(b, (int32_t)(v * 100u + 63u) / 127, 0); tx_cat(b, "%"); break;
    case CR_K_BANK: {
        uint32_t bk = cr_bank_of(v, id == CR_W2BANK);
        if (lng) {
            tx_cpy(b, CR_BANK_NAME[bk]);
            if (bk >= CR_NBANK) {
                const char *nm = uname ? uname(ctx, bk - CR_NBANK) : 0;
                if (nm && nm[0])
                    tx_cpy(b, nm);               /* a user bank: its name */
                else if (!nm)
                    tx_cat(b, " (EMPTY)");
            }
        } else if (bk >= CR_NBANK) {
            tx_cpy(b, "U");
            tx_num(b + 1, (int32_t)(bk - CR_NBANK + 1u), 0);
        } else {
            tx_cpy(b, "B");
            tx_num(b + 1, (int32_t)bk + 1, 0);
        }
        break;
    }
    case CR_K_OMMODE: tx_cpy(b, lng ? CR_OM_NAME[cr_om_of(v)] : TX_OM_SHORT[cr_om_of(v)]); break;
    case CR_K_SPREAD:
        if (v < 64u) {
            if (lng && v) { tx_cpy(b, "UNISON "); tx_num(b + 7, (int32_t)v, 0); }
            else if (lng) tx_cpy(b, "OFF");
            else tx_num(b, (int32_t)v, 0);
        } else
            tx_cpy(b, lng ? CR_SHAPE_NAME[cr_spread_shape(v)] : TX_SHAPE_SHORT[cr_spread_shape(v)]);
        break;
    case CR_K_SEMI: tx_num(b, cr_semi_of(v), 1); if (lng) tx_cat(b, " SEMI"); break;
    case CR_K_CENT: tx_num(b, ((int32_t)v - 64) * 100 / 63, 1); tx_cat(b, lng ? " CENTS" : "C"); break;
    case CR_K_OCT: tx_num(b, (int32_t)((v * 7u) >> 7) - 2, 1); break;
    case CR_K_GLIDE:
        if (v == 64u)
            tx_cpy(b, "OFF");
        else {
            float g = ((float)v - 64.f) / 63.f;
            if (g < 0.f)
                g = -g;
            if (lng) {
                tx_cpy(b, v > 64u ? "ALWAYS " : "LEGATO ");
                tx_time(b + 7, 2.5f * g * g);
            } else {
                tx_cpy(b, v > 64u ? "A" : "L");
                tx_num(b + 1, (int32_t)(g * 99.f + 0.5f), 0);
            }
        }
        break;
    case CR_K_HZ: {
        float f = 20.f * cr_exp2f((float)v * (10.f / 127.f));
        if (f < 1000.f)
            tx_num(b, (int32_t)(f + 0.5f), 0);
        else {
            tx_dec(b, f / 1000.f, 1);
            tx_cat(b, "K");
        }
        if (lng)
            tx_cat(b, "HZ");
        break;
    }
    case CR_K_MORPH:
        if (lng) {
            tx_cpy(b, v < 22u ? "LOW PASS" : v < 50u ? "LOW SHELF" : v < 78u ? "BAND PASS" : v < 106u ? "BP TO HP" : "HIGH PASS");
        } else
            tx_cpy(b, v < 22u ? "LP" : v < 50u ? "SHELF" : v < 78u ? "BP" : v < 106u ? "B-H" : "HP");
        break;
    case CR_K_LSHAPE: tx_cpy(b, TX_LSHAPE[(v * 4u + 63u) / 127u]); if (!lng && b[3]) b[3] = 0; break;
    case CR_K_LMODE: tx_cpy(b, v < 42u ? (lng ? "RETRIGGER" : "RTRIG") : v < 84u ? "FREE" : (lng ? "SINGLE" : "ONCE")); break;
    case CR_K_ONOFF: tx_cpy(b, v >= 64u ? "ON" : "OFF"); break;
    case CR_K_LRATE1:
        if (p->p[CR_L1SYNC] >= 64u) {
            tx_cpy(b, TX_L1SYNC[(v * 16u) >> 7]);
            if (lng) tx_cat(b, " PER BAR");
        } else {
            float hz = 0.02f * cr_exp2f((float)v * (10.64f / 127.f));
            tx_dec(b, hz, hz < 10.f ? 2u : 1u);
            if (lng) tx_cat(b, " HZ");
        }
        break;
    case CR_K_LRATE2:
        if (v >= 64u)
            tx_cpy(b, TX_L2ROOT[((v - 64u) * 8u) >> 6]);
        else if (p->p[CR_L2SYNC] >= 64u) {
            tx_cpy(b, TX_L2SYNC[(v * 8u) >> 6]);
            if (lng) tx_cat(b, " PER BEAT");
        } else {
            float hz = 0.02f * cr_exp2f((float)v * (2.f * 10.64f / 127.f));
            tx_dec(b, hz, hz < 10.f ? 2u : 1u);
            if (lng) tx_cat(b, " HZ");
        }
        break;
    case CR_K_DTIME:
        if (p->p[CR_DSYNC] >= 64u)
            tx_cpy(b, TX_DSYNC[(v * 9u) >> 7]);
        else
            tx_time(b, 0.0005f * cr_exp2f((float)v * (10.55f / 127.f)));
        break;
    case CR_K_TEMPO: tx_num(b, 40 + 2 * (int32_t)v, 0); if (lng) tx_cat(b, " BPM"); break;
    case CR_K_ARPRATE: tx_cpy(b, CR_ARPRATE_NAME[(v * 6u) >> 7]); break;
    case CR_K_DEST: tx_cpy(b, CR_DEST_NAME[v % CR_ND]); break;
    case CR_K_BMODE: {
        static const char *const SH[5] = {"FM", "PM", "SYNC", "RING", "FOLD"};
        tx_cpy(b, lng ? CR_BMODE_NAME[cr_band(v, 5)] : SH[cr_band(v, 5)]);
        break;
    }
    case CR_K_FTYPE: {
        static const char *const SH[12] = {"SVF", "LADR", "MS20", "303", "SEM", "STNR", "POLI", "FORM", "COMB",
                                           "PHAS", "DUAL", "DECIM"};
        tx_cpy(b, lng ? CR_FTYPE_NAME[cr_band(v, 12)] : SH[cr_band(v, 12)]);
        break;
    }
    case CR_K_VMODE: tx_cpy(b, CR_VMODE_NAME[cr_band(v, 6)]); break;
    case CR_K_SUBOCT:
        tx_cpy(b, CR_SUBOCT_NAME[cr_band(v, 2)]);
        break;
    case CR_K_SUBWAVE: {
        static const char *const SH[3] = {"SINE", "TRI", "SQUARE"};
        tx_cpy(b, lng ? CR_SUBWAVE_NAME[cr_band(v, 3)] : SH[cr_band(v, 3)]);
        break;
    }
    case CR_K_ERA: {
        static const char *const SH[6] = {"OFF", "SP12", "MPC", "S950", "S1K", "AMIGA"};
        tx_cpy(b, lng ? CR_ERA_NAME[cr_band(v, 6)] : SH[cr_band(v, 6)]);
        break;
    }
    default: tx_num(b, (int32_t)v, 0); break;
    }
}

/* the page a parameter sits on, where its label alone is ambiguous (ATTACK x3, RATE x3 ...): "FILT EG", or "" */
static const char *const TX_PAGE[CR_NP] = {
    "", "", "OSC", "", "", "", "", "OSC", "OSC B", "OSC B", "", "", "", "FILTER", "FILTER", "", "FILT EG",
    "FILT EG", "FILT EG", "FILT EG", "AMP EG", "AMP EG", "AMP EG", "AMP EG", "", "MOD EG", "MOD EG", "MOD EG",
    "MOD EG", "LFO 1", "LFO 1", "LFO 1", "LFO 1", "LFO 2", "LFO 2", "LFO 2", "LFO 2", "", "", "DELAY", "DELAY",
    "DELAY", "", "LFO 1", "LFO 2", "MOD EG", "NOTE", "VELO", "MOD WHL", "EXPR", "AFTTCH", "LFO 1", "LFO 2",
    "MOD EG", "NOTE", "VELO", "MOD WHL", "EXPR", "AFTTCH", "", "", "ARP", "", "", "FILTER", "REVERB", "REVERB",
    "REVERB", "", "REVERB", "REVERB", "REVERB", "", "", "", "", "", "", "", "", "", ""};
_Static_assert(sizeof TX_PAGE / sizeof TX_PAGE[0] == CR_NP, "a page for every parameter");
const char *cr_param_page(uint32_t id) { return id < CR_NP ? TX_PAGE[id] : ""; }
