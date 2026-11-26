/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the parameter table (CC numbers from the Modal CRAFTsynth 2.0's chart), the INIT patch, the checks.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_patch.h"

/* cc, kind, default, label. Defaults are the INIT patch: a saw (bank 1, wave 1 at the saw), filter open. */
const cr_pinfo_t CR_P[CR_NP] = {
    [CR_W1POS] = {16, CR_K_NUM, 127, "WAVE 1"},
    [CR_W2POS] = {17, CR_K_NUM, 96, "WAVE 2"},
    [CR_MIX] = {18, CR_K_MIX, 0, "MIX"},
    [CR_OMAMT] = {19, CR_K_NUM, 0, "OSC MOD"},
    [CR_SPREAD] = {20, CR_K_SPREAD, 0, "SPREAD"},
    [CR_W1BANK] = {37, CR_K_BANK, 0, "BANK 1"},
    [CR_W2BANK] = {48, CR_K_BANK, 0, "BANK 2"},
    [CR_OMMODE] = {41, CR_K_OMMODE, 0, "MOD MODE"},
    [CR_TUNE] = {30, CR_K_SEMI, 64, "TUNE"},
    [CR_FINE] = {31, CR_K_CENT, 64, "FINE"},
    [CR_OCT] = {40, CR_K_OCT, 42, "OCTAVE"},
    [CR_GLIDE] = {5, CR_K_GLIDE, 64, "GLIDE"},
    [CR_CUT] = {34, CR_K_HZ, 127, "CUTOFF"},
    [CR_RES] = {35, CR_K_NUM, 0, "RESO"},
    [CR_MORPH] = {33, CR_K_MORPH, 0, "MORPH"},
    [CR_FEGAMT] = {32, CR_K_BIP, 64, "FILT EG"},
    [CR_FA] = {22, CR_K_TIME, 0, "ATTACK"},
    [CR_FD] = {23, CR_K_TIME, 40, "DECAY"},
    [CR_FS] = {24, CR_K_PCT, 127, "SUSTAIN"},
    [CR_FR] = {25, CR_K_TIME, 20, "RELEASE"},
    [CR_AA] = {26, CR_K_TIME, 0, "ATTACK"},
    [CR_AD] = {27, CR_K_TIME, 40, "DECAY"},
    [CR_AS] = {28, CR_K_PCT, 127, "SUSTAIN"},
    [CR_AR] = {29, CR_K_TIME, 20, "RELEASE"},
    [CR_AEGAMT] = {51, CR_K_BIP, 127, "AMP EG"},
    [CR_MA] = {43, CR_K_TIME, 0, "ATTACK"},
    [CR_MD] = {44, CR_K_TIME, 40, "DECAY"},
    [CR_MS] = {45, CR_K_PCT, 0, "SUSTAIN"},
    [CR_MR] = {46, CR_K_TIME, 20, "RELEASE"},
    [CR_L1RATE] = {36, CR_K_LRATE1, 60, "RATE"},
    [CR_L1SHAPE] = {39, CR_K_LSHAPE, 0, "SHAPE"},
    [CR_L1MODE] = {56, CR_K_LMODE, 0, "MODE"},
    [CR_L1SYNC] = {52, CR_K_ONOFF, 0, "SYNC"},
    [CR_L2RATE] = {47, CR_K_LRATE2, 40, "RATE"},
    [CR_L2SHAPE] = {50, CR_K_LSHAPE, 0, "SHAPE"},
    [CR_L2MODE] = {57, CR_K_LMODE, 0, "MODE"},
    [CR_L2SYNC] = {54, CR_K_ONOFF, 0, "SYNC"},
    [CR_DIST] = {12, CR_K_NUM, 0, "DISTORT"},
    [CR_DMIX] = {13, CR_K_NUM, 0, "DELAY"},
    [CR_DTIME] = {14, CR_K_DTIME, 64, "TIME"},
    [CR_DFB] = {15, CR_K_PCT, 40, "FEEDBACK"},
    [CR_DSYNC] = {55, CR_K_ONOFF, 0, "SYNC"},
    [CR_VOL] = {7, CR_K_NUM, 100, "VOLUME"},
    [CR_DEP0] = {88, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP1] = {89, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP2] = {90, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP3] = {91, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP4] = {92, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP5] = {93, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP6] = {94, CR_K_BIP, 64, "DEPTH"},
    [CR_DEP7] = {95, CR_K_BIP, 64, "DEPTH"},
    [CR_DST0] = {101, CR_K_DEST, 0, "DEST"},
    [CR_DST1] = {102, CR_K_DEST, 0, "DEST"},
    [CR_DST2] = {103, CR_K_DEST, 0, "DEST"},
    [CR_DST3] = {104, CR_K_DEST, 0, "DEST"},
    [CR_DST4] = {105, CR_K_DEST, 0, "DEST"},
    [CR_DST5] = {106, CR_K_DEST, 0, "DEST"},
    [CR_DST6] = {107, CR_K_DEST, 0, "DEST"},
    [CR_DST7] = {108, CR_K_DEST, 0, "DEST"},
    [CR_ARP] = {58, CR_K_ONOFF, 0, "ARP"},
    [CR_TEMPO] = {3, CR_K_TEMPO, 40, "TEMPO"},       /* 40 + 2 x value: 40..294 BPM (default 120) */
    [CR_ARPRATE] = {9, CR_K_ARPRATE, 64, "RATE"},
    /* 0.4 (free CC numbers: the CRAFTsynth's chart has none of these) */
    [CR_BMODE] = {59, CR_K_BMODE, 0, "B MODE"},
    [CR_BAMT] = {60, CR_K_NUM, 0, "B MOD"},
    [CR_FTYPE] = {61, CR_K_FTYPE, 0, "TYPE"},
    [CR_VMODE] = {62, CR_K_VMODE, 32, "MODE"},
    [CR_VSIZE] = {63, CR_K_NUM, 80, "SIZE"},
    [CR_VDECAY] = {70, CR_K_PCT, 80, "DECAY"},
    [CR_VMIX] = {71, CR_K_PCT, 0, "REVERB"},
    [CR_VWARP] = {72, CR_K_PCT, 90, "WARP"},
    [CR_VDENS] = {74, CR_K_PCT, 90, "DENSITY"},
    [CR_VMOD] = {75, CR_K_PCT, 40, "MOD"},
    [CR_CHMIX] = {76, CR_K_PCT, 0, "CHORUS"},
    [CR_CHRATE] = {77, CR_K_NUM, 40, "CH RATE"},
    [CR_PHMIX] = {78, CR_K_PCT, 0, "PHASER"},
    [CR_PHRATE] = {80, CR_K_NUM, 40, "PH RATE"},
    [CR_ERA] = {81, CR_K_ERA, 0, "ERA"},
    [CR_CRUSH] = {82, CR_K_PCT, 60, "CRUSH"},
    [CR_OTT] = {83, CR_K_PCT, 0, "OTT"},
    [CR_SUBLVL] = {84, CR_K_PCT, 0, "SUB LVL"},
    [CR_SUBOCT] = {85, CR_K_SUBOCT, 0, "SUB OCT"},
    [CR_SUBWAVE] = {86, CR_K_SUBWAVE, 0, "SUB WAVE"},
};

const char *const CR_BMODE_NAME[5] = {"FM FROM A", "PM FROM A", "SYNC TO A", "RING WITH A", "WAVEFOLD"};
const char *const CR_FTYPE_NAME[12] = {"SVF", "LADDER", "MS-20", "TB-303", "SEM", "STEINER", "POLIVOKS",
                                       "FORMANT", "COMB +/-", "PHASER", "DUAL PEAK", "DECIMATOR"};
const char *const CR_VMODE_NAME[6] = {"ROOM", "HALL", "HALO", "SWELL", "ABYSS", "DUB"};
const char *const CR_ERA_NAME[6] = {"OFF", "SP-1200", "MPC60", "S950", "S1000", "AMIGA"};
const char *const CR_SUBOCT_NAME[2] = {"-1 OCT", "-2 OCT"};
const char *const CR_SUBWAVE_NAME[3] = {"SINE", "TRIANGLE", "SQUARE"};

uint32_t cr_band(uint32_t v, uint32_t n) { return ((v & 0x7Fu) * n) >> 7; }
uint32_t cr_band_val(uint32_t i, uint32_t n) { return i >= n ? 127u : (i * 128u + 64u) / n; }

const char *const CR_DEST_NAME[CR_ND] = {
    "NONE", "WAVE 1", "WAVE 2", "MIX", "PITCH", "OSC2 PCH", "OSC MOD", "L1 RATE", "L2 RATE", "L1 SHAPE",
    "L2 SHAPE", "CUTOFF", "RESO", "MORPH", "FEG AMT", "FEG A", "FEG D", "FEG S", "FEG R", "AEG AMT", "AEG A",
    "AEG D", "AEG S", "AEG R", "VOLUME", "MEG A", "MEG D", "MEG S", "MEG R", "DISTORT", "DELAY", "DLY TIME",
    "DLY FB", "SPREAD", "GLIDE", "L1 DEPTH", "L2 DEPTH", "MEG DEP", "B MOD", "REVERB", "CHORUS", "PHASER", "CRUSH",
    "OTT", "BANK 1", "BANK 2", "MOD MODE", "TUNE", "FINE", "OCTAVE", "L1 MODE", "L1 SYNC", "L2 MODE", "L2 SYNC",
    "DLY SYNC", "NOTE DEP", "VELO DEP", "MODW DEP", "EXPR DEP", "AFTT DEP", "ARP", "TEMPO", "ARP RATE", "B MODE",
    "FLT TYPE", "VRB MODE", "VRB SIZE", "VRB DCAY", "VRB WARP", "VRB DENS", "VRB MOD", "CH RATE", "PH RATE", "ERA", "SUB LVL", "SUB OCT", "SUB WAVE"};
const char *const CR_SRC_NAME[CR_NSRC] = {"LFO 1", "LFO 2", "MOD EG", "NOTE", "VELO", "MOD WHL", "EXPR", "AFTTCH"};
const char *const CR_OM_NAME[CR_NOM] = {
    "PHASE MOD", "WINDOW SYNC", "RING MOD", "TRI FOLD", "DE-REZ", "RISE/RUN", "GLITCH SINE", "WINDOW AMP",
    "FREQ MOD", "HARD SYNC", "MIN MOD", "SINE FOLD", "BIT CRUSH", "SCRUNCH", "LO-FI PHASE", "VOCAL SYNC"};
const char *const CR_BANK_NAME[CR_BANKS] = {
    "ANALOGUE", "DIGITAL 1", "MATH", "POLYGON", "CURATED 1", "FORMANT", "DIGITAL 2", "CURATED 2", "SINE/NOISE",
    "USER 1", "USER 2", "USER 3", "USER 4", "USER 5", "USER 6", "USER 7", "USER 8",
    "USER 9", "USER 10", "USER 11", "USER 12", "USER 13", "USER 14", "USER 15", "USER 16",
    "USER 17", "USER 18", "USER 19", "USER 20", "USER 21", "USER 22", "USER 23", "USER 24",
    "USER 25", "USER 26", "USER 27", "USER 28", "USER 29", "USER 30", "USER 31", "USER 32"};
const char *const CR_SHAPE_NAME[10] = {"UNISON", "MAJOR", "MINOR", "MAJ 6TH", "SUS 4TH", "5THS", "5TH+OCT",
                                       "OCT +1+2", "OCT +1-1", "OCT -1-2"};

const char *const CR_SCALE_NAME[CR_NSCALE] = {
    "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN", "HARM MINOR",
    "MELOD MINOR", "PHRYG DOM", "MAJ PENTA", "MIN PENTA", "BLUES", "HUNGARIAN", "WHOLE TONE"};
#define SM(a, b, c, d, e, f, g, h, i, j, k, l) \
    ((a) | (b) << 1 | (c) << 2 | (d) << 3 | (e) << 4 | (f) << 5 | (g) << 6 | (h) << 7 | (i) << 8 | (j) << 9 | (k) << 10 | (l) << 11)
const uint16_t CR_SCALE_MASK[CR_NSCALE] = {
    0xFFF,
    SM(1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1),        /* major */
    SM(1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0),        /* natural minor */
    SM(1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0),        /* dorian */
    SM(1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0),        /* phrygian */
    SM(1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1),        /* lydian */
    SM(1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0),        /* mixolydian */
    SM(1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0),        /* locrian */
    SM(1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1),        /* harmonic minor */
    SM(1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1),        /* melodic minor */
    SM(1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0),        /* phrygian dominant */
    SM(1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0),        /* major pentatonic */
    SM(1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0),        /* minor pentatonic */
    SM(1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0),        /* blues */
    SM(1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1),        /* hungarian minor */
    SM(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0),        /* whole tone */
};

static void cr_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        d[i] = 0;
    for (i = 0; s && s[i] && i + 1u < n; i++)
        d[i] = s[i];
}

