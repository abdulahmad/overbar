/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The online season (overbar/season.h). Messages, the first byte says which:
 *   1 HELLO   (a guest) its name, the league's name and id (each length u8, bytes), teams (count u8, each u8),
 *             games played away (count u8, each key u32)
 *   2 WELCOME (the master) ok u8, why (length u8, bytes), the teams taken (count u8, each u8)
 *   3 SYNCED  (a guest) every copy imported
 *   4 FAIL    (the master) why (length u8, bytes)
 * Files: "x<team>" a guest's export, "m<team>" the master's copy back, "g<team>" a first copy, "L" a mirror.
 * Folders under base: L<hash of the league>/ (season.txt, the ledger; x/t<team>, a guest's exports;
 * m/t<team>, copies taken; in/m<machine>-t<team>, exports come to the master; out/m<machine>-t<team>, copies
 * for them), new/t<team> (a first copy), and mirror/. */
#include "overbar/season.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsdir.h"

enum { HELLO = 1, WELCOME, SYNCED, FAIL };

#define DIRLEN (OB_SEASON_PATH + 48)

/* ---- messages ---- */

typedef struct wr { uint8_t b[1024]; size_t n; } wr;
static void put8(wr *w, unsigned v) { if (w->n < sizeof w->b) w->b[w->n++] = (uint8_t)v; }
static void put32(wr *w, uint32_t v) { put8(w, v & 0xFF); put8(w, v >> 8 & 0xFF); put8(w, v >> 16 & 0xFF); put8(w, v >> 24); }
static void putstr(wr *w, const char *s)
{
    size_t n = strlen(s), i;
    if (n > 63) n = 63;
    put8(w, (unsigned)n);
    for (i = 0; i < n; i++) put8(w, (uint8_t)s[i]);
}
typedef struct rd { const uint8_t *p; size_t n; int bad; } rd;
static unsigned get8(rd *r) { if (!r->n) { r->bad = 1; return 0; } r->n--; return *r->p++; }
static uint32_t get32(rd *r) { uint32_t v = get8(r); v |= (uint32_t)get8(r) << 8; v |= (uint32_t)get8(r) << 16; return v | (uint32_t)get8(r) << 24; }
static void getstr(rd *r, char *s, size_t cap)
{
    size_t n = get8(r), i;
    for (i = 0; i < n; i++) { unsigned c = get8(r); if (i + 1 < cap) s[i] = (char)((c >= 32 && c < 127) ? c : '?'); }
    s[n < cap ? n : cap - 1] = 0;
}

static void emit(ob_season *s, int to, const wr *w) { if (s->send) s->send(s->send_ud, to, w->b, w->n); }

static void fail_to(ob_season *s, int to, const char *why)
{
    wr w;
    w.n = 0;
    put8(&w, FAIL);
    putstr(&w, why);
    emit(s, to, &w);
}

/* ---- folders and the ledger ---- */

static uint32_t league_key(const ob_season_league *l)
{
    char b[2 * OB_SEASON_TEXT + 4];
    snprintf(b, sizeof b, "%s\n%s", l->name, l->id);
    return (uint32_t)ob_fs_hash(b, strlen(b));
}

static void league_dir(const ob_season *s, char *out, size_t size)
{
    snprintf(out, size, "%s/L%08x", s->base, (unsigned)league_key(&s->league));
}

/* base/L<league>/<sub>/<leaf> made (leaf: NULL just the sub folder; sub NULL: the league folder) */
static int make(const ob_season *s, const char *sub, const char *leaf, char *out, size_t size)
{
    char d[DIRLEN], sd[DIRLEN + 8];
    ob_fs_mkdir(s->base);
    league_dir(s, d, sizeof d);
    if (ob_fs_mkdir(d)) return -1;
    if (!sub) { snprintf(out, size, "%s", d); return 0; }
    ob_fs_join(sd, sizeof sd, d, sub);
    if (ob_fs_mkdir(sd)) return -1;
    if (!leaf) { size_t n = strlen(sd); if (n >= size) n = size - 1; memcpy(out, sd, n); out[n] = 0; return 0; }
    ob_fs_join(out, size, sd, leaf);
    return ob_fs_mkdir(out);
}

