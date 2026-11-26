/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the editor protocol (docs/PROTOCOL.md, protocol 2) answered by a plugin instead of the FM-1, over
 * a user area in RAM (src/core/cr_user.h) that the host keeps in a file. The editor can't tell the difference: the
 * same presets, user slots and wavetable banks, the same commands. Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include "cr_inst.h"

typedef struct {
    uint8_t user[CR_USER_BYTES];        /* the user area: starts erased (0xFF), or loaded from the host's file */
    cr_ubank_t in;                      /* a bank coming in from the editor (WAVE PUT, then WAVE COMMIT) */
    uint8_t user_dirty;                 /* the user area changed: the host writes its file */
    const char *name;                   /* INFO's name, e.g. "Crafty Synth AU 0.5" */
} cr_store_t;

enum { CR_ED_INFO = 40, CR_ED_PGET, CR_ED_PPUT, CR_ED_SELECT, CR_ED_GGET, CR_ED_GPUT, CR_ED_WPUT, CR_ED_WCOMMIT,
       CR_ED_EVT, CR_ED_WGET, CR_ED_PRESET_RESET };

void cr_store_init(cr_store_t *st, const char *name);       /* an empty user area */
/* one editor command (cmd and its 7-bit argument bytes, without the F0 7D 46 4C header or F7): the reply's bytes go
 * to put (7-bit). Returns 1 when it answered, 0 for a command it doesn't know or broken arguments. The instance's
 * user area must be st->user (cr_inst_set_user). */
int cr_proto_cmd(cr_store_t *st, cr_inst_t *in, uint32_t cmd, const uint8_t *a, uint32_t na,
                 void (*put)(void *ctx, uint32_t b), void *ctx);
/* the event the FM-1 sends when a preset was loaded (kind 1) or saved (2), as a whole frame F0 .. F7 (10 bytes) */
void cr_proto_event(const cr_inst_t *in, uint32_t kind, uint8_t out[10]);
