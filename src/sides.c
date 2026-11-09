/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The select-sides screen (overbar/sides.h). Its messages (OB_NET_TYPE_SIDES), kind u8 first:
 *   ADD     local u8, name (length u8, bytes)          a guest's device, to the host
 *   REMOVE  local u8                                   a guest's device gone, to the host
 *   INPUT   local u8, input u8                         a press on a guest's device, to the host
 *   STATE   open u8, done u8, the teams' names (each length u8, bytes), devices u8, then each:
 *           owner u8, local u8, team + 1 u8, ready u8, name     the host's, to every guest */
#include "overbar/sides.h"
#include <string.h>
#include "mdrt/menu.h"

enum { K_ADD = 1, K_REMOVE, K_INPUT, K_STATE };

static void name_copy(char *to, const char *from, size_t n)
{
    size_t i = 0;
    if (from)
        for (; i < n && i < OB_SIDES_NAME && from[i]; i++) to[i] = (from[i] >= 32 && from[i] < 127) ? from[i] : '?';
    to[i] = 0;
}

void ob_sides_init(ob_sides *s, const ob_sides_game *g, int me, void (*send)(void *, int, const void *, size_t), void *ud)
{
    int t;
    memset(s, 0, sizeof *s);
    s->g = *g;
    if (s->g.teams < 1) s->g.teams = 1;
    if (s->g.teams > OB_SIDES_MAX_TEAMS) s->g.teams = OB_SIDES_MAX_TEAMS;
    if (s->g.seats < 1 || s->g.seats > OB_SIDES_MAX_SEATS) s->g.seats = s->g.seats < 1 ? 1 : OB_SIDES_MAX_SEATS;
    if (s->g.per_team <= 0 || s->g.per_team > s->g.seats) s->g.per_team = s->g.seats;
    for (t = 0; t < s->g.teams; t++) name_copy(s->team[t], g->team_name[t] ? g->team_name[t] : (t ? "Home" : "Away"), 64);
    s->me = me;
    s->online = send != NULL;
    s->host = !s->online || me == 0;
    s->send = send;
    s->send_ud = ud;
}

/* ---- the messages ---- */

typedef struct wr { uint8_t b[16 + OB_SIDES_MAX_TEAMS * 25 + OB_SIDES_MAX_DEVICES * 28]; size_t n; } wr;
static void w8(wr *w, int v) { if (w->n < sizeof w->b) w->b[w->n++] = (uint8_t)v; }
static void wstr(wr *w, const char *s) { size_t i, n = strlen(s); w8(w, (int)n); for (i = 0; i < n; i++) w8(w, s[i]); }

typedef struct rd { const uint8_t *b; size_t n, at; int bad; } rd;
static int r8(rd *r) { if (r->at >= r->n) { r->bad = 1; return 0; } return r->b[r->at++]; }
static void rstr(rd *r, char *to)
{
    size_t n = (size_t)r8(r);
    if (r->at + n > r->n) { r->bad = 1; to[0] = 0; return; }
    name_copy(to, (const char *)r->b + r->at, n);
    r->at += n;
}

static void send_state(ob_sides *s, int to)
{
    wr w;
    int t, i;
    if (!s->send || !s->host) return;
    w.n = 0;
    w8(&w, K_STATE); w8(&w, s->open); w8(&w, s->done);
    for (t = 0; t < s->g.teams; t++) wstr(&w, s->team[t]);
    w8(&w, s->n);
    for (i = 0; i < s->n; i++) {
        const ob_sides_device *d = &s->d[i];
        w8(&w, d->owner); w8(&w, d->local); w8(&w, d->team + 1); w8(&w, d->ready); wstr(&w, d->name);
    }
    s->send(s->send_ud, to, w.b, w.n);
}

static void changed(ob_sides *s)
{
    s->changes++;
    send_state(s, -1);
}

/* ---- the model ---- */

int ob_sides_find(const ob_sides *s, int owner, int local)
{
    int i;
    for (i = 0; i < s->n; i++) if (s->d[i].owner == owner && s->d[i].local == local) return i;
    return -1;
}

static int on_team(const ob_sides *s, int team)
{
    int i, c = 0;
    for (i = 0; i < s->n; i++) if (s->d[i].team == team) c++;
    return c;
}

static int seated(const ob_sides *s)
{
    int i, c = 0;
    for (i = 0; i < s->n; i++) if (s->d[i].team != OB_SIDES_MIDDLE) c++;
    return c;
}

int ob_sides_all_ready(const ob_sides *s)
{
    int i;
    for (i = 0; i < s->n; i++) if (s->d[i].team != OB_SIDES_MIDDLE && !s->d[i].ready) return 0;
    return 1;
}