void cr_patch_init(cr_patch_t *p, const char *name)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)p;
    for (i = 0; i < sizeof *p; i++)
        b[i] = 0;
    cr_cpy(p->name, name, sizeof p->name);
    for (i = 0; i < CR_NP; i++)
        p->p[i] = CR_P[i].def;
    p->magic = CR_PATCH_MAGIC;
    p->ver = CR_PATCH_VER;
}

int cr_patch_from_v1(const uint8_t v1[CR_PATCH_V1_BYTES], cr_patch_t *out)
{
    uint32_t i;
    if (v1[77] != CR_PATCH_MAGIC || v1[78] != 1u)
        return -1;
    cr_patch_init(out, 0);
    for (i = 0; i < 12u; i++)
        out->name[i] = (char)v1[i];
    for (i = 0; i <= CR_ARPRATE; i++)  /* the 62 parameters it had; the new ones keep their defaults */
        out->p[i] = v1[12u + i];
    out->steps = v1[76];
    out->pad = v1[79];
    for (i = 0; i < CR_NSTEP; i++)
        out->step[i] = v1[80u + i];
    return cr_patch_check(out);
}

int cr_patch_check(cr_patch_t *p)
{
    uint32_t i;
    if (p->magic != CR_PATCH_MAGIC || p->ver != CR_PATCH_VER)
        return -1;
    p->name[sizeof p->name - 1u] = 0;
    for (i = 0; i < sizeof p->name - 1u; i++)
        if (p->name[i] && (p->name[i] < 32 || p->name[i] > 126))
            p->name[i] = ' ';
    for (i = 0; i < CR_NP_MAX; i++)
        p->p[i] = i < CR_NP ? p->p[i] & 0x7Fu : 0u;
    for (i = 0; i < CR_NSRC; i++)
        if (p->p[CR_DST0 + i] >= CR_ND || !cr_dest_ok(i, p->p[CR_DST0 + i]))
            p->p[CR_DST0 + i] = CR_D_NONE;
    if (p->steps > CR_NSTEP)
        p->steps = 0;
    for (i = 0; i < CR_NSTEP; i++)
        p->step[i] &= 0x7Fu;
    return 0;
}

