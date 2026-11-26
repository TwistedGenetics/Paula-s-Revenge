/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: wavetable files, numbered (see cr_tables.h; the Twisted Tracker's CraftyTables.cpp, in shared C).
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_tables.h"
#include "cr_patch.h"

static char tb_low(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static uint32_t tb_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)tb_low(*s)) * 16777619u;
    return h;
}
static int tb_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = tb_low(*a), y = tb_low(*b);
        if (x != y || !x)
            return (int)(uint8_t)x - (int)(uint8_t)y;
    }
}
static uint32_t tb_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static const char *tb_name(const cr_tables_t *t, uint32_t id)
{
    return id >= 1u && id <= t->count ? t->pool + t->off[id] : 0;
}

void cr_tables_init(cr_tables_t *t)
{
    uint32_t i;
    t->count = t->saved = t->nsorted = t->used = t->walked = 0;
    for (i = 0; i < CR_TABLES_HASH; i++)
        t->hash[i] = 0;
    for (i = 0; i <= CR_TABLES_MAX; i++) {
        t->present[i] = 0;
        t->pos[i] = 0xFFFFu;
    }
}

uint32_t cr_tables_find(const cr_tables_t *t, const char *path)
{
    uint32_t h;
    for (h = tb_hash(path) & (CR_TABLES_HASH - 1u);; h = (h + 1u) & (CR_TABLES_HASH - 1u)) {
        uint32_t id = t->hash[h];
        if (!id)
            return 0;
        if (!tb_cmp(tb_name(t, id), path))
            return id;
    }
}
static uint32_t tb_add(cr_tables_t *t, const char *path)
{
    uint32_t len = tb_len(path) + 1u, id, h, i;
    if (t->count >= CR_TABLES_MAX || t->used + len > CR_TABLES_POOL || len < 2u || len > CR_TABLES_PATH)
        return 0;
    id = ++t->count;
    t->off[id] = t->used;
    for (i = 0; i < len; i++)
        t->pool[t->used + i] = path[i];
    t->used += len;
    t->present[id] = 0;
    for (h = tb_hash(path) & (CR_TABLES_HASH - 1u); t->hash[h]; h = (h + 1u) & (CR_TABLES_HASH - 1u))
        ;
    t->hash[h] = (uint16_t)id;
    return id;
}

void cr_tables_load(cr_tables_t *t, const char *text, uint32_t n)
{
    char line[CR_TABLES_PATH];
    uint32_t len = 0, i;
    for (i = 0; i <= n; i++) {
        char c = i < n ? text[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (len) {
                line[len] = 0;
                if (!cr_tables_find(t, line))
                    tb_add(t, line);
                len = 0;
            }
            continue;
        }
        if (len < CR_TABLES_PATH - 1u)
            line[len++] = c;
    }
    t->saved = t->count;
}

void cr_tables_begin(cr_tables_t *t)
{
    uint32_t i;
    for (i = 0; i <= CR_TABLES_MAX; i++)
        t->present[i] = 0;
}
uint32_t cr_tables_seen(cr_tables_t *t, const char *path)
{
    uint32_t id = cr_tables_find(t, path);
    if (!id)
        id = tb_add(t, path);
    if (id)
        t->present[id] = 1;
    return id;
}

/* the present numbers by name: a heap sort (no libc, no extra memory) */
static void tb_sift(cr_tables_t *t, uint32_t i, uint32_t n)
{
    for (;;) {
        uint32_t c = 2u * i + 1u;
        uint16_t x;
        if (c >= n)
            return;
        if (c + 1u < n && tb_cmp(tb_name(t, t->sorted[c + 1u]), tb_name(t, t->sorted[c])) > 0)
            c++;
        if (tb_cmp(tb_name(t, t->sorted[c]), tb_name(t, t->sorted[i])) <= 0)
            return;
        x = t->sorted[i];
        t->sorted[i] = t->sorted[c];
        t->sorted[c] = x;
        i = c;
    }
}
void cr_tables_done(cr_tables_t *t)
{
    uint32_t id, i, n = 0;
    for (id = 1; id <= t->count; id++)
        if (t->present[id])
            t->sorted[n++] = (uint16_t)id;
    for (i = n / 2u; i-- > 0;)
        tb_sift(t, i, n);
    for (i = n; i > 1u; i--) {
        uint16_t x = t->sorted[0];
        t->sorted[0] = t->sorted[i - 1u];
        t->sorted[i - 1u] = x;
        tb_sift(t, 0, i - 1u);
    }
    t->nsorted = n;
    for (i = 0; i <= CR_TABLES_MAX; i++)
        t->pos[i] = 0xFFFFu;
    for (i = 0; i < n; i++)
        t->pos[t->sorted[i]] = (uint16_t)i;
    t->walked = 1;
}

