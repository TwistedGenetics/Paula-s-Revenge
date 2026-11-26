/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: wavetable files, numbered. Every WAV under a host's Wavetables folder (sub-folders too) gets a number
 * that never changes, kept in TableIndex.txt next to it (one path a line, relative to the folder: line n is number
 * n, append-only), so a patch or project that names a table by number finds it again. The Twisted Tracker's
 * /Crafty/Wavetables, the plugin's and the MPC's folders all work this way, and a TableIndex.txt copied between
 * them keeps the numbers.
 *
 * Plain C, no allocation and no libc: the host owns the struct (about 260 KB), reads the index file into it, walks
 * its folders (cr_tables_seen() for each WAV), then cr_tables_done(), and appends the new lines it gets from
 * cr_tables_new_lines(). Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

#define CR_TABLES_MAX 4000u              /* numbers 1..4000; 0 is "no file: the bank" */
#define CR_TABLES_POOL (200u * 1024u)    /* the paths' bytes */
#define CR_TABLES_HASH 8192u
#define CR_TABLES_PATH 256u              /* the longest path kept (relative to the folder) */

typedef struct {
    char pool[CR_TABLES_POOL];
    uint32_t off[CR_TABLES_MAX + 1u];
    uint16_t hash[CR_TABLES_HASH];
    uint16_t sorted[CR_TABLES_MAX];      /* the present tables, by name (case ignored) */
    uint16_t pos[CR_TABLES_MAX + 1u];    /* each number's place in sorted (0xFFFF: not on the card) */
    uint8_t present[CR_TABLES_MAX + 1u];
    uint32_t count, saved, nsorted, used;
    uint32_t walked;                     /* cr_tables_done() has run */
} cr_tables_t;

void cr_tables_init(cr_tables_t *t);
/* the index file's text (any line ends); repeated lines keep their first number */
void cr_tables_load(cr_tables_t *t, const char *text, uint32_t n);
void cr_tables_begin(cr_tables_t *t);                        /* a walk starts: nothing present */
uint32_t cr_tables_seen(cr_tables_t *t, const char *path);   /* a WAV found ("Basses/Growl.wav"): its number (0: full) */
void cr_tables_done(cr_tables_t *t);                         /* the walk is over: the names sorted */
/* the index's new lines (numbers saved+1..count) as text into out (CRLF ends), its length; then they count as saved
 * (call again until it gives 0 when out is small) */
uint32_t cr_tables_new_lines(cr_tables_t *t, char *out, uint32_t n);

uint32_t cr_tables_find(const cr_tables_t *t, const char *path);  /* a path's number, or 0 */
const char *cr_tables_path(const cr_tables_t *t, uint32_t id);   /* a number's path, or 0 */
int cr_tables_present(const cr_tables_t *t, uint32_t id);        /* found by the last walk */
/* the next / previous table in name order from id (0: off), delta steps; by_folder: the first table of the next
 * (or this, or the previous) folder */
uint32_t cr_tables_step(const cr_tables_t *t, uint32_t id, int32_t delta, int by_folder);
uint32_t cr_tables_at(const cr_tables_t *t, uint32_t i);         /* the i-th present table by name (0: none) */
uint32_t cr_tables_random(const cr_tables_t *t, uint32_t *seed); /* any present table (0: none) */
/* a table's name to show: its file name without the folder and ".wav" */
void cr_tables_label(const cr_tables_t *t, uint32_t id, char *b, uint32_t n);