void cr_globals_init(cr_globals_t *g)
{
    uint8_t *b = (uint8_t *)g;
    uint32_t i;
    for (i = 0; i < sizeof *g; i++)
        b[i] = 0;
    g->magic = CR_GLOBALS_MAGIC;
    g->midi_in = 16;                   /* omni */
    g->root = 5;                       /* F */
    g->bend = 2;
    g->velo = 1;
    g->local = 1;
    g->theme = 2;                      /* SERUM */
}

int cr_globals_check(cr_globals_t *g)
{
    if (g->magic != CR_GLOBALS_MAGIC)
        return -1;
    g->midi_ch &= 15u;
    if (g->midi_in > 16u)
        g->midi_in = 16;
    g->scale %= CR_NSCALE;
    g->root %= 12u;
    if (g->clock > 2u)
        g->clock = 0;
    if (g->bend > 24u)
        g->bend = 2;
    if (g->cur >= CR_NPRESET)
        g->cur = 0;
    g->velo &= 1u;
    g->local &= 1u;
    if (g->breath > 3u)
        g->breath = 0;
    g->kbmode &= 1u;
    g->arp_out &= 1u;
    return 0;
}

int cr_param_of_cc(uint32_t cc)
{
    uint32_t i;
    for (i = 0; i < CR_NP; i++)
        if (CR_P[i].cc == cc)
            return (int)i;
    return -1;
}