void ob_sides_open(ob_sides *s)
{
    int i;
    if (!s->host) { s->open = 1; return; }        /* a guest's shows; the host's state says what is on it */
    for (i = 0; i < s->n; i++) { s->d[i].team = OB_SIDES_MIDDLE; s->d[i].ready = 0; }
    s->open = 1;
    s->done = 0;
    changed(s);
}

void ob_sides_close(ob_sides *s)
{
    s->open = 0;
    if (s->host) changed(s);
}

void ob_sides_set_team(ob_sides *s, int team, const char *name)
{
    if (!s->host || team < 0 || team >= s->g.teams) return;
    name_copy(s->team[team], name, 64);
    changed(s);
}

static int add(ob_sides *s, int owner, int local, const char *name)
{
    int i = ob_sides_find(s, owner, local);
    if (i < 0) {
        if (s->n >= OB_SIDES_MAX_DEVICES) return -1;
        i = s->n++;
        s->d[i].owner = (int8_t)owner;
        s->d[i].local = (int8_t)local;
        s->d[i].team = OB_SIDES_MIDDLE;
        s->d[i].ready = 0;
    }
    name_copy(s->d[i].name, name, 64);
    changed(s);
    return i;
}

static void drop(ob_sides *s, int i)
{
    memmove(&s->d[i], &s->d[i + 1], (size_t)(s->n - i - 1) * sizeof s->d[0]);
    s->n--;
}

static void say_add(ob_sides *s, int local, const char *name)
{
    wr w;
    w.n = 0;
    w8(&w, K_ADD); w8(&w, local); wstr(&w, name);
    s->send(s->send_ud, 0, w.b, w.n);
}

int ob_sides_add(ob_sides *s, int local, const char *name)
{
    int i;
    if (local < 0 || local > 127) return -1;
    if (!s->host) {
        for (i = 0; i < s->nmine && s->mine[i].local != local; i++) {}
        if (i == s->nmine) {
            if (s->nmine >= OB_SIDES_MAX_DEVICES) return -1;
            s->nmine++;
        }
        s->mine[i].local = (int8_t)local;
        name_copy(s->mine[i].name, name, 64);
        say_add(s, local, s->mine[i].name);
        return ob_sides_find(s, s->me, local);
    }
    return add(s, s->me, local, name);
}

void ob_sides_remove(ob_sides *s, int local)
{
    int i;
    if (!s->host) {
        wr w;
        for (i = 0; i < s->nmine; i++)
            if (s->mine[i].local == local) { memmove(&s->mine[i], &s->mine[i + 1], (size_t)(s->nmine - i - 1) * sizeof s->mine[0]); s->nmine--; break; }
        w.n = 0;
        w8(&w, K_REMOVE); w8(&w, local);
        s->send(s->send_ud, 0, w.b, w.n);
        return;
    }
    if ((i = ob_sides_find(s, s->me, local)) >= 0) { drop(s, i); changed(s); }
}

void ob_sides_machine_gone(ob_sides *s, int machine)
{
    int i, any = 0;
    if (!s->host) return;
    for (i = s->n - 1; i >= 0; i--) if (s->d[i].owner == machine) { drop(s, i); any = 1; }
    if (any) changed(s);
}

void ob_sides_machine_came(ob_sides *s, int machine)
{
    int i;
    if (s->host) { send_state(s, machine); return; }
    if (machine == 0 && s->send)
        for (i = 0; i < s->nmine; i++) say_add(s, s->mine[i].local, s->mine[i].name);
}

static void commit(ob_sides *s)
{
    ob_sides_seat seat[OB_SIDES_MAX_SEATS];
    int n = ob_sides_seats(s, seat, OB_SIDES_MAX_SEATS);
    s->done = 1;
    s->open = 0;
    changed(s);
    if (s->g.apply) s->g.apply(s->g.ud, seat, n);
}