static void save_ledger(ob_season *s)
{
    char d[DIRLEN], path[DIRLEN + 16];
    char *t;
    size_t cap = 256 + (size_t)(OB_SEASON_TEAMS * 48 + (OB_SEASON_MERGED + OB_SEASON_PLAYED) * 20), n = 0;
    int i;
    if (make(s, NULL, NULL, d, sizeof d) || !(t = (char *)malloc(cap))) return;
    n += (size_t)snprintf(t + n, cap - n, "OBSEASON 1\nleague %s\nid %s\n", s->league.name, s->league.id);
    for (i = 0; i < OB_SEASON_TEAMS; i++) if (s->owner[i][0]) n += (size_t)snprintf(t + n, cap - n, "owner %d %s\n", i, s->owner[i]);
    for (i = 0; i < s->nmerged; i++) n += (size_t)snprintf(t + n, cap - n, "merged %lu\n", (unsigned long)s->merged[i]);
    for (i = 0; i < s->nplayed; i++) n += (size_t)snprintf(t + n, cap - n, "played %lu\n", (unsigned long)s->played[i]);
    snprintf(path, sizeof path, "%s/season.txt", d);
    if (ob_fs_write(path, t, n)) snprintf(s->why, sizeof s->why, "could not write the season's ledger");
    free(t);
}

/* the league this machine plays now and its ledger: 0, -1 none */
static int load(ob_season *s)
{
    ob_season_league l;
    char d[DIRLEN], path[DIRLEN + 16], *line, *next;
    uint8_t *data;
    size_t size;
    memset(&l, 0, sizeof l);
    if (!s->ops.league || s->ops.league(s->ops.ud, &l) || !l.name[0]) return -1;
    if (l.nteam < 0) l.nteam = 0;
    if (l.nteam > OB_SEASON_TEAMS) l.nteam = OB_SEASON_TEAMS;
    if (!strcmp(l.name, s->league.name) && !strcmp(l.id, s->league.id) && s->league.name[0]) { s->league = l; return 0; }
    s->league = l;
    memset(s->owner, 0, sizeof s->owner);
    s->nmerged = s->nplayed = 0;
    league_dir(s, d, sizeof d);
    snprintf(path, sizeof path, "%s/season.txt", d);
    if (ob_fs_read(path, &data, &size)) return 0;
    data = (uint8_t *)realloc(data, size + 1);
    if (!data) return 0;
    data[size] = 0;
    for (line = (char *)data; line && *line; line = next) {
        unsigned long v;
        int team;
        char name[OB_NET_NAME + 1];         /* (%24: OB_NET_NAME) */
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (sscanf(line, "owner %d %24[^\n]", &team, name) == 2 && team >= 0 && team < OB_SEASON_TEAMS)
            snprintf(s->owner[team], sizeof s->owner[team], "%s", name);
        else if (sscanf(line, "merged %lu", &v) == 1 && s->nmerged < OB_SEASON_MERGED) s->merged[s->nmerged++] = (uint32_t)v;
        else if (sscanf(line, "played %lu", &v) == 1 && s->nplayed < OB_SEASON_PLAYED) s->played[s->nplayed++] = (uint32_t)v;
    }
    free(data);
    return 0;
}

static int was_merged(const ob_season *s, uint32_t key)
{
    int i;
    for (i = 0; i < s->nmerged; i++) if (s->merged[i] == key) return 1;
    return 0;
}

static void add_merged(ob_season *s, uint32_t key)
{
    if (was_merged(s, key)) return;
    if (s->nmerged == OB_SEASON_MERGED) { memmove(s->merged, s->merged + 1, (OB_SEASON_MERGED - 1) * sizeof s->merged[0]); s->nmerged--; }
    s->merged[s->nmerged++] = key;
}

/* ---- set-up ---- */

static int where(void *ud, int from, const char *tag, char *dir, int size);
static void done(void *ud, int out, int peer, const char *tag, int ok, const char *why);

void ob_season_init(ob_season *s, const ob_season_ops *ops, const char *base, const char *me, ob_files *files,
                    void (*send)(void *, int, const void *, size_t), void *ud)
{
    memset(s, 0, sizeof *s);
    if (ops) s->ops = *ops;
    snprintf(s->base, sizeof s->base, "%s", base ? base : "season");
    snprintf(s->me, sizeof s->me, "%s", me ? me : "");
    s->files = files;
    s->send = send;
    s->send_ud = ud;
    s->mirror_from = -1;
    ob_fs_mkdir(s->base);
    if (files) ob_files_hooks(files, where, done, s);
}

void ob_season_free(ob_season *s) { (void)s; }

