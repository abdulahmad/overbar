/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Saves of an online match (overbar/save.h). A file, little-endian:
 *   "OBSV" u32, version u32, identity (length u8, bytes), key u32, kind u8, step u32, written i64,
 *   the summary: league, game, the teams, the players (each length u8, bytes), score x 2 i32, period i32,
 *   clock i32; then delay u8, the pads' owners (OB_NET_MAX_PADS x i8), settings (size u32, bytes),
 *   the shared state (size u32, bytes). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "overbar/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define make_dir(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p, 0755)
#endif

#define MAGIC 0x5653424Fu       /* "OBSV" */
#define VERSION 1u
#define HEAD_MAX 4096           /* enough for everything before the settings */

static const char kind_char[5] = { '?', 'f', 'c', 'p', 'd' };

static void text(char *to, const char *from, size_t max)
{
    size_t i = 0;
    if (from) for (; from[i] && i < max; i++) to[i] = (from[i] >= 32 && from[i] < 127) ? from[i] : '?';
    to[i] = 0;
}

int ob_saves_begin(ob_saves *s, const char *dir, const char *identity, const ob_net_match *m, uint32_t key)
{
    memset(s, 0, sizeof *s);
    snprintf(s->dir, sizeof s->dir, "%s", dir ? dir : ".");
    snprintf(s->identity, sizeof s->identity, "%s", identity ? identity : "");
    s->key = key ? key : (uint32_t)time(NULL) ^ (uint32_t)clock() << 20 ^ 0x9E3779B9u;
    s->delay = m ? m->delay : -1;
    memset(s->owner, -1, sizeof s->owner);
    if (m) memcpy(s->owner, m->owner, sizeof s->owner);
    if (m && m->settings_size) {
        s->settings = malloc(m->settings_size);
        if (!s->settings) { snprintf(s->why, sizeof s->why, "out of memory"); return -1; }
        memcpy(s->settings, m->settings, m->settings_size);
        s->settings_size = m->settings_size;
    }
    make_dir(s->dir);                               /* (there already: fine) */
    return 0;
}

void ob_saves_free(ob_saves *s)
{
    free(s->settings);
    s->settings = NULL;
    s->settings_size = 0;
}

void ob_saves_event(ob_saves *s, int kind, uint32_t step, const ob_save_summary *now)
{
    int i;
    if (kind < OB_SAVE_FACEOFF || kind > OB_SAVE_DROP || !now) return;
    s->now = *now;
    s->have_now = 1;
    if (step + 1 <= s->done_step[kind]) return;     /* (this point, or a later one, already: a replay) */
    s->done_step[kind] = step + 1;
    if (s->npend == 8) {                            /* (full: the oldest goes) */
        memmove(&s->pend[0], &s->pend[1], 7 * sizeof s->pend[0]);
        s->npend = 7;
    }
    i = s->npend++;
    s->pend[i].kind = kind;
    s->pend[i].step = step;
    s->pend[i].sum = *now;
}

void ob_saves_clock(ob_saves *s, uint32_t step, int game_seconds, const ob_save_summary *now)
{
    int mark = game_seconds / OB_SAVE_CLOCK_EVERY;
    if (now) { s->now = *now; s->have_now = 1; }
    if (game_seconds <= 0 || mark <= s->clock_mark) return;
    s->clock_mark = mark;
    ob_saves_event(s, OB_SAVE_CLOCK, step, now);
}

/* ---- the file ---- */

typedef struct wr { uint8_t *b; size_t n, cap; int bad; } wr;
static void put(wr *w, const void *p, size_t n)
{
    if (w->bad) return;
    if (w->n + n > w->cap) {
        size_t c = w->cap ? w->cap : 4096;
        uint8_t *nb;
        while (c < w->n + n) c *= 2;
        nb = realloc(w->b, c);
        if (!nb) { w->bad = 1; return; }
        w->b = nb; w->cap = c;
    }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void u8w(wr *w, uint32_t v) { uint8_t b = (uint8_t)v; put(w, &b, 1); }
static void u32w(wr *w, uint32_t v) { uint8_t b[4]; int i; for (i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i)); put(w, b, 4); }
static void i64w(wr *w, int64_t v) { uint8_t b[8]; int i; for (i = 0; i < 8; i++) b[i] = (uint8_t)((uint64_t)v >> (8 * i)); put(w, b, 8); }
static void strw(wr *w, const char *s) { size_t n = strlen(s); if (n > 255) n = 255; u8w(w, (uint32_t)n); put(w, s, n); }