/* (the deciding machine) an input from a device; 1 when used */
static int apply(ob_sides *s, int owner, int local, int in)
{
    int i = ob_sides_find(s, owner, local), to;
    ob_sides_device *d;
    if (!s->open || s->done) return 0;
    if (i < 0) return 1;
    d = &s->d[i];
    switch (in) {
    case MDRT_MENU_LEFT:
    case MDRT_MENU_RIGHT:
        if (s->g.teams == 1) to = d->team == OB_SIDES_MIDDLE ? 0 : OB_SIDES_MIDDLE;
        else if (in == MDRT_MENU_LEFT) to = d->team == OB_SIDES_MIDDLE ? 0 : d->team == 0 ? 0 : d->team == s->g.teams - 1 ? OB_SIDES_MIDDLE : d->team - 1;
        else to = d->team == OB_SIDES_MIDDLE ? s->g.teams - 1 : d->team == s->g.teams - 1 ? d->team : d->team == 0 ? OB_SIDES_MIDDLE : d->team + 1;
        if (to == d->team) return 1;
        if (to != OB_SIDES_MIDDLE && (on_team(s, to) >= s->g.per_team || (d->team == OB_SIDES_MIDDLE && seated(s) >= s->g.seats)))
            return 1;                                  /* that side is full */
        d->team = (int8_t)to;
        d->ready = 0;
        changed(s);
        return 1;
    case MDRT_MENU_OK:
        if (d->team != OB_SIDES_MIDDLE && !d->ready) { d->ready = 1; changed(s); return 1; }
        if (owner == 0 && ob_sides_all_ready(s)) commit(s);    /* the deciding machine starts the game */
        return 1;
    case MDRT_MENU_BACK:
        if (d->ready) d->ready = 0;
        else if (d->team != OB_SIDES_MIDDLE) d->team = OB_SIDES_MIDDLE;
        else return 0;                                 /* the port may leave the screen */
        changed(s);
        return 1;
    }
    return 1;
}

int ob_sides_input(ob_sides *s, int local, int in)
{
    if (!s->host) {
        wr w;
        if (!s->open || s->done) return 0;
        w.n = 0;
        w8(&w, K_INPUT); w8(&w, local); w8(&w, in);
        s->send(s->send_ud, 0, w.b, w.n);
        return 1;
    }
    return apply(s, s->me, local, in);
}

int ob_sides_message(ob_sides *s, int from, const void *data, size_t size)
{
    rd r;
    int kind, local, t, i, n, was_done;
    char name[OB_SIDES_NAME + 1];
    ob_sides_device d[OB_SIDES_MAX_DEVICES];
    r.b = data; r.n = size; r.at = 0; r.bad = 0;
    kind = r8(&r);
    if (s->host) {
        uint32_t before = s->changes;
        if (from <= 0 || !s->online) return 0;       /* only guests send to the host */
        local = r8(&r);
        switch (kind) {
        case K_ADD: rstr(&r, name); if (!r.bad) add(s, from, local, name); break;
        case K_REMOVE: if ((i = ob_sides_find(s, from, local)) >= 0) { drop(s, i); changed(s); } break;
        case K_INPUT: t = r8(&r); if (!r.bad) apply(s, from, local, t); break;
        }
        return s->changes != before;
    }
    if (kind != K_STATE || from != 0) return 0;
    {
        int open = r8(&r), done = r8(&r);
        char team[OB_SIDES_MAX_TEAMS][OB_SIDES_NAME + 1];
        for (t = 0; t < s->g.teams; t++) rstr(&r, team[t]);
        n = r8(&r);
        if (n > OB_SIDES_MAX_DEVICES) return 0;
        for (i = 0; i < n; i++) {
            d[i].owner = (int8_t)r8(&r);
            d[i].local = (int8_t)r8(&r);
            d[i].team = (int8_t)(r8(&r) - 1);
            d[i].ready = (uint8_t)r8(&r);
            rstr(&r, d[i].name);
            if (d[i].team < OB_SIDES_MIDDLE || d[i].team >= s->g.teams) r.bad = 1;
        }
        if (r.bad) return 0;
        was_done = s->done;
        s->open = open;
        s->done = done;
        memcpy(s->team, team, sizeof team);
        memcpy(s->d, d, (size_t)n * sizeof d[0]);
        s->n = n;
        s->changes++;
        if (done && !was_done && s->g.apply) {
            ob_sides_seat seat[OB_SIDES_MAX_SEATS];
            int k = ob_sides_seats(s, seat, OB_SIDES_MAX_SEATS);
            s->g.apply(s->g.ud, seat, k);
        }
    }
    return 1;
}

/* ---- what the game takes ---- */

int ob_sides_seats(const ob_sides *s, ob_sides_seat *seat, int max)
{
    int t, i, k = 0;
    for (t = 0; t < s->g.teams; t++)
        for (i = 0; i < s->n; i++)
            if (s->d[i].team == t && k < max && k < s->g.seats) {
                seat[k].team = t;
                seat[k].owner = s->d[i].owner;
                seat[k].local = s->d[i].local;
                seat[k].name = s->d[i].name;
                k++;
            }
    return k;
}

int ob_sides_seat_of(const ob_sides *s, int owner, int local)
{
    ob_sides_seat seat[OB_SIDES_MAX_SEATS];
    int k, n = ob_sides_seats(s, seat, OB_SIDES_MAX_SEATS);
    for (k = 0; k < n; k++) if (seat[k].owner == owner && seat[k].local == local) return k;
    return -1;
}

