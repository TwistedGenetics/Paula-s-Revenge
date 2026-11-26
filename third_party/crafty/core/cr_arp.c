/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the sequenced arpeggiator (see cr_arp.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_arp.h"

const char *const CR_ARPRATE_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const uint8_t TICKS[6] = {24, 12, 8, 6, 4, 3};

float cr_arp_bpm(const cr_patch_t *p) { return 40.f + 2.f * (float)p->p[CR_TEMPO]; }
uint32_t cr_arp_ticks(const cr_patch_t *p) { return TICKS[((uint32_t)p->p[CR_ARPRATE] * 6u) >> 7]; }

void cr_arp_init(cr_arp_t *a, const cr_arp_out_t *out)
{
    uint8_t *b = (uint8_t *)a;
    uint32_t i;
    for (i = 0; i < sizeof *a; i++)
        b[i] = 0;
    a->out = out;
    a->sounding = -1;
}

static void stop_note(cr_arp_t *a)
{
    if (a->sounding >= 0) {
        a->out->off(a, (uint32_t)a->sounding);
        a->sounding = -1;
    }
}

void cr_arp_release_all(cr_arp_t *a)
{
    a->nheld = 0;
    a->playing = 0;
    stop_note(a);
}

void cr_arp_key(cr_arp_t *a, uint32_t note, uint32_t vel)
{
    uint32_t i, j;
    if (vel) {
        for (i = j = 0; i < a->nheld; i++)
            if (a->held[i] != note)
                a->held[j++] = a->held[i];
        a->nheld = (uint8_t)j;
        if (a->nheld < sizeof a->held)
            a->held[a->nheld++] = (uint8_t)note;
        a->vel = (uint8_t)vel;
        if (!a->playing) {             /* the first note: the pattern starts now, on the beat */
            a->playing = 1;
            a->step = 0;
            a->acc = 1e9f;
            a->ticks = 1000;
        }
        return;
    }
    for (i = j = 0; i < a->nheld; i++)
        if (a->held[i] != note)
            a->held[j++] = a->held[i];
    a->nheld = (uint8_t)j;
    if (!a->nheld) {
        a->playing = 0;
        stop_note(a);
    }
}

static void next_step(cr_arp_t *a, const cr_patch_t *p)
{
    int32_t n = -1;
    stop_note(a);
    if (!a->nheld) {
        a->playing = 0;
        return;
    }
    if (p->steps) {                    /* the pattern, on the last note held */
        uint32_t s = p->step[a->step % p->steps];
        if (s != CR_REST)
            n = (int32_t)a->held[a->nheld - 1u] + (int32_t)s - 64;
        a->step = (uint8_t)((a->step + 1u) % p->steps);
    } else {                           /* up through the held notes */
        uint8_t sorted[16];
        uint32_t i, j, k = a->nheld;
        for (i = 0; i < k; i++)
            sorted[i] = a->held[i];
        for (i = 1; i < k; i++)
            for (j = i; j > 0 && sorted[j - 1u] > sorted[j]; j--) {
                uint8_t t = sorted[j];
                sorted[j] = sorted[j - 1u];
                sorted[j - 1u] = t;
            }
        n = sorted[a->step % k];
        a->step = (uint8_t)((a->step + 1u) % k);
    }
    if (n >= 0 && n < 128) {
        a->out->on(a, (uint32_t)n, a->vel);
        a->sounding = (int16_t)n;
    }
}

void cr_arp_run(cr_arp_t *a, const cr_patch_t *p, uint32_t n)
{
    float len = 44100.f * 60.f / (a->bpm > 0.f ? a->bpm : cr_arp_bpm(p)) * (float)cr_arp_ticks(p) / 24.f;
    if (!a->playing)
        return;
    a->acc += (float)n;
    if (a->acc >= len) {
        a->acc = a->acc > 2.f * len ? 0.f : a->acc - len;
        next_step(a, p);
        a->gate_at = len * 0.5f;
    } else if (a->acc >= a->gate_at && a->sounding >= 0)
        stop_note(a);                  /* half the step: the note ends (the envelope's release follows) */
}

void cr_arp_clock(cr_arp_t *a, const cr_patch_t *p)
{
    uint32_t t = cr_arp_ticks(p);
    if (!a->playing)
        return;
    if (++a->ticks >= t) {
        a->ticks = 0;
        next_step(a, p);
    } else if (a->ticks == t / 2u && a->sounding >= 0)
        stop_note(a);
}
