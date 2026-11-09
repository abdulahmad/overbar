/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The Game page: rows of the client's own options (game_ops). Overbar draws them, keeps them in
 * the ini and carries the net ones to every machine of an online session. What they do is the
 * client's (its changed hook).
 */
#include "overbar/overbar.h"

#include <stdlib.h>
#include <string.h>

static int nopts(const ob_host *h)
{
    int n = h && h->game_ready ? h->game_ops.nopts : 0;
    return n > OB_GAME_OPTS_MAX ? OB_GAME_OPTS_MAX : n;
}

static int clamp_choice(const ob_game_opt *o, int v)
{
    if (o->nchoices <= 0) return 0;
    return v < 0 || v >= o->nchoices ? o->def : v;
}

/* a choice by its name (as the ini keeps it), else a number; -1 if neither */
static int choice_of(const ob_game_opt *o, const char *v)
{
    int i;
    char *end;
    long n;
    for (i = 0; i < o->nchoices; i++)
        if (o->choices[i] && !strcmp(o->choices[i], v)) return i;
    n = strtol(v, &end, 10);
    if (end != v && *end == 0 && n >= 0 && n < o->nchoices) return (int)n;
    return -1;
}

static int find_key(const ob_host *h, const char *key)
{
    int i, n = nopts(h);
    for (i = 0; i < n; i++)
        if (h->game_ops.opts[i].key && !strcmp(h->game_ops.opts[i].key, key)) return i;
    return -1;
}

static void notify(ob_host *h, int i)
{
    if (h->game_ops.changed) h->game_ops.changed(h->game_ops.ctx, i, h->game_value[i]);
}

void ob_game_set_ops(ob_host *h, const ob_game_ops *ops)
{
    int i, n;
    if (!h) return;
    if (!ops || !ops->opts || ops->nopts <= 0) {
        memset(&h->game_ops, 0, sizeof h->game_ops);
        h->game_ready = 0;
        return;
    }
    h->game_ops = *ops;
    h->game_ready = 1;
    h->game_session = 0;
    n = nopts(h);
    for (i = 0; i < n; i++) h->game_value[i] = clamp_choice(&ops->opts[i], ops->opts[i].def);
    /* choices the ini gave before the options were known */
    {
        int kept = 0;
        for (i = 0; i < h->ngame_stash; i++) {
            int k = find_key(h, h->game_stash[i][0]);
            if (k < 0) {        /* not one of this build's: kept for the ini */
                if (kept != i) memcpy(h->game_stash[kept], h->game_stash[i], sizeof h->game_stash[i]);
                kept++;
            } else {
                int v = choice_of(&ops->opts[k], h->game_stash[i][1]);
                if (v >= 0) h->game_value[k] = v;
            }
        }
        h->ngame_stash = kept;
    }
    for (i = 0; i < n; i++) notify(h, i);
}

int ob_game_get(const ob_host *h, int index)
{
    if (index < 0 || index >= nopts(h)) return 0;
    return h->game_value[index];
}

void ob_game_set(ob_host *h, int index, int value)
{
    if (index < 0 || index >= nopts(h)) return;
    h->game_value[index] = clamp_choice(&h->game_ops.opts[index], value);
}

int ob_game_apply(ob_host *h, int id, int value)
{
    int i = id - OB_ID_GAME_OPT;
    if (!h || id < OB_ID_GAME_OPT || i >= nopts(h)) return 0;
    if (h->game_session && h->game_ops.opts[i].net) return 1;   /* the host's, for this session */
    h->game_value[i] = clamp_choice(&h->game_ops.opts[i], value);
    notify(h, i);
    if (h->ini_path[0]) ob_host_save_ini(h);
    return 1;
}

const char *ob_game_title(const ob_host *h)
{
    return h && h->game_ops.title ? h->game_ops.title : "Game";
}

