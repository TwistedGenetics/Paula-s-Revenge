/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: a parameter's value as text, the FM-1's way ("2.1K", "1/8D", "LADDER"), shared by the FM-1's
 * screen, the plugins' host displays and the MPC's skin. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_patch.h"

/* the text of parameter id's value in patch p, into b (at least 24 chars): short (a knob's cell) or long (lng).
 * uname gives a user bank's name (u 0..31), "" for one with no name, or 0 for an empty bank; uname may be 0. */
void cr_param_text(const cr_patch_t *p, uint32_t id, char *b, int lng, const char *(*uname)(void *ctx, uint32_t u),
                   void *ctx);
/* the page a parameter sits on where its label alone is ambiguous ("FILT EG" for ATTACK), else "" */
const char *cr_param_page(uint32_t id);