uint32_t cr_tables_new_lines(cr_tables_t *t, char *out, uint32_t n)
{
    uint32_t k = 0;
    while (t->saved < t->count) {
        const char *p = tb_name(t, t->saved + 1u);
        uint32_t len = tb_len(p), i;
        if (k + len + 2u > n)
            break;
        for (i = 0; i < len; i++)
            out[k++] = p[i];
        out[k++] = '\r';
        out[k++] = '\n';
        t->saved++;
    }
    return k;
}

const char *cr_tables_path(const cr_tables_t *t, uint32_t id) { return tb_name(t, id); }
int cr_tables_present(const cr_tables_t *t, uint32_t id)
{
    return id >= 1u && id <= t->count && t->present[id] && t->pos[id] != 0xFFFFu;
}
uint32_t cr_tables_at(const cr_tables_t *t, uint32_t i) { return i < t->nsorted ? t->sorted[i] : 0u; }

static uint32_t tb_folder_len(const char *p)
{
    uint32_t n = 0, i;
    for (i = 0; p[i]; i++)
        if (p[i] == '/')
            n = i;
    return n;
}
static int tb_same_folder(const cr_tables_t *t, uint32_t a, uint32_t b)
{
    const char *pa = tb_name(t, a), *pb = tb_name(t, b);
    uint32_t la = tb_folder_len(pa), lb = tb_folder_len(pb), i;
    if (la != lb)
        return 0;
    for (i = 0; i < la; i++)
        if (tb_low(pa[i]) != tb_low(pb[i]))
            return 0;
    return 1;
}
uint32_t cr_tables_step(const cr_tables_t *t, uint32_t id, int32_t delta, int by_folder)
{
    int32_t pos, n = (int32_t)t->nsorted, s;
    if (!n || !delta)
        return id;
    pos = cr_tables_present(t, id) ? (int32_t)t->pos[id] : -1;     /* -1: off */
    if (!by_folder) {
        pos += delta;
        pos = pos < -1 ? -1 : pos > n - 1 ? n - 1 : pos;
        return pos < 0 ? 0u : t->sorted[pos];
    }
    for (s = 0; s < (delta < 0 ? -delta : delta); s++) {
        if (delta > 0) {
            int32_t p;
            if (pos < 0) {
                pos = 0;
                continue;
            }
            for (p = pos; p < n - 1 && tb_same_folder(t, t->sorted[p], t->sorted[pos]); p++)
                ;
            if (tb_same_folder(t, t->sorted[p], t->sorted[pos]))
                break;                          /* the last folder */
            pos = p;
        } else {
            int32_t first = pos, p;
            if (pos <= 0) {
                pos = -1;
                break;
            }
            while (first > 0 && tb_same_folder(t, t->sorted[first - 1], t->sorted[pos]))
                first--;
            if (first < pos) {
                pos = first;                    /* back to this folder's first table */
                continue;
            }
            for (p = pos - 1; p > 0 && tb_same_folder(t, t->sorted[p - 1], t->sorted[pos - 1]); p--)
                ;
            pos = p;
        }
    }
    return pos < 0 ? 0u : t->sorted[pos];
}
uint32_t cr_tables_random(const cr_tables_t *t, uint32_t *seed)
{
    return t->nsorted ? t->sorted[cr_rand(seed) % t->nsorted] : 0u;
}

void cr_tables_label(const cr_tables_t *t, uint32_t id, char *b, uint32_t n)
{
    const char *p = tb_name(t, id);
    uint32_t s, e, k = 0, i;
    if (!n)
        return;
    if (!p) {
        b[0] = 0;
        return;
    }
    s = tb_folder_len(p);
    if (p[s] == '/')
        s++;
    e = tb_len(p);
    if (e - s > 4u && p[e - 4] == '.' && tb_low(p[e - 3]) == 'w' && tb_low(p[e - 2]) == 'a' && tb_low(p[e - 1]) == 'v')
        e -= 4u;
    for (i = s; i < e && k + 1u < n; i++)
        b[k++] = p[i];
    b[k] = 0;
}