uint32_t cr_om_of(uint32_t v) { return (v & 0x7Fu) >> 3; }

uint32_t cr_bank_of(uint32_t v, int wave2)
{
    uint32_t n = wave2 ? CR_BANKS : CR_BANKS - 1u;       /* the noise bank is Wave 2's only */
    uint32_t b = ((v & 0x7Fu) * n) >> 7;
    if (!wave2 && b >= CR_BANK_NOISE)
        b++;                                             /* Wave 1 skips it: 9, 10 are the user banks */
    return b;
}

uint32_t cr_bank_val(uint32_t bank, int wave2)
{
    uint32_t n = wave2 ? CR_BANKS : CR_BANKS - 1u, i = bank;
    if (!wave2 && bank > CR_BANK_NOISE)
        i--;
    if (i >= n)
        i = n - 1u;
    return (i * 128u + 64u) / n;
}

int32_t cr_semi_of(uint32_t v)
{
    int32_t d = (int32_t)(v & 0x7Fu) - 64;
    return d >= 0 ? (d * 48 + 31) / 63 : -((-d * 48 + 31) / 64);
}

uint32_t cr_spread_shape(uint32_t v)
{
    if (v < 64u)
        return 0;
    v = (v - 64u) / 7u + 1u;           /* 64-70 major, 71-77 minor ... (the CRAFTsynth's CC 20) */
    return v > 9u ? 9u : v;
}