/* ---- a guest ---- */

int ob_season_sync(ob_season *s, int host)
{
    wr w;
    int i;
    if (load(s) || s->league.master) return -1;
    s->host = host;
    s->state = OB_SEASON_ASKED;
    s->nteam = 0;
    s->taken = 0;
    s->why[0] = 0;
    s->sent_played = s->nplayed;
    w.n = 0;
    put8(&w, HELLO);
    putstr(&w, s->me);
    putstr(&w, s->league.name);
    putstr(&w, s->league.id);
    put8(&w, (unsigned)s->league.nteam);
    for (i = 0; i < s->league.nteam; i++) put8(&w, (unsigned)s->league.team[i]);
    put8(&w, (unsigned)s->nplayed);
    for (i = 0; i < s->nplayed; i++) put32(&w, s->played[i]);
    emit(s, host, &w);
    return 0;
}

static void guest_failed(ob_season *s, const char *why)
{
    s->state = OB_SEASON_FAILED;
    snprintf(s->why, sizeof s->why, "%s", why);
}

static void got_welcome(ob_season *s, int from, rd *r)
{
    int ok = (int)get8(r), i, n;
    char why[80], tag[16], dir[DIRLEN];
    getstr(r, why, sizeof why);
    if (from != s->host || s->state != OB_SEASON_ASKED) return;
    if (!ok) { guest_failed(s, why); return; }
    n = (int)get8(r);
    s->nteam = 0;
    for (i = 0; i < n && i < OB_SEASON_TEAMS; i++) { int t = (int)get8(r); if (t < OB_SEASON_TEAMS) s->team[s->nteam++] = t; }
    if (r->bad) { guest_failed(s, "a damaged answer from the host"); return; }
    s->state = OB_SEASON_SENDING;
    if (!s->nteam) { wr w; w.n = 0; put8(&w, SYNCED); emit(s, from, &w); s->state = OB_SEASON_SYNCED; return; }
    for (i = 0; i < s->nteam; i++) {
        char leaf[16];
        snprintf(leaf, sizeof leaf, "t%d", s->team[i]);
        if (make(s, "x", leaf, dir, sizeof dir)) { guest_failed(s, "could not make a folder for the export"); return; }
        ob_fs_clear(dir);
        ob_fs_mkdir(dir);
        if (!s->ops.export_team || s->ops.export_team(s->ops.ud, s->team[i], dir)) { guest_failed(s, "the game's export failed"); return; }
        snprintf(tag, sizeof tag, "x%d", s->team[i]);
        if (!s->files || ob_files_send(s->files, from, tag, dir, OB_FILES_MIRROR)) { guest_failed(s, "could not send the export"); return; }
    }
}

static int team_index(const int *team, int n, int t)
{
    int i;
    for (i = 0; i < n; i++) if (team[i] == t) return i;
    return -1;
}

static void guest_took(ob_season *s, int team, const char *dir)
{
    int i = team_index(s->team, s->nteam, team), all = 1;
    wr w;
    if (i < 0) return;
    if (!s->ops.import_team || s->ops.import_team(s->ops.ud, team, dir)) { guest_failed(s, "the game's import failed"); return; }
    s->taken |= 1u << i;
    s->state = OB_SEASON_TAKING;
    for (i = 0; i < s->nteam; i++) if (!(s->taken >> i & 1)) all = 0;
    if (!all) return;
    /* the games that went with the sync are merged now */
    memmove(s->played, s->played + s->sent_played, (size_t)(s->nplayed - s->sent_played) * sizeof s->played[0]);
    s->nplayed -= s->sent_played;
    s->sent_played = 0;
    save_ledger(s);
    w.n = 0;
    put8(&w, SYNCED);
    emit(s, s->host, &w);
    s->state = OB_SEASON_SYNCED;
}

/* ---- the master ---- */

