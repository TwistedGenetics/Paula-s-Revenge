/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the sequenced arpeggiator (the CRAFTsynth's ARP-SEQ). With no pattern it plays the held notes upward;
 * with a pattern (up to 16 steps of notes and rests, entered from the keys) it plays the pattern transposed to the
 * last note held: a note-triggered sequence. Its clock: the patch's tempo, or MIDI clock (24 a beat).
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_patch.h"

struct cr_arp;
typedef struct {
    void (*on)(struct cr_arp *a, uint32_t note, uint32_t vel);
    void (*off)(struct cr_arp *a, uint32_t note);
} cr_arp_out_t;

typedef struct cr_arp {
    const cr_arp_out_t *out;
    void *user;                        /* the host's, for out (an instance; set after cr_arp_init) */
    uint8_t held[16], nheld, playing, vel, step;
    float acc, gate_at;                /* samples into this step; when its note ends */
    uint32_t ticks;                    /* MIDI clock ticks into this step */
    int16_t sounding;                  /* the note on now, or -1 */
    float bpm;                         /* a host's tempo (the plugins), or 0: the patch's TEMPO */
} cr_arp_t;

void cr_arp_init(cr_arp_t *a, const cr_arp_out_t *out);
void cr_arp_key(cr_arp_t *a, uint32_t note, uint32_t vel);   /* vel 0: let go (latch is the app's) */
void cr_arp_release_all(cr_arp_t *a);  /* every note gone */
/* n samples of the patch's own tempo (when MIDI clock does not drive it) */
void cr_arp_run(cr_arp_t *a, const cr_patch_t *p, uint32_t n);
void cr_arp_clock(cr_arp_t *a, const cr_patch_t *p);   /* one MIDI clock tick */
float cr_arp_bpm(const cr_patch_t *p);
uint32_t cr_arp_ticks(const cr_patch_t *p);            /* MIDI clock ticks a step */
extern const char *const CR_ARPRATE_NAME[6];
