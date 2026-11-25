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
#include <stdio.h>
#include <string.h>
#include "sides_icons.h"
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

/* The screen is laid out on the console's 320 x 224 and drawn at the largest whole-number scale that fits the picture,
 * centred, with the NHL 94 Rewind look: a blue field, a CONTROLLER SETUP plate, a plate for each team, each device as its
 * icon in the team's column (the middle when it has none) with a star that fills when it is ready. A game gives its own art
 * through ob_sides_skin (the field, its font, the team colours); without them it is drawn with Overbar's. */
#define LW 320
#define LH 224

typedef struct cv { uint32_t *px; int w, h, pitch, s, ox, oy; const ob_sides_skin *k; } cv;

static void cfill(cv *c, int x, int y, int w, int h, uint32_t rgb)
{
    int i, j, x0 = c->ox + x * c->s, y0 = c->oy + y * c->s, x1 = x0 + w * c->s, y1 = y0 + h * c->s;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) c->px[(size_t)j * c->pitch + i] = 0xFF000000u | rgb;
}

/* the width of s in game pixels at mul times the font's size */
static int cwidth(const cv *c, const char *s, int mul)
{
    if (c->k->text_width) return c->k->text_width(c->k->ud, s, mul > 1);
    return (int)strlen(s) * 6 * mul;
}

/* s with its top left at game pixel x, y, mul times the size, a shadow under it */
static void ctext(cv *c, int x, int y, const char *s, uint32_t rgb, int mul)
{
    char b[80];
    uint32_t t[80 * 6 * 3 * 10 * 3];
    int n, len = (int)strlen(s), i, j, tw, th = 9;
    if (len > 79) len = 79;
    memcpy(b, s, (size_t)len);
    b[len] = 0;
    if (c->k->text) {
        c->k->text(c->k->ud, c->px + (size_t)(c->oy + y * c->s) * c->pitch + (c->ox + x * c->s), c->w - (c->ox + x * c->s), c->h - (c->oy + y * c->s), c->pitch,
                   0, 0, b, 0xFF000000u | rgb, mul > 1, c->s);
        return;
    }
    tw = len * 6 + 1;
    if (tw * th > (int)(sizeof t / sizeof t[0])) return;
    memset(t, 0, (size_t)tw * th * sizeof t[0]);
    mdrt_menu_text(t, tw, th, tw, 0, 0, b, 0xFF000000u | rgb);
    n = c->s * mul;
    for (j = 0; j < th * n; j++) {
        int yy = c->oy + y * c->s + j;
        if (yy < 0 || yy >= c->h) continue;
        for (i = 0; i < tw * n; i++) {
            int xx = c->ox + x * c->s + i;
            uint32_t v = t[(j / n) * tw + i / n];
            if (v && xx >= 0 && xx < c->w) c->px[(size_t)yy * c->pitch + xx] = v | 0xFF000000u;
        }
    }
}

static void ctext_c(cv *c, int x, int w, int y, const char *s, uint32_t rgb, int mul)
{
    ctext(c, x + (w - cwidth(c, s, mul)) / 2, y, s, rgb, mul);
}

static uint32_t icon_rgb(char l)
{
    switch (l) {
    case '0': return 0x000000; case '1': return 0x181818; case '2': return 0x2C2C2C; case '3': return 0x484848;
    case '4': return 0x686868; case 'W': return 0xE0E0E0; case 'B': return 0x3880F0; case 'Y': return 0xF0C820;
    case 'R': return 0xE0506A; case 'G': return 0x50B030; default: return 0;
    }
}

static void cicon(cv *c, int kind, int x, int y)
{
    const ob_icon *ic = &ob_icons[kind];
    int j, i;
    for (j = 0; j < ic->h; j++)
        for (i = 0; i < ic->w; i++)
            if (ic->rows[j][i] != '.') cfill(c, x + i, y + j, 1, 1, icon_rgb(ic->rows[j][i]));
}

/* the star (15 game pixels): an outline, or filled when the device is ready */
static void cstar(cv *c, int x, int y, int on)
{
    static const char *const r[15] = {
        ".......1.......", ".......1.......", "......111......", "......111......", ".....11111.....", "111111111111111", ".1111111111111.",
        "..11111111111..", "...111111111...", "...111111111...", "..1111...1111..", "..111.....111..", ".111.......111.", ".11.........11.", "11...........11" };
    int j, i;
    for (j = 0; j < 15; j++)
        for (i = 0; i < 15; i++) {
            int edge;
            if (r[j][i] != '1') continue;
            edge = j == 0 || i == 0 || j == 14 || i == 14 || r[j - 1][i] != '1' || r[j + 1][i] != '1' || r[j][i - 1] != '1' || r[j][i + 1] != '1';
            if (on) cfill(c, x + i, y + j, 1, 1, edge ? 0xC08000 : 0xF8D830);   /* filled: ready */
            else if (edge) cfill(c, x + i, y + j, 1, 1, 0x28A8FF);              /* the outline only */
        }
}