static void got_hello(ob_season *s, int from, rd *r)
{
    ob_season_peer *p;
    char name[OB_NET_NAME + 1], lname[OB_SEASON_TEXT + 1], lid[OB_SEASON_TEXT + 1], why[96];
    int team[OB_SEASON_TEAMS], n, i, nt = 0;
    wr w;
    if (from < 0 || from >= OB_NET_MAX_MACHINES) return;
    p = &s->peer[from];
    getstr(r, name, sizeof name);
    getstr(r, lname, sizeof lname);
    getstr(r, lid, sizeof lid);
    n = (int)get8(r);
    for (i = 0; i < n; i++) { int t = (int)get8(r); if (i < OB_SEASON_TEAMS) team[i] = t; }
    if (n > OB_SEASON_TEAMS) n = OB_SEASON_TEAMS;
    memset(p, 0, sizeof *p);
    p->nplayed = (int)get8(r);
    if (p->nplayed > OB_SEASON_PLAYED) p->nplayed = OB_SEASON_PLAYED;
    for (i = 0; i < p->nplayed; i++) p->played[i] = get32(r);
    if (r->bad) { memset(p, 0, sizeof *p); return; }
    w.n = 0;
    put8(&w, WELCOME);
    if (load(s) || !s->league.master) { put8(&w, 0); putstr(&w, "the host has no master league"); emit(s, from, &w); memset(p, 0, sizeof *p); return; }
    if (strcmp(lname, s->league.name) || strcmp(lid, s->league.id)) {
        snprintf(why, sizeof why, "the host's league is %s, not this one", s->league.name);
        put8(&w, 0); putstr(&w, why); emit(s, from, &w); memset(p, 0, sizeof *p); return;
    }
    for (i = 0; i < n; i++) {                       /* the first player to bring a team owns it */
        int t = team[i];
        if (t < 0 || t >= OB_SEASON_TEAMS || team_index(p->team, nt, t) >= 0) continue;
        if (s->owner[t][0] && ob_fs_stricmp(s->owner[t], name)) continue;
        if (!s->owner[t][0]) snprintf(s->owner[t], sizeof s->owner[t], "%s", name);
        p->team[nt++] = t;
    }
    p->nteam = nt;
    p->state = nt ? 1 : 0;
    snprintf(p->name, sizeof p->name, "%s", name);
    save_ledger(s);
    put8(&w, 1);
    putstr(&w, "");
    put8(&w, (unsigned)nt);
    for (i = 0; i < nt; i++) put8(&w, (unsigned)p->team[i]);
    emit(s, from, &w);
}

static void peer_dir(const ob_season *s, const char *sub, int m, int team, char *out, size_t size)
{
    char leaf[24];
    snprintf(leaf, sizeof leaf, "m%d-t%d", m, team);
    make(s, sub, leaf, out, size);
}

/* every export of guest m came: merge them all, then a copy back for each */
static void master_merge(ob_season *s, int m)
{
    ob_season_peer *p = &s->peer[m];
    uint32_t fresh[OB_SEASON_PLAYED];
    char dir[DIRLEN], tag[16];
    int nfresh = 0, i;
    for (i = 0; i < p->nplayed; i++) if (!was_merged(s, p->played[i])) fresh[nfresh++] = p->played[i];
    for (i = 0; i < p->nteam; i++) {
        peer_dir(s, "in", m, p->team[i], dir, sizeof dir);
        if (!s->ops.merge_team || s->ops.merge_team(s->ops.ud, p->team[i], dir, fresh, nfresh)) {
            snprintf(s->why, sizeof s->why, "the merge of %s's team failed", p->name);
            fail_to(s, m, "the host's merge failed");
            p->state = 0;
            return;
        }
    }
    for (i = 0; i < nfresh; i++) add_merged(s, fresh[i]);
    save_ledger(s);
    p->state = 2;
    for (i = 0; i < p->nteam; i++) {
        peer_dir(s, "out", m, p->team[i], dir, sizeof dir);
        ob_fs_clear(dir);
        ob_fs_mkdir(dir);
        snprintf(tag, sizeof tag, "m%d", p->team[i]);
        if (!s->ops.copy_team || s->ops.copy_team(s->ops.ud, p->team[i], dir) || !s->files ||
            ob_files_send(s->files, m, tag, dir, OB_FILES_MIRROR)) {
            fail_to(s, m, "the host could not make a copy");
            p->state = 0;
            return;
        }
    }
}

/* ---- files ---- */

