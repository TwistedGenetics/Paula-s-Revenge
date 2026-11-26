/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the user area, as the FM-1 keeps it in flash and the plugins keep it in a file ("Crafty Synth.user"):
 * 128 user presets (128 B each, 16 KB), then 32 user wavetable banks (a 4 KB sector each). Erased bytes are 0xFF;
 * a slot or bank is in use when its magic and version are right. The same bytes everywhere, so a file can move
 * between the plugin, the MPC and (through the editor) the FM-1. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_patch.h"
#include "cr_wt.h"

typedef struct {
    uint8_t magic, ver;
    char name[14];
    int16_t w[5][CR_WT_N];
} cr_ubank_t;                                    /* a user wavetable bank (Serum import): 2,576 B */
#define CR_UBANK_MAGIC 0xC7u
#define CR_FL_PRESETS 0x00000u                   /* 128 x 128 B */
#define CR_FL_BANKS 0x04000u                     /* 32 x 4 KB */
#define CR_SECTOR 0x1000u
#define CR_USER_BYTES (CR_FL_BANKS + CR_NUBANK * CR_SECTOR)   /* 144 KB */
_Static_assert(sizeof(cr_ubank_t) == 2576u, "a user bank's bytes (the editor, the files)");
_Static_assert(CR_NUSER * CR_PATCH_BYTES <= CR_FL_BANKS, "the user presets' sectors");