static int kind_of(const char *name)
{
    char b[16];
    size_t i;
    for (i = 0; i < sizeof b - 1 && name[i]; i++) b[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    b[i] = 0;
    if (!strncmp(b, "keyboard", 8)) return OB_ICON_KEYBOARD;
    if (!strncmp(b, "mouse", 5)) return OB_ICON_MOUSE;
    return OB_ICON_PAD;
}

static void cplate(cv *c, int x, int y, int w, int h, uint32_t fill, uint32_t band, const char *text)
{
    cfill(c, x, y, w, h, 0x8C8CA8);
    cfill(c, x + 2, y + 2, w - 4, h - 4, fill);
    if (band) cfill(c, x + 2, y + h - 7, w - 4, 5, band);
    if (text) ctext_c(c, x, w, y + (h - 8) / 2 - 1, text, 0xFFFFFF, 1);
}

void ob_sides_draw(const ob_sides *s, uint32_t *px, int w, int h, int pitch)
{
    static const ob_sides_skin none;
    cv c;
    const ob_sides_skin *k = &s->g.skin;
    int teams = s->g.teams, i, t, cols = 3, cx[3], per[3] = { 0, 0, 0 }, at[3] = { 0, 0, 0 }, rowh, top = 90, lbl;
    const char *foot;
    char line[64];
    uint32_t col2[2] = { k->team_rgb[0] ? k->team_rgb[0] : 0xE03030u, k->team_rgb[1] ? k->team_rgb[1] : 0x3060E0u };
    if (!s->open || w < 64 || h < 48) return;
    if (!k) k = &none;
    c.px = px; c.w = w; c.h = h; c.pitch = pitch; c.k = k;
    c.s = w / LW < h / LH ? w / LW : h / LH;
    if (c.s < 1) c.s = 1;
    c.ox = (w - LW * c.s) / 2;
    c.oy = (h - LH * c.s) / 2;
    /* the field */
    if (k->background) k->background(k->ud, px, w, h, pitch);
    else {
        int x, y;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) px[(size_t)y * pitch + x] = ((x / c.s) * 2 + (y / c.s)) % 56 < 9 ? 0xFF0000A2u : 0xFF0000B6u;
    }
    cplate(&c, 14, 8, 292, 46, 0x000000, 0, NULL);
    ctext_c(&c, 14, 292, 22, "CONTROLLER SETUP", 0xFFFFFF, 2);
    cx[0] = 77; cx[1] = 160; cx[2] = 243;
    for (t = 0; t < 2 && t < teams; t++) {
        int x = t == 0 ? 14 : 180;
        cplate(&c, x, 60, 126, 20, 0x00005C, col2[t], s->team[t]);
    }
    if (teams == 1) { (void)0; }
    /* each column's devices */
    for (i = 0; i < s->n; i++) {
        const ob_sides_device *d = &s->d[i];
        int col = teams == 1 ? (d->team == OB_SIDES_MIDDLE ? 0 : 1) : d->team == OB_SIDES_MIDDLE ? 1 : d->team == 0 ? 0 : 2;
        per[col]++;
    }
    for (i = 0; i < s->n; i++) {
        const ob_sides_device *d = &s->d[i];
        int col = teams == 1 ? (d->team == OB_SIDES_MIDDLE ? 0 : 1) : d->team == OB_SIDES_MIDDLE ? 1 : d->team == 0 ? 0 : 2;
        int kind = kind_of(d->name), x, y, ih = ob_icons[kind].h, iw = ob_icons[kind].w;
        rowh = per[col] > 3 ? 120 / per[col] : 40;
        lbl = rowh >= 36;
        y = top + at[col]++ * rowh;
        x = cx[col] - iw / 2 + 8;
        cstar(&c, x - 20, y + (ih - 15) / 2, d->ready);
        cicon(&c, kind, x, y);
        if (lbl) {
            snprintf(line, sizeof line, "%.20s", d->name);
            ctext_c(&c, cx[col] - 60 + 8, 120, y + ih + 2, line, d->ready ? 0xA0FFA0 : 0xFFFFFF, 1);
        }
    }
    (void)cols;
    if (!s->n) ctext_c(&c, 0, LW, 130, "No controller found", 0xFFFFFF, 1);
    if (!seated(s)) foot = "Left / Right: pick a side";
    else if (!ob_sides_all_ready(s)) foot = "Left / Right: pick a side   OK: ready";
    else if (s->host) foot = "Press OK to begin";
    else foot = "Waiting for the host to begin";
    ctext_c(&c, 0, LW, 206, foot, 0xFFE020, 1);
}