typedef struct rd { const uint8_t *b; size_t n, at; int bad; } rd;
static const uint8_t *get(rd *r, size_t n) { const uint8_t *p; if (r->bad || r->at + n > r->n) { r->bad = 1; return NULL; } p = r->b + r->at; r->at += n; return p; }
static uint32_t u8r(rd *r) { const uint8_t *p = get(r, 1); return p ? p[0] : 0; }
static uint32_t u32r(rd *r) { const uint8_t *p = get(r, 4); return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0; }
static int64_t i64r(rd *r) { const uint8_t *p = get(r, 8); uint64_t v = 0; int i; if (p) for (i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return (int64_t)v; }
static void strr(rd *r, char *to, size_t max)
{
    size_t n = u8r(r);
    const uint8_t *p = get(r, n);
    char tmp[256];
    if (!p) { to[0] = 0; return; }
    memcpy(tmp, p, n);
    tmp[n] = 0;
    text(to, tmp, max);
}

static void summary_w(wr *w, const ob_save_summary *m)
{
    strw(w, m->league); strw(w, m->game); strw(w, m->team[0]); strw(w, m->team[1]); strw(w, m->players[0]); strw(w, m->players[1]);
    u32w(w, (uint32_t)m->score[0]); u32w(w, (uint32_t)m->score[1]); u32w(w, (uint32_t)m->period); u32w(w, (uint32_t)m->clock);
}

static void summary_r(rd *r, ob_save_summary *m)
{
    strr(r, m->league, OB_SAVE_TEXT); strr(r, m->game, OB_SAVE_TEXT); strr(r, m->team[0], OB_SAVE_TEXT); strr(r, m->team[1], OB_SAVE_TEXT);
    strr(r, m->players[0], OB_SAVE_TEXT); strr(r, m->players[1], OB_SAVE_TEXT);
    m->score[0] = (int)u32r(r); m->score[1] = (int)u32r(r); m->period = (int)u32r(r); m->clock = (int)u32r(r);
}

static void file_name(const ob_saves *s, int kind, int seq, char *out, size_t size)
{
    snprintf(out, size, "%s/m%08x-%c-%04d.obsave", s->dir, (unsigned)s->key, kind_char[kind], seq);
}

static int write_file(ob_saves *s, int kind, uint32_t step, const ob_save_summary *sum, const void *state, size_t state_n)
{
    wr w = { 0 };
    char path[400], tmp[410];
    FILE *f;
    int p;
    u32w(&w, MAGIC); u32w(&w, VERSION); strw(&w, s->identity); u32w(&w, s->key); u8w(&w, (uint32_t)kind); u32w(&w, step);
    i64w(&w, (int64_t)time(NULL));
    summary_w(&w, sum);
    u8w(&w, (uint32_t)(s->delay < 0 ? 255 : s->delay));
    for (p = 0; p < OB_NET_MAX_PADS; p++) u8w(&w, (uint32_t)(uint8_t)s->owner[p]);
    u32w(&w, (uint32_t)s->settings_size); if (s->settings_size) put(&w, s->settings, s->settings_size);
    u32w(&w, (uint32_t)state_n); put(&w, state, state_n);
    if (w.bad) { free(w.b); snprintf(s->why, sizeof s->why, "out of memory"); return -1; }
    file_name(s, kind, s->seq[kind], path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f || fwrite(w.b, 1, w.n, f) != w.n) {
        if (f) fclose(f);
        free(w.b);
        remove(tmp);
        snprintf(s->why, sizeof s->why, "could not write %.120s", path);
        return -1;
    }
    free(w.b);
    if (fclose(f)) { remove(tmp); snprintf(s->why, sizeof s->why, "could not write %.120s", path); return -1; }
    remove(path);                                   /* (Windows' rename does not replace) */
    if (rename(tmp, path)) { remove(tmp); snprintf(s->why, sizeof s->why, "could not name %.120s", path); return -1; }
    s->seq[kind]++;
    s->saved++;
    if (kind != OB_SAVE_PERIOD && s->seq[kind] > OB_SAVE_KEEP) {   /* the oldest of its kind past the last 3 */
        file_name(s, kind, s->seq[kind] - 1 - OB_SAVE_KEEP, path, sizeof path);
        remove(path);
    }
    return 0;
}

int ob_saves_poll_with(ob_saves *s, uint32_t confirmed, size_t (*share)(void *, uint32_t *, void *, size_t), void *ud)
{
    int wrote = 0;
    while (s->npend && s->pend[0].step < confirmed) {
        uint32_t at = 0;
        size_t need = share(ud, &at, NULL, 0), got;
        uint8_t *b;
        if (!need || at < s->pend[0].step) break;   /* (no state at or after the point yet: later) */
        b = malloc(need);
        if (!b) break;
        got = share(ud, &at, b, need);
        if (got == need && at >= s->pend[0].step) {
            if (!write_file(s, s->pend[0].kind, at, &s->pend[0].sum, b, got)) wrote++;
        } else if (got > need) { free(b); continue; }          /* (it grew: again) */
        free(b);
        memmove(&s->pend[0], &s->pend[1], (size_t)(s->npend - 1) * sizeof s->pend[0]);
        s->npend--;
    }
    return wrote;
}

static size_t net_share(void *ud, uint32_t *step, void *out, size_t cap) { return ob_net_share_confirmed((ob_net *)ud, step, out, cap); }

int ob_saves_poll(ob_saves *s, ob_net *n)
{
    unsigned w;
    if (!n || !ob_net_is_host(n)) return 0;
    w = ob_net_waiting_for(n);
    if (w & ~s->dropped && s->npend < 8) {               /* (a player dropped out: a save now, from any state) */
        int i = 0;                                       /* (first: the points after it wait for buttons that may not come) */
        memmove(&s->pend[1], &s->pend[0], (size_t)s->npend * sizeof s->pend[0]);
        s->npend++;
        s->pend[i].kind = OB_SAVE_DROP;
        s->pend[i].step = 0;
        if (s->have_now) s->pend[i].sum = s->now;
        else memset(&s->pend[i].sum, 0, sizeof s->pend[i].sum);
    }
    s->dropped = w;
    if (!s->npend) return 0;
    return ob_saves_poll_with(s, ob_net_confirmed(n), net_share, n);
}

void ob_saves_done(ob_saves *s)
{
    char path[400];
    int k, q;
    for (k = OB_SAVE_FACEOFF; k <= OB_SAVE_DROP; k++)
        for (q = 0; q < s->seq[k]; q++) { file_name(s, k, q, path, sizeof path); remove(path); }
    s->npend = 0;
}

/* ---- Recover game ---- */

/* the header of a save (all but its settings and state); 0, or -1 */
static int read_head(rd *r, const char *identity, ob_save_entry *e)
{
    char id[256];
    if (u32r(r) != MAGIC || u32r(r) != VERSION) return -1;
    strr(r, id, 255);
    if (r->bad || (identity && strcmp(id, identity))) return -1;
    e->key = u32r(r);
    e->kind = (int)u8r(r);
    e->step = u32r(r);
    e->when = i64r(r);
    summary_r(r, &e->sum);
    return r->bad || e->kind < OB_SAVE_FACEOFF || e->kind > OB_SAVE_DROP ? -1 : 0;
}

static int is_save(const char *name)
{
    size_t n = strlen(name);
    return n > 7 && !strcmp(name + n - 7, ".obsave");
}

static int add_entry(const char *dir, const char *name, const char *identity, ob_save_entry *e)
{
    uint8_t head[HEAD_MAX];
    size_t got;
    rd r;
    FILE *f;
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s/%s", dir, name);
    f = fopen(e->path, "rb");
    if (!f) return -1;
    got = fread(head, 1, sizeof head, f);
    fclose(f);
    r.b = head; r.n = got; r.at = 0; r.bad = 0;
    return read_head(&r, identity, e);
}

static int newer(const ob_save_entry *a, const ob_save_entry *b)
{
    if (a->when != b->when) return a->when > b->when;
    if (a->key == b->key) return a->step > b->step;
    return strcmp(a->path, b->path) > 0;
}

int ob_saves_list(const char *dir, const char *identity, ob_save_entry *e, int max)
{
    int n = 0, i, j;
    ob_save_entry t;
#ifdef _WIN32
    char pat[300];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof pat, "%s/*.obsave", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (is_save(fd.cFileName) && n < max && !add_entry(dir, fd.cFileName, identity, &e[n])) n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return 0;
    while ((de = readdir(d)))
        if (is_save(de->d_name) && n < max && !add_entry(dir, de->d_name, identity, &e[n])) n++;
    closedir(d);
#endif
    for (i = 1; i < n; i++) {                       /* newest first */
        t = e[i];
        for (j = i; j > 0 && newer(&t, &e[j - 1]); j--) e[j] = e[j - 1];
        e[j] = t;
    }
    return n;
}

void ob_save_line(const ob_save_entry *e, char *line, int size)
{
    static const char *nth[] = { "", "1st", "2nd", "3rd" };
    const ob_save_summary *m = &e->sum;
    char per[24], who[112], where[112];
    if (m->period >= 1 && m->period <= 3) snprintf(per, sizeof per, "%s period", nth[m->period]);
    else if (m->period > 3) snprintf(per, sizeof per, "overtime");
    else per[0] = 0;
    if (m->players[0][0] || m->players[1][0])
        snprintf(who, sizeof who, "  (%s vs %s)", m->players[0][0] ? m->players[0] : "CPU", m->players[1][0] ? m->players[1] : "CPU");
    else who[0] = 0;
    if (m->league[0] || m->game[0]) snprintf(where, sizeof where, "  %s%s%s", m->league, m->league[0] && m->game[0] ? ", " : "", m->game);
    else where[0] = 0;
    snprintf(line, (size_t)size, "%s %d:%02d  %s %d - %d %s%s%s", per, m->clock / 60, m->clock % 60, m->team[0], m->score[0],
             m->score[1], m->team[1], who, where);
}

int ob_save_load(const char *path, const char *identity, ob_save_file *f)
{
    FILE *fp;
    long len;
    uint8_t *b;
    rd r;
    int p;
    uint32_t n;
    const uint8_t *q;
    memset(f, 0, sizeof *f);
    fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) || (len = ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET)) { fclose(fp); return -1; }
    b = malloc((size_t)len);
    if (!b || fread(b, 1, (size_t)len, fp) != (size_t)len) { free(b); fclose(fp); return -1; }
    fclose(fp);
    r.b = b; r.n = (size_t)len; r.at = 0; r.bad = 0;
    snprintf(f->e.path, sizeof f->e.path, "%s", path);
    if (read_head(&r, identity, &f->e)) { free(b); return -1; }
    n = u8r(&r);
    f->delay = n == 255 ? -1 : (int)n;
    for (p = 0; p < OB_NET_MAX_PADS; p++) f->owner[p] = (int8_t)u8r(&r);
    n = u32r(&r);
    q = get(&r, n);
    if (q && n) { f->settings = malloc(n); if (f->settings) { memcpy(f->settings, q, n); f->settings_size = n; } else r.bad = 1; }
    n = u32r(&r);
    q = get(&r, n);
    if (q && n) { f->state = malloc(n); if (f->state) { memcpy(f->state, q, n); f->state_size = n; } else r.bad = 1; }
    free(b);
    if (r.bad || !f->state_size || r.at != r.n) { ob_save_file_free(f); return -1; }
    return 0;
}

void ob_save_match(const ob_save_file *f, ob_net_match *m)
{
    memset(m, 0, sizeof *m);
    memcpy(m->owner, f->owner, sizeof m->owner);
    m->delay = f->delay;
    m->settings = f->settings;
    m->settings_size = f->settings_size;
    m->state = f->state;
    m->state_size = f->state_size;
    m->state_step = f->e.step;
}

void ob_save_file_free(ob_save_file *f)
{
    free(f->settings);
    free(f->state);
    f->settings = f->state = NULL;
    f->settings_size = f->state_size = 0;
}