static int where(void *ud, int from, const char *tag, char *dir, int size)
{
    ob_season *s = (ob_season *)ud;
    char d[DIRLEN];
    int team;
    if (!strcmp(tag, "L")) {                        /* a mirror */
        snprintf(dir, (size_t)size, "%s/mirror", s->base);
        return 1;
    }
    if (from < 0 || from >= OB_NET_MAX_MACHINES || sscanf(tag + 1, "%d", &team) != 1 || team < 0 || team >= OB_SEASON_TEAMS) return 0;
    if (tag[0] == 'g') {                            /* a first copy: the league may not be here yet */
        snprintf(d, sizeof d, "%s/new", s->base);
        ob_fs_mkdir(d);
        snprintf(d, sizeof d, "%s/new/t%d", s->base, team);
        if ((int)strlen(d) >= size) return 0;
        memcpy(dir, d, strlen(d) + 1);
        return 1;
    }
    if (tag[0] == 'x' && s->peer[from].state == 1 && team_index(s->peer[from].team, s->peer[from].nteam, team) >= 0) {
        peer_dir(s, "in", from, team, d, sizeof d);
    } else if (tag[0] == 'm' && from == s->host && (s->state == OB_SEASON_SENDING || s->state == OB_SEASON_TAKING) &&
               team_index(s->team, s->nteam, team) >= 0) {
        char leaf[16];
        snprintf(leaf, sizeof leaf, "t%d", team);
        if (make(s, "m", leaf, d, sizeof d)) return 0;
    } else {
        return 0;
    }
    if ((int)strlen(d) >= size) return 0;
    memcpy(dir, d, strlen(d) + 1);
    return 1;
}

static void done(void *ud, int out, int peer, const char *tag, int ok, const char *why)
{
    ob_season *s = (ob_season *)ud;
    char dir[DIRLEN];
    int team = -1, i;
    if (tag[0] && tag[1]) sscanf(tag + 1, "%d", &team);
    if (!strcmp(tag, "L")) {
        if (out && peer >= 0 && peer < OB_NET_MAX_MACHINES) { if (ok) s->mirror_ok |= 1u << peer; else s->mirror_bad |= 1u << peer; }
        if (!out && ok) s->mirror_from = peer;
        return;
    }
    if (tag[0] == 'g') {
        if (out || !ok || team < 0 || team >= OB_SEASON_TEAMS) return;
        snprintf(dir, sizeof dir, "%s/new/t%d", s->base, team);
        if (!s->ops.import_team || s->ops.import_team(s->ops.ud, team, dir)) { snprintf(s->why, sizeof s->why, "the game's import of a first copy failed"); return; }
        s->given |= 1u << team;
        ob_fs_clear(dir);
        return;
    }
    if (tag[0] == 'x' && out && !ok && s->state == OB_SEASON_SENDING) { char b[160]; snprintf(b, sizeof b, "the export did not reach the host (%s)", why); guest_failed(s, b); return; }
    if (tag[0] == 'm' && !out && peer == s->host && (s->state == OB_SEASON_SENDING || s->state == OB_SEASON_TAKING)) {
        char leaf[16];
        if (!ok) { guest_failed(s, "the host's copy did not come"); return; }
        snprintf(leaf, sizeof leaf, "t%d", team);
        if (make(s, "m", leaf, dir, sizeof dir)) { guest_failed(s, "no folder for the copy"); return; }
        guest_took(s, team, dir);
        return;
    }
    if (tag[0] == 'x' && !out && peer >= 0 && peer < OB_NET_MAX_MACHINES && s->peer[peer].state == 1) {
        ob_season_peer *p = &s->peer[peer];
        int all = 1;
        if (!ok) { p->state = 0; return; }
        i = team_index(p->team, p->nteam, team);
        if (i >= 0) p->came |= 1u << i;
        for (i = 0; i < p->nteam; i++) if (!(p->came >> i & 1)) all = 0;
        if (all) master_merge(s, peer);
        return;
    }
    if (tag[0] == 'm' && out && peer >= 0 && peer < OB_NET_MAX_MACHINES && s->peer[peer].state == 2) {
        ob_season_peer *p = &s->peer[peer];
        i = team_index(p->team, p->nteam, team);
        if (!ok) { p->state = 0; return; }
        if (i >= 0) p->went |= 1u << i;
    }
}

void ob_season_message(ob_season *s, int from, const void *data, size_t size)
{
    rd r;
    r.p = (const uint8_t *)data;
    r.n = size;
    r.bad = 0;
    switch (get8(&r)) {
    case HELLO: got_hello(s, from, &r); break;
    case WELCOME: got_welcome(s, from, &r); break;
    case SYNCED: if (from >= 0 && from < OB_NET_MAX_MACHINES) s->peer[from].state = 0; break;
    case FAIL: {
        char why[80];
        getstr(&r, why, sizeof why);
        if (from == s->host && s->state != OB_SEASON_IDLE && s->state != OB_SEASON_SYNCED) guest_failed(s, why);
        break;
    }
    default: break;
    }
}

