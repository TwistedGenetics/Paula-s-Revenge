/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: wavetables from WAV files (Serum's, Vital's, or any single-cycle WAV), the one importer every version
 * uses (the editor through WebAssembly, the AU / VST3 and MPC plugins, the Twisted Tracker's CRAFTY): five frames
 * picked from the table, each band-limited to 128 harmonics and resampled to 256 samples (the firmware's wave), the
 * five normalised to the firmware's peak.
 *
 * Plain C, no allocation and no libc. The file is read through a callback, a piece at a time (the host may keep it on
 * an SD card); the work memory (cr_wav_work_t, about 58 KB) is the host's.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>
#include "cr_user.h"
#include "cr_wt.h"

#define CR_WAV_MAXFRAME 4096u            /* the longest frame (one cycle) read */
#define CR_WAV_FRAME 2048u               /* a frame's length when the file doesn't say (Serum's 'clm ' chunk does) */
#define CR_WAV_PEAK 26000                /* the firmware's wavetable peak */

/* read n bytes at off into buf: the number read (fewer at the end of the file) */
typedef uint32_t (*cr_wav_read_t)(void *ctx, uint32_t off, void *buf, uint32_t n);

typedef struct {
    cr_wav_read_t read;
    void *ctx;
    uint32_t size;                       /* the file's bytes */
    uint32_t data_off, data_len;         /* the samples */
    uint16_t format, ch, bits;           /* 1 PCM (8, 16, 24, 32 bit), 3 float (32 bit) */
    uint32_t frame_size, frames;         /* the table: frames of frame_size samples (one frame: a single cycle) */
} cr_wav_t;

typedef struct {
    float frame[CR_WAV_MAXFRAME];
    float cs[CR_WAV_MAXFRAME], sn[CR_WAV_MAXFRAME];
    float c256[CR_WT_N], s256[CR_WT_N];
    float wave[5][CR_WT_N];
    uint8_t io[512];
} cr_wav_work_t;

/* the file's format and table: 1, or 0 when it's not a WAV this reads (or a frame is over CR_WAV_MAXFRAME) */
int cr_wav_open(cr_wav_t *w, cr_wav_read_t read, void *ctx, uint32_t size);
/* the five frames spread evenly through the table (what a file in a folder gets) */
void cr_wav_picks(uint32_t frames, uint32_t picks[5]);
/* the bank: the frames picks names (0: cr_wav_picks') into out; 1, or 0 (unreadable, or silent) */
int cr_wav_bank(const cr_wav_t *w, const uint32_t picks[5], int16_t out[5][CR_WT_N], cr_wav_work_t *k);
/* a bank's name from a file's ("Growl Bass.wav" -> "GROWL BASS"): capitals, the screen's characters, 13 at most */
void cr_wav_name(const char *file, char name[14]);
/* a user bank record (cr_user.h): name and waves */
void cr_ubank_make(cr_ubank_t *b, const char *name, const int16_t w[5][CR_WT_N]);