void ob_sides_owners(const ob_sides *s, int8_t *owner, int pads, int per_seat)
{
    ob_sides_seat seat[OB_SIDES_MAX_SEATS];
    int k, j, n = ob_sides_seats(s, seat, OB_SIDES_MAX_SEATS);
    if (per_seat < 1) per_seat = 1;
    for (j = 0; j < pads; j++) owner[j] = -1;
    for (k = 0; k < n; k++)
        for (j = 0; j < per_seat; j++)
            if (k * per_seat + j < pads) owner[k * per_seat + j] = (int8_t)seat[k].owner;
}

unsigned ob_sides_teams_of(const ob_sides *s, int machine)
{
    ob_sides_seat seat[OB_SIDES_MAX_SEATS];
    int k, n = ob_sides_seats(s, seat, OB_SIDES_MAX_SEATS);
    unsigned m = 0;
    for (k = 0; k < n; k++) if (seat[k].owner == machine) m |= 1u << seat[k].team;
    return m;
}

uint32_t ob_sides_hotkeys_filter(uint32_t in, const ob_sides_hotkey *key, int n, unsigned teams)
{
    int i;
    for (i = 0; i < n; i++)
        if (key[i].team < 0 || key[i].team >= 32 || !(teams & (1u << key[i].team))) in &= ~key[i].bits;
    return in;
}

/* ---- drawing ---- */

static void fill(uint32_t *px, int w, int h, int pitch, int x, int y, int rw, int rh, uint32_t rgb)
{
    int i, j;
    for (j = 0; j < rh; j++) {
        if (y + j < 0 || y + j >= h) continue;
        for (i = 0; i < rw; i++)
            if (x + i >= 0 && x + i < w) px[(size_t)(y + j) * pitch + x + i] = rgb;
    }
}

/* text cut to fit `chars`, centred in a column `cw` wide at x */
static void text_in(uint32_t *px, int w, int h, int pitch, int x, int cw, int y, const char *s, uint32_t rgb)
{
    char b[64];
    int chars = cw / 6, len;
    if (chars > 63) chars = 63;
    if (chars < 1) return;
    len = (int)strlen(s);
    if (len > chars) len = chars;
    memcpy(b, s, (size_t)len);
    b[len] = 0;
    mdrt_menu_text(px, w, h, pitch, x + (cw - len * 6) / 2, y, b, rgb);
}

void ob_sides_draw(const ob_sides *s, uint32_t *px, int w, int h, int pitch)
{
    int cols = s->g.teams + 1, bw, bh, x0, y0, cw, i, t;
    const char *foot;
    if (!s->open || w < 120 || h < 80) return;
    bw = w - 16 > 360 ? 360 : w - 16;
    bh = 40 + (s->n ? s->n : 1) * 14;
    if (bh > h - 8) bh = h - 8;
    x0 = (w - bw) / 2;
    y0 = (h - bh) / 2;
    cw = (bw - 8) / cols;
    fill(px, w, h, pitch, x0, y0, bw, bh, 0xF0101830u);
    text_in(px, w, h, pitch, x0, bw, y0 + 4, "Select sides", 0xFFFFFFFFu);
    /* the columns: team 0, the middle, then the rest (two teams: away | middle | home) */
    for (t = 0; t < cols; t++) {
        int team = t == 0 ? 0 : t == 1 ? OB_SIDES_MIDDLE : t - 1;
        const char *head = team == OB_SIDES_MIDDLE ? "" : s->team[team];
        if (s->g.teams == 1) head = t == 1 ? s->team[0] : "";
        text_in(px, w, h, pitch, x0 + 4 + t * cw, cw, y0 + 16, head, 0xFFE0E060u);
    }
    for (i = 0; i < s->n && y0 + 30 + i * 14 + 12 <= y0 + bh - 12; i++) {
        const ob_sides_device *d = &s->d[i];
        int col = d->team == OB_SIDES_MIDDLE ? 1 : d->team == 0 ? 0 : d->team + 1;
        int x, y = y0 + 30 + i * 14;
        if (s->g.teams == 1) col = d->team == OB_SIDES_MIDDLE ? 0 : 1;
        x = x0 + 4 + col * cw;
        fill(px, w, h, pitch, x + 2, y, cw - 4, 12, d->ready ? 0xFF508050u : 0xFF303860u);
        text_in(px, w, h, pitch, x + 2, cw - 4, y + 2, d->name, 0xFFFFFFFFu);
    }
    if (!ob_sides_all_ready(s)) foot = "Left/Right: pick a side   OK: ready";
    else if (s->host) foot = "Everyone is ready: OK starts";
    else foot = "Everyone is ready: the host starts";
    text_in(px, w, h, pitch, x0, bw, y0 + bh - 12, foot, 0xFFC0C0C0u);
}