void ob_season_machine_gone(ob_season *s, int machine)
{
    if (machine < 0 || machine >= OB_NET_MAX_MACHINES) return;
    memset(&s->peer[machine], 0, sizeof s->peer[machine]);
    if (machine == s->host && s->state != OB_SEASON_IDLE && s->state != OB_SEASON_SYNCED && s->state != OB_SEASON_FAILED)
        guest_failed(s, "the host left before the league was synced");
    if (machine == s->mirror_from) s->mirror_from = -1;
    s->mirror_to &= ~(1u << machine);
}

int ob_season_busy(const ob_season *s)
{
    int k, n = s->state == OB_SEASON_ASKED || s->state == OB_SEASON_SENDING || s->state == OB_SEASON_TAKING;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) n += s->peer[k].state != 0;
    return n;
}

void ob_season_status(const ob_season *s, char *line, int size)
{
    int k;
    line[0] = 0;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++)
        if (s->peer[k].state) { snprintf(line, (size_t)size, "Syncing league with %s", s->peer[k].name); return; }
    switch (s->state) {
    case OB_SEASON_ASKED: case OB_SEASON_SENDING: snprintf(line, (size_t)size, "Syncing league: sending your teams"); break;
    case OB_SEASON_TAKING: snprintf(line, (size_t)size, "Syncing league: taking the host's copy"); break;
    case OB_SEASON_SYNCED: snprintf(line, (size_t)size, "League synced"); break;
    case OB_SEASON_FAILED: snprintf(line, (size_t)size, "League not synced: %s", s->why); break;
    default: break;
    }
}

int ob_season_give(ob_season *s, int machine, const char *name, int team)
{
    char dir[DIRLEN], leaf[24], tag[16];
    if (team < 0 || team >= OB_SEASON_TEAMS || !name || !name[0] || load(s) || !s->league.master) return -1;
    if (s->owner[team][0] && ob_fs_stricmp(s->owner[team], name)) { snprintf(s->why, sizeof s->why, "that team is %s's", s->owner[team]); return -1; }
    snprintf(s->owner[team], sizeof s->owner[team], "%s", name);
    save_ledger(s);
    snprintf(leaf, sizeof leaf, "g-t%d", team);
    if (make(s, "out", leaf, dir, sizeof dir)) return -1;
    ob_fs_clear(dir);
    ob_fs_mkdir(dir);
    snprintf(tag, sizeof tag, "g%d", team);
    if (!s->ops.copy_team || s->ops.copy_team(s->ops.ud, team, dir) || !s->files || ob_files_send(s->files, machine, tag, dir, OB_FILES_MIRROR))
        return -1;
    return 0;
}

/* ---- matches away from the master ---- */

void ob_season_played(ob_season *s, uint32_t key)
{
    int i;
    if (load(s) || s->league.master) return;
    for (i = 0; i < s->nplayed; i++) if (s->played[i] == key) return;
    if (s->nplayed == OB_SEASON_PLAYED) { snprintf(s->why, sizeof s->why, "too many games played away from the master"); return; }
    s->played[s->nplayed++] = key;
    save_ledger(s);
}

int ob_season_mirror_send(ob_season *s, unsigned machines)
{
    char dir[OB_SEASON_PATH];
    int k;
    if (!s->files || !s->ops.league_dir || s->ops.league_dir(s->ops.ud, dir, sizeof dir)) return -1;
    s->mirror_to = s->mirror_ok = s->mirror_bad = 0;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) {
        if (!(machines >> k & 1)) continue;
        if (ob_files_send(s->files, k, "L", dir, OB_FILES_MIRROR)) return -1;
        s->mirror_to |= 1u << k;
    }
    return 0;
}

int ob_season_mirror_sent(const ob_season *s)
{
    if (s->mirror_bad & s->mirror_to) return -1;
    return (s->mirror_ok & s->mirror_to) == s->mirror_to;
}

int ob_season_mirror(const ob_season *s, char *dir, int size)
{
    if (s->mirror_from < 0) return 0;
    snprintf(dir, (size_t)size, "%s/mirror", s->base);
    return 1;
}

void ob_season_mirror_drop(ob_season *s)
{
    char dir[DIRLEN];
    snprintf(dir, sizeof dir, "%s/mirror", s->base);
    ob_fs_clear(dir);
    s->mirror_from = -1;
}