int cr_dest_ok(uint32_t src, uint32_t dst)
{
    if (src == CR_S_LFO1)
        return dst != CR_D_L1RATE && dst != CR_D_L1SHAPE && dst != CR_D_L1DEP && dst != CR_D_L1MODE && dst != CR_D_L1SYNC;
    if (src == CR_S_LFO2)
        return dst != CR_D_L2RATE && dst != CR_D_L2SHAPE && dst != CR_D_L2DEP && dst != CR_D_L2MODE && dst != CR_D_L2SYNC;
    if (src == CR_S_MEG)
        return !(dst >= CR_D_MA && dst <= CR_D_MR) && dst != CR_D_MEGDEP;
    return dst != CR_D_NOTEDEP + (src - CR_S_NOTE);   /* no source modulates its own depth */
}

/* ---- 0.13: Random Patch (the Twisted Tracker's rules, shared by every version) ----
 * A random preset (never an empty user slot), then: each continuous value pushed from it by up to a quarter of its
 * range (one in five by up to 60%), about a third of the stepped ones changed, the banks a random factory bank
 * (or kept), and level, octave, glide, the amp envelope's amount, the arp and the delay / reverb tails left alone,
 * so it stays playable and in the mix. A matrix slot never gets a destination that holds the amp open or moves a
 * bank. */
uint32_t cr_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static float rnd01(uint32_t *s) { return (float)(cr_rand(s) >> 8) * (1.f / 16777216.f); }

uint32_t cr_rand_slot(uint32_t *s, int (*saved)(void *ctx, uint32_t slot), void *ctx)
{
    uint32_t t, slot = 0;
    for (t = 0; t < 24u; t++) {
        slot = cr_rand(s) % CR_NPRESET;
        if (slot < CR_NFACTORY || !saved || saved(ctx, slot))
            return slot;
    }
    return cr_rand(s) % CR_NFACTORY;
}

