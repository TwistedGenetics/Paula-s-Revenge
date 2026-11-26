/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the editor protocol over a RAM user area (cr_proto.h). The FM-1's own is src/app/cr_editor.c; the
 * commands and replies here are the same. Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_proto.h"

#define CHUNK 256u
#define PROTO 2u

static void pack(void (*put)(void *, uint32_t), void *ctx, const uint8_t *b, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7u) {
        uint32_t m = 0, k = n - i < 7u ? n - i : 7u;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(b[i + j] >> 7) << j;
        put(ctx, m);
        for (j = 0; j < k; j++)
            put(ctx, b[i + j] & 0x7Fu);
    }
}
static uint32_t unpack(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static uint32_t slot_of(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7; }
static void put_slot(void (*put)(void *, uint32_t), void *ctx, uint32_t s)
{
    put(ctx, s & 0x7Fu);
    put(ctx, (s >> 7) & 0x7Fu);
}
static void write_bytes(cr_store_t *st, uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        st->user[off + i] = src ? ((const uint8_t *)src)[i] : 0xFFu;
    st->user_dirty = 1;
}

void cr_store_init(cr_store_t *st, const char *name)
{
    uint32_t i;
    for (i = 0; i < CR_USER_BYTES; i++)
        st->user[i] = 0xFFu;
    st->user_dirty = 0;
    st->name = name;
}

void cr_proto_event(const cr_inst_t *in, uint32_t kind, uint8_t ev[10])
{
    ev[0] = 0xF0; ev[1] = 0x7D; ev[2] = 0x46; ev[3] = 0x4C; ev[4] = CR_ED_EVT;
    ev[5] = (uint8_t)(kind & 0x7Fu);
    ev[6] = (uint8_t)(in->cur & 0x7Fu);
    ev[7] = (uint8_t)(in->cur >> 7);
    ev[8] = in->dirty;
    ev[9] = 0xF7;
}

int cr_proto_cmd(cr_store_t *st, cr_inst_t *in, uint32_t cmd, const uint8_t *a, uint32_t na,
                 void (*put)(void *ctx, uint32_t b), void *ctx)
{
    static const uint8_t none[1] = {0};
    cr_patch_t t;
    uint32_t i, s;
    if (!a)
        a = none;
    switch (cmd) {
    case CR_ED_INFO: {
        const char *v = st->name ? st->name : "Crafty Synth";
        for (i = 0; v[i] && i < 28u; i++)
            put(ctx, (uint8_t)v[i] & 0x7Fu);
        put(ctx, 0);
        put(ctx, PROTO);
        put(ctx, CR_NFACTORY);
        put(ctx, CR_NUSER & 0x7Fu);
        put(ctx, CR_NUSER >> 7);
        put_slot(put, ctx, in->cur);
        put(ctx, in->dirty);
        put(ctx, CR_NUBANK);
        for (i = 0; i < CR_NUBANK; i++)
            put(ctx, cr_user_bank(st->user, i) != 0);
        return 1;
    }
    case CR_ED_PGET:
        if (na < 2u)
            return 0;
        s = slot_of(a);
        if (s == CR_WORKING)
            t = in->p;
        else if (s < CR_NPRESET)
            (void)cr_preset_get(st->user, s, &t);
        else
            return 0;
        put_slot(put, ctx, s);
        put(ctx, s < CR_NPRESET ? (uint32_t)(s < CR_NFACTORY || cr_user_saved(st->user, s)) : (uint32_t)in->dirty);
        pack(put, ctx, (const uint8_t *)&t, CR_PATCH_BYTES);
        return 1;
    case CR_ED_PPUT:
        if (na < 3u)
            return 0;
        s = slot_of(a);
        if (unpack(a + 2, na - 2u, (uint8_t *)&t, CR_PATCH_BYTES) != CR_PATCH_BYTES || cr_patch_check(&t))
            return 0;
        put_slot(put, ctx, s);
        if (s == CR_WORKING) {
            uint16_t cur = in->cur;
            cr_inst_use(in, &t);
            in->cur = cur;
            in->dirty = 1;
            put(ctx, 0);
        } else if (s >= CR_NFACTORY && s < CR_NPRESET) {
            write_bytes(st, CR_FL_PRESETS + (s - CR_NFACTORY) * CR_PATCH_BYTES, &t, CR_PATCH_BYTES);
            if (s == in->cur && !in->dirty)
                cr_inst_use(in, &t);
            if (s == in->cur)
                in->dirty = 0;
            put(ctx, 0);
        } else
            put(ctx, 2);                         /* the factory presets can't be written */
        return 1;
    case CR_ED_SELECT:
        if (na < 2u || (s = slot_of(a)) >= CR_NPRESET)
            return 0;
        cr_inst_all_off(in);
        cr_inst_load(in, s);
        put_slot(put, ctx, s);
        return 1;
    case CR_ED_PRESET_RESET:
        if (na < 2u || (s = slot_of(a)) < CR_NFACTORY || s >= CR_NPRESET)
            return 0;
        write_bytes(st, CR_FL_PRESETS + (s - CR_NFACTORY) * CR_PATCH_BYTES, 0, CR_PATCH_BYTES);
        put_slot(put, ctx, s);
        put(ctx, 0);
        return 1;
    case CR_ED_GGET:
        pack(put, ctx, (const uint8_t *)&in->g, sizeof in->g);
        return 1;
    case CR_ED_GPUT: {
        cr_globals_t g;
        if (unpack(a, na, (uint8_t *)&g, sizeof g) != sizeof g || cr_globals_check(&g))
            return 0;
        g.cur = in->g.cur;
        in->g = g;
        put(ctx, 0);
        return 1;
    }
    case CR_ED_WPUT: {
        uint32_t u, c, n, off;
        if (na < 3u)
            return 0;
        u = a[0];
        c = a[1];
        off = c * CHUNK;
        if (u >= CR_NUBANK || off >= sizeof(cr_ubank_t))
            return 0;
        n = sizeof(cr_ubank_t) - off < CHUNK ? sizeof(cr_ubank_t) - off : CHUNK;
        if (unpack(a + 2, na - 2u, (uint8_t *)&st->in + off, n) != n)
            return 0;
        put(ctx, u);
        put(ctx, c);
        return 1;
    }
    case CR_ED_WCOMMIT: {
        uint32_t u;
        if (na < 1u || a[0] >= CR_NUBANK)
            return 0;
        u = a[0];
        put(ctx, u);
        if (st->in.magic != CR_UBANK_MAGIC || st->in.ver != 1u) {
            put(ctx, 2);
            return 1;
        }
        st->in.name[sizeof st->in.name - 1u] = 0;
        write_bytes(st, CR_FL_BANKS + u * CR_SECTOR, &st->in, sizeof st->in);
        cr_inst_user_changed(in);
        put(ctx, 0);
        return 1;
    }
    case CR_ED_WGET: {
        uint32_t u, c, n, off;
        const cr_ubank_t *b;
        if (na < 2u || a[0] >= CR_NUBANK)
            return 0;
        u = a[0];
        c = a[1];
        off = c * CHUNK;
        if (off >= sizeof(cr_ubank_t))
            return 0;
        b = cr_user_bank(st->user, u);
        n = sizeof(cr_ubank_t) - off < CHUNK ? sizeof(cr_ubank_t) - off : CHUNK;
        put(ctx, u);
        put(ctx, c);
        if (b)
            pack(put, ctx, (const uint8_t *)b + off, n);
        return 1;
    }
    default:
        return 0;
    }
}