int ob_game_items(const ob_host *h, ob_item *out, int cap)
{
    int i, n = 0, k = nopts(h);
    for (i = 0; i < k && n < cap; i++) {
        const ob_game_opt *o = &h->game_ops.opts[i];
        ob_item *it;
        if (i > 0 && o->group != h->game_ops.opts[i - 1].group && n < cap) {
            memset(&out[n], 0, sizeof out[n]);
            out[n++].kind = OB_ITEM_SEP;
            if (n >= cap) break;
        }
        it = &out[n++];
        memset(it, 0, sizeof *it);
        it->id = OB_ID_GAME_OPT + i;
        it->kind = MDRT_ITEM_CHOICE;
        it->label = o->label;
        it->choices = o->choices;
        it->nchoices = o->nchoices;
        it->value = h->game_value[i];
        if (h->game_session && o->net) snprintf(it->detail, sizeof it->detail, "the host's, online");
    }
    return n;
}

void ob_game_write_ini(const ob_host *h, FILE *f)
{
    int i, n = nopts(h);
    if (!f) return;
    for (i = 0; i < n; i++) {
        const ob_game_opt *o = &h->game_ops.opts[i];
        int v = h->game_session && o->net ? h->game_kept[i] : h->game_value[i];
        if (!o->key) continue;
        if (o->choices && v >= 0 && v < o->nchoices && o->choices[v]) fprintf(f, "game_%s=%s\n", o->key, o->choices[v]);
        else fprintf(f, "game_%s=%d\n", o->key, v);
    }
    /* lines for options this build does not give (yet) are kept */
    for (i = 0; i < h->ngame_stash; i++) fprintf(f, "game_%s=%s\n", h->game_stash[i][0], h->game_stash[i][1]);
}

int ob_game_read_ini(ob_host *h, const char *key, const char *value)
{
    int k, v;
    if (!h || strncmp(key, "game_", 5)) return 0;
    key += 5;
    k = find_key(h, key);
    if (k >= 0) {
        v = choice_of(&h->game_ops.opts[k], value);
        if (v >= 0 && v != h->game_value[k]) {
            h->game_value[k] = v;
            notify(h, k);
        }
        return 1;
    }
    if (h->ngame_stash < OB_GAME_OPTS_MAX && strlen(key) < 48 && strlen(value) < 48) {
        strcpy(h->game_stash[h->ngame_stash][0], key);
        strcpy(h->game_stash[h->ngame_stash][1], value);
        h->ngame_stash++;
    }
    return 1;
}

size_t ob_game_pack(const ob_host *h, char *buf, size_t size)
{
    size_t at = 0;
    int i, n = nopts(h);
    if (!buf || size == 0) return 0;
    buf[0] = 0;
    for (i = 0; i < n; i++) {
        const ob_game_opt *o = &h->game_ops.opts[i];
        int w;
        if (!o->net || !o->key) continue;
        w = snprintf(buf + at, size - at, "%s=%d\n", o->key, h->game_value[i]);
        if (w < 0 || (size_t)w >= size - at) { buf[0] = 0; return 0; }
        at += (size_t)w;
    }
    return at;
}

int ob_game_session_begin(ob_host *h, const void *data, size_t size)
{
    const char *p = (const char *)data, *end = p + size;
    int i, set = 0;
    if (!h || !h->game_ready) return 0;
    if (!h->game_session) memcpy(h->game_kept, h->game_value, sizeof h->game_kept);
    h->game_session = 1;
    while (p && p < end) {
        char line[128], *eq;
        size_t ln = 0;
        while (p < end && *p != '\n' && ln + 1 < sizeof line) line[ln++] = *p++;
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
        line[ln] = 0;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        i = find_key(h, line);
        if (i < 0 || !h->game_ops.opts[i].net) continue;
        {
            int v = clamp_choice(&h->game_ops.opts[i], atoi(eq));
            if (v != h->game_value[i]) { h->game_value[i] = v; notify(h, i); }
            set++;
        }
    }
    return set;
}

void ob_game_session_end(ob_host *h)
{
    int i, n = nopts(h);
    if (!h || !h->game_session) return;
    h->game_session = 0;
    for (i = 0; i < n; i++)
        if (h->game_ops.opts[i].net && h->game_value[i] != h->game_kept[i]) {
            h->game_value[i] = h->game_kept[i];
            notify(h, i);
        }
}