/* a stepped value's choices (0: continuous), and choice i's patch value: the Tracker's CRAFTY steps */
static uint32_t rnd_choices(uint32_t id)
{
    switch (CR_P[id].kind) {
    case CR_K_BANK: return 8u;                  /* the factory banks both waves have */
    case CR_K_OMMODE: return CR_NOM;
    case CR_K_OCT: return 7u;
    case CR_K_LSHAPE: return 5u;
    case CR_K_LMODE: return 3u;
    case CR_K_ONOFF: return 2u;
    case CR_K_ARPRATE: return 6u;
    case CR_K_DEST: return CR_ND;
    case CR_K_BMODE: return 5u;
    case CR_K_FTYPE: return 12u;
    case CR_K_VMODE: return 6u;
    case CR_K_ERA: return 6u;
    case CR_K_SUBOCT: return 2u;
    case CR_K_SUBWAVE: return 3u;
    default: return 0u;
    }
}
static uint32_t rnd_choice_val(uint32_t id, uint32_t i)
{
    switch (CR_P[id].kind) {
    case CR_K_BANK: return cr_bank_val(i, id == CR_W2BANK);
    case CR_K_OMMODE: return i * 8u + 4u;
    case CR_K_LSHAPE: return (i * 127u) / 4u;
    case CR_K_LMODE: return i == 0u ? 20u : i == 1u ? 63u : 105u;
    case CR_K_ONOFF: return i ? 127u : 0u;
    case CR_K_DEST: return i;
    default: return cr_band_val(i, rnd_choices(id));
    }
}
int cr_rand_keeps(uint32_t id)
{
    switch (id) {
    case CR_VOL: case CR_OCT: case CR_GLIDE: case CR_AEGAMT: case CR_DTIME: case CR_DFB:
    case CR_ARP: case CR_TEMPO: case CR_ARPRATE: case CR_VMODE: case CR_VSIZE: case CR_VDECAY:
        return 1;
    default:
        return 0;
    }
}
static int rnd_dest_bad(uint32_t src, uint32_t d)
{
    return (d >= CR_D_AEGAMT && d <= CR_D_VOL) || d == CR_D_W1BANK || d == CR_D_W2BANK || !cr_dest_ok(src, d);
}
/* a time or level leaning low (amp envelope times at full depth: most patches still speak) */
static int rnd_short(uint32_t id)
{
    return id == CR_AA || id == CR_AD || id == CR_AR || id == CR_FA || id == CR_MA;
}
void cr_patch_random(cr_patch_t *p, const cr_patch_t *cur, uint32_t *s, uint32_t flags, uint32_t depth,
                     uint32_t umask)
{
    uint32_t id, i, nu = 0;
    float a = (float)(depth > 127u ? 127u : depth) / 127.f;   /* 0: the Tracker's rules; 1: anything goes */
    char nm[12];
    for (i = 0; i < CR_NUBANK; i++)
        nu += (umask >> i) & 1u;
    for (id = 0; id < CR_NP; id++) {
        uint32_t n = rnd_choices(id);
        if (CR_P[id].kind == CR_K_BANK) {
            if ((flags & CR_RAND_KEEP_TABLES) && cur)
                p->p[id] = cur->p[id];
            else if (!(flags & CR_RAND_KEEP_TABLES)) {
                if (nu && cr_rand(s) % 2u) {    /* half the time one of your banks, when you have any */
                    uint32_t k = cr_rand(s) % nu, u;
                    for (u = 0; u < CR_NUBANK; u++)
                        if (((umask >> u) & 1u) && !k--)
                            break;
                    p->p[id] = (uint8_t)cr_bank_val(CR_NBANK + u, id == CR_W2BANK);
                } else
                    p->p[id] = (uint8_t)rnd_choice_val(id, cr_rand(s) % 8u);
            }
            continue;
        }
        if (cr_rand_keeps(id))
            continue;
        if (n) {                                /* two in three keep the preset's choice (fewer with depth) */
            uint32_t v;
            if (rnd01(s) > 0.33f + 0.67f * a)
                continue;
            v = rnd_choice_val(id, cr_rand(s) % n);
            if (CR_P[id].kind == CR_K_DEST && rnd_dest_bad(id - CR_DST0, v))
                continue;
            p->p[id] = (uint8_t)v;
        } else if (rnd01(s) < a) {              /* with depth: anywhere in its range */
            float r = rnd01(s);
            if (rnd_short(id))
                r *= r;
            if (id == CR_AS)
                r = 0.3f + 0.7f * r;            /* the amp held open enough to hear */
            p->p[id] = (uint8_t)(r * 127.f + 0.5f);
        } else {                                /* an offset from the preset: mostly small, now and then bigger */
            float r = rnd01(s) * 2.f - 1.f, span = (rnd01(s) < 0.2f ? 0.6f : 0.25f) * 127.f, o = r * span;
            int32_t v = (int32_t)p->p[id] + (int32_t)(o < 0.f ? o - 0.5f : o + 0.5f);
            p->p[id] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        }
    }
    /* its name says what it is: "RND " and the preset's ("RND SYNC SW") */
    for (i = 0; i < 11u && p->name[i]; i++)
        nm[i] = p->name[i];
    nm[i] = 0;
    if (!(nm[0] == 'R' && nm[1] == 'N' && nm[2] == 'D' && nm[3] == ' ')) {
        p->name[0] = 'R';
        p->name[1] = 'N';
        p->name[2] = 'D';
        p->name[3] = ' ';
        for (i = 0; i < 7u && nm[i]; i++)
            p->name[4 + i] = nm[i];
        for (i += 4u; i < 12u; i++)
            p->name[i] = 0;
    }
}
