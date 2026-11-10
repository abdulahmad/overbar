/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The online season (overbar/season.h). Messages, the first byte says which:
 *   1 HELLO   (a guest) its name, the league's name and id (each length u8, bytes), teams (count u8, each u8),
 *             games played away (count u8, each key u32)
 *   2 WELCOME (the master) ok u8, why (length u8, bytes), the teams taken (count u8, each u8)
 *   3 SYNCED  (a guest) every copy imported
 *   4 FAIL    (the master) why (length u8, bytes)
 *   5 CLAIM   (a guest) its name, the team u8
 *   6 REFUSED (the master) the team u8, why
 *   7 TEAMS   (a guest) nothing: who has which team?
 *   8 ROSTER  (the master) the league's name, count u8, each: team u8, kind u8, the player's name
 * A team's file (ob_season_pack and the others): text lines "OBTEAM 1", "league <name>", "id <id>", "team <n>",
 * "player <name>", "kind export|copy|first", "played <key>" / "merged <key>", then "file <name> <size>" and the
 * bytes and a newline, for each file; "end".
 * Files: "x<team>" a guest's export, "m<team>" the master's copy back, "g<team>" a first copy, "L" a mirror (a player's copy).
 * Folders under base: L<hash of the league>/ (season.txt, the ledger; x/t<team>, a guest's exports;
 * m/t<team>, copies taken; in/m<machine>-t<team>, exports come to the master; out/m<machine>-t<team>, copies
 * for them), new/t<team> (a first copy), and mirror/m<machine>. */
#include "overbar/season.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsdir.h"

enum { HELLO = 1, WELCOME, SYNCED, FAIL, CLAIM, REFUSED, TEAMS, ROSTER };

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
    for (i = 0; i < OB_SEASON_TEAMS; i++) if (s->took[i]) n += (size_t)snprintf(t + n, cap - n, "took %d %lu\n", i, (unsigned long)s->took[i]);
    for (i = 0; i < s->nmerged; i++) n += (size_t)snprintf(t + n, cap - n, "merged %lu %d\n", (unsigned long)s->merged[i], s->merged_team[i]);
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
    memset(s->took, 0, sizeof s->took);
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
        else if (sscanf(line, "took %d %lu", &team, &v) == 2 && team >= 0 && team < OB_SEASON_TEAMS) s->took[team] = (uint32_t)v;
        else if (sscanf(line, "merged %lu", &v) == 1 && s->nmerged < OB_SEASON_MERGED) {
            if (sscanf(line, "merged %lu %d", &v, &team) != 2 || team < -1 || team >= OB_SEASON_TEAMS) team = -1;
            s->merged[s->nmerged] = (uint32_t)v;
            s->merged_team[s->nmerged++] = (signed char)team;
        }
        else if (sscanf(line, "played %lu", &v) == 1 && s->nplayed < OB_SEASON_PLAYED) s->played[s->nplayed++] = (uint32_t)v;
    }
    free(data);
    return 0;
}

/* the game's result for team merged already (each player's copy brings its own team's) */
static int was_merged(const ob_season *s, uint32_t key, int team)
{
    int i;
    for (i = 0; i < s->nmerged; i++) if (s->merged[i] == key && (s->merged_team[i] < 0 || s->merged_team[i] == team)) return 1;
    return 0;
}

static void add_merged(ob_season *s, uint32_t key, int team)
{
    if (was_merged(s, key, team)) return;
    if (s->nmerged == OB_SEASON_MERGED) {
        memmove(s->merged, s->merged + 1, (OB_SEASON_MERGED - 1) * sizeof s->merged[0]);
        memmove(s->merged_team, s->merged_team + 1, (OB_SEASON_MERGED - 1) * sizeof s->merged_team[0]);
        s->nmerged--;
    }
    s->merged[s->nmerged] = key;
    s->merged_team[s->nmerged++] = (signed char)team;
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
    s->claim = -1;
    s->disk_team = -1;
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
    int nfresh, i, j;
    for (i = 0; i < p->nteam; i++) {
        for (nfresh = j = 0; j < p->nplayed; j++) if (!was_merged(s, p->played[j], p->team[i])) fresh[nfresh++] = p->played[j];
        peer_dir(s, "in", m, p->team[i], dir, sizeof dir);
        if (!s->ops.merge_team || s->ops.merge_team(s->ops.ud, p->team[i], dir, fresh, nfresh)) {
            snprintf(s->why, sizeof s->why, "the merge of %s's team failed", p->name);
            fail_to(s, m, "the host's merge failed");
            p->state = 0;
            return;
        }
    }
    for (i = 0; i < p->nteam; i++) for (j = 0; j < p->nplayed; j++) add_merged(s, p->played[j], p->team[i]);
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
    if (!strcmp(tag, "L")) {                        /* a mirror: a player's copy */
        if (from < 0 || from >= OB_NET_MAX_MACHINES) return 0;
        snprintf(d, sizeof d, "%s/mirror", s->base);
        ob_fs_mkdir(d);
        snprintf(d, sizeof d, "%s/mirror/m%d", s->base, from);
        if ((int)strlen(d) >= size) return 0;
        memcpy(dir, d, strlen(d) + 1);
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
        if (!out && ok && peer >= 0 && peer < OB_NET_MAX_MACHINES) s->mirror_have |= 1u << peer;
        return;
    }
    if (tag[0] == 'g') {
        if (out || !ok || team < 0 || team >= OB_SEASON_TEAMS) return;
        snprintf(dir, sizeof dir, "%s/new/t%d", s->base, team);
        if (s->ops.give_as_disk) {                  /* the player's disk: the game's menus load it */
            s->given |= 1u << team;
            s->disk |= 1u << team;
            s->disk_team = team;
            if (s->claim == team) s->claim = OB_SEASON_TEAMS + team;
            s->changes++;
            return;
        }
        if (!s->ops.import_team || s->ops.import_team(s->ops.ud, team, dir)) { snprintf(s->why, sizeof s->why, "the game's import of a first copy failed"); return; }
        s->given |= 1u << team;
        if (s->claim == team) s->claim = OB_SEASON_TEAMS + team;
        s->changes++;
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

/* ---- picking a team ---- */

static void refuse(ob_season *s, int to, int team, const char *why)
{
    wr w;
    w.n = 0;
    put8(&w, REFUSED);
    put8(&w, (unsigned)team);
    putstr(&w, why);
    emit(s, to, &w);
}

static const char *set_up_for(ob_season *s, int t);

static void got_claim(ob_season *s, int from, rd *r)
{
    char name[OB_NET_NAME + 1], why[96];
    int t;
    getstr(r, name, sizeof name);
    t = (int)get8(r);
    if (r->bad || from < 0 || from >= OB_NET_MAX_MACHINES) return;
    if (t >= OB_SEASON_TEAMS || !name[0]) { refuse(s, from, t, "no such team"); return; }
    if (load(s) || !s->league.master) { refuse(s, from, t, "the host has no master league"); return; }
    if (s->owner[t][0] && ob_fs_stricmp(s->owner[t], name)) {
        snprintf(why, sizeof why, "that team is %s's", s->owner[t]);
        refuse(s, from, t, why);
        return;
    }
    if (!s->owner[t][0]) {
        if (team_index(s->league.team, s->league.nteam, t) >= 0) { refuse(s, from, t, "that team is the host's"); return; }
        if (set_up_for(s, t)) {                     /* set up for a player away: that copy, no question */
            if (ob_season_give(s, from, name, t)) refuse(s, from, t, "the host could not make a copy");
            return;
        }
        if (!s->ops.add_human) { refuse(s, from, t, "the host adds the human teams"); return; }
        if (s->ops.add_human(s->ops.ud, t, name)) { refuse(s, from, t, "the host's game could not add that team"); return; }
    }
    if (ob_season_give(s, from, name, t)) refuse(s, from, t, "the host could not make a copy");
}

static const char *set_up_for(ob_season *s, int t)
{
    const char *p = s->ops.player ? s->ops.player(s->ops.ud, t) : NULL;
    return p && p[0] ? p : NULL;
}

int ob_season_roster(ob_season *s, int kind[OB_SEASON_TEAMS], char who[OB_SEASON_TEAMS][OB_NET_NAME + 1])
{
    int t;
    if (load(s) || !s->league.master) return -1;
    for (t = 0; t < OB_SEASON_TEAMS; t++) {
        const char *p = set_up_for(s, t);
        who[t][0] = 0;
        if (s->owner[t][0]) {
            kind[t] = OB_SEASON_OWNED;
            snprintf(who[t], OB_NET_NAME + 1, "%s", p ? p : s->owner[t]);
        } else if (team_index(s->league.team, s->league.nteam, t) >= 0) {
            kind[t] = OB_SEASON_HOSTS;
            if (p) snprintf(who[t], OB_NET_NAME + 1, "%s", p);
            else if (s->me[0]) snprintf(who[t], OB_NET_NAME + 1, "%s", s->me);
        } else if (p) {
            kind[t] = OB_SEASON_WAITING;
            snprintf(who[t], OB_NET_NAME + 1, "%s", p);
        } else
            kind[t] = OB_SEASON_FREE;
    }
    return 0;
}

static void got_teams(ob_season *s, int from)
{
    wr w;
    int kind[OB_SEASON_TEAMS], t, n = 0;
    char who[OB_SEASON_TEAMS][OB_NET_NAME + 1];
    size_t at;
    if (from < 0 || from >= OB_NET_MAX_MACHINES) return;
    w.n = 0;
    put8(&w, ROSTER);
    if (ob_season_roster(s, kind, who)) {           /* no league open on the host: say so (an empty list) */
        putstr(&w, "");
        put8(&w, 0);
        emit(s, from, &w);
        return;
    }
    putstr(&w, s->league.name);
    at = w.n;
    put8(&w, 0);
    for (t = 0; t < OB_SEASON_TEAMS; t++) {
        if (kind[t] == OB_SEASON_FREE) continue;
        put8(&w, (unsigned)t);
        put8(&w, (unsigned)kind[t]);
        putstr(&w, who[t]);
        n++;
    }
    w.b[at] = (uint8_t)n;
    emit(s, from, &w);
}

static void got_roster(ob_season *s, int from, rd *r)
{
    char lname[OB_SEASON_TEXT + 1];
    int kind[OB_SEASON_TEAMS];
    char who[OB_SEASON_TEAMS][OB_NET_NAME + 1];
    int n, i;
    if (from != s->host) return;
    memset(who, 0, sizeof who);
    for (i = 0; i < OB_SEASON_TEAMS; i++) kind[i] = OB_SEASON_FREE;
    getstr(r, lname, sizeof lname);
    n = (int)get8(r);
    for (i = 0; i < n; i++) {
        int t = (int)get8(r), k = (int)get8(r);
        char nm[OB_NET_NAME + 1];
        getstr(r, nm, sizeof nm);
        if (t < OB_SEASON_TEAMS && (k == OB_SEASON_HOSTS || k == OB_SEASON_OWNED || k == OB_SEASON_WAITING)) { kind[t] = k; memcpy(who[t], nm, sizeof nm); }
    }
    if (r->bad) return;
    memcpy(s->roster_kind, kind, sizeof kind);
    memcpy(s->roster, who, sizeof who);
    memcpy(s->roster_league, lname, sizeof lname);
    s->roster_have = 1;
    s->changes++;
}

int ob_season_teams(ob_season *s, int host)
{
    wr w;
    if (host < 0 || host >= OB_NET_MAX_MACHINES) return -1;
    s->host = host;                     /* the last list stays shown until the new one comes */
    w.n = 0;
    put8(&w, TEAMS);
    emit(s, host, &w);
    return 0;
}

int ob_season_disk(const ob_season *s, int team, char *dir, int size)
{
    char d[DIRLEN];
    if (team < 0) team = s->disk_team;
    if (team < 0 || team >= OB_SEASON_TEAMS || !(s->disk >> team & 1)) return -1;
    snprintf(d, sizeof d, "%s/new/t%d", s->base, team);
    if ((int)strlen(d) >= size) return -1;
    memcpy(dir, d, strlen(d) + 1);
    return team;
}

int ob_season_claim(ob_season *s, int host, int team)
{
    wr w;
    if (host < 0 || host >= OB_NET_MAX_MACHINES || team < 0 || team >= OB_SEASON_TEAMS || !s->me[0]) return -1;
    s->host = host;
    s->claim = team;
    s->why[0] = 0;
    w.n = 0;
    put8(&w, CLAIM);
    putstr(&w, s->me);
    put8(&w, (unsigned)team);
    emit(s, host, &w);
    return 0;
}

/* ---- a team as a file ---- */

typedef struct buf { char *p; size_t n, cap; int bad; } buf;
static void bput(buf *b, const void *d, size_t n)
{
    if (b->bad) return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        char *q;
        while (c < b->n + n) c *= 2;
        if (!(q = (char *)realloc(b->p, c))) { b->bad = 1; return; }
        b->p = q;
        b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void bline(buf *b, const char *key, const char *v)
{
    bput(b, key, strlen(key));
    bput(b, " ", 1);
    bput(b, v, strlen(v));
    bput(b, "\n", 1);
}

/* the file: a header and dir's files */
static int pack_write(const ob_season *s, const char *path, int team, const char *player, const char *kind,
                      const char *keyword, const uint32_t *keys, int nkeys, const char *dir)
{
    static char files[256][OB_FS_NAME];
    char num[32], fp[DIRLEN + OB_FS_NAME + 2];
    buf b;
    int n, i, err;
    memset(&b, 0, sizeof b);
    bput(&b, "OBTEAM 1\n", 9);
    bline(&b, "league", s->league.name);
    bline(&b, "id", s->league.id);
    snprintf(num, sizeof num, "%d", team);
    bline(&b, "team", num);
    bline(&b, "player", player);
    bline(&b, "kind", kind);
    for (i = 0; i < nkeys; i++) { snprintf(num, sizeof num, "%lu", (unsigned long)keys[i]); bline(&b, keyword, num); }
    n = ob_fs_list(dir, files, 256);
    if (n < 0) { free(b.p); return -1; }
    for (i = 0; i < n; i++) {
        uint8_t *data;
        size_t size;
        char head[OB_FS_NAME + 32];
        ob_fs_join(fp, sizeof fp, dir, files[i]);
        if (ob_fs_read(fp, &data, &size)) { free(b.p); return -1; }
        snprintf(head, sizeof head, "file %.63s %lu\n", files[i], (unsigned long)size);
        bput(&b, head, strlen(head));
        bput(&b, data, size);
        bput(&b, "\n", 1);
        free(data);
    }
    bput(&b, "end\n", 4);
    err = b.bad ? -1 : ob_fs_write(path, b.p, b.n);
    free(b.p);
    return err;
}

static void cut(char *out, size_t cap, const char *in)
{
    size_t n = strlen(in);
    if (n >= cap) n = cap - 1;
    memcpy(out, in, n);
    out[n] = 0;
}

typedef struct pack {
    char league[OB_SEASON_TEXT + 1], id[OB_SEASON_TEXT + 1], player[OB_NET_NAME + 1], kind[16];
    int team;
    uint32_t keys[OB_SEASON_PLAYED];
    int nkeys;
} pack;

/* reads path's header into pk and, with dir, its files into dir (made, emptied first): 0, or -1 */
static int pack_read(const char *path, pack *pk, const char *keyword, const char *dir)
{
    uint8_t *data;
    size_t size, at = 0;
    int ended = 0, first = 1;
    memset(pk, 0, sizeof *pk);
    pk->team = -1;
    if (ob_fs_read(path, &data, &size)) return -1;
    if (dir) { ob_fs_clear(dir); ob_fs_mkdir(dir); }
    while (at < size && !ended) {
        char line[OB_FS_NAME + 96], *nl = (char *)memchr(data + at, '\n', size - at);
        size_t len = nl ? (size_t)(nl - (char *)data) - at : size - at;
        unsigned long v, fsize;
        char name[OB_FS_NAME];
        if (!nl || len >= sizeof line) break;
        memcpy(line, data + at, len);
        line[len] = 0;
        at += len + 1;
        if (first) { if (strcmp(line, "OBTEAM 1")) break; first = 0; continue; }
        if (!strncmp(line, "league ", 7)) cut(pk->league, sizeof pk->league, line + 7);
        else if (!strncmp(line, "id ", 3)) cut(pk->id, sizeof pk->id, line + 3);
        else if (!strncmp(line, "player ", 7)) cut(pk->player, sizeof pk->player, line + 7);
        else if (!strncmp(line, "kind ", 5)) cut(pk->kind, sizeof pk->kind, line + 5);
        else if (sscanf(line, "team %d", &pk->team) == 1) {}
        else if (!strncmp(line, keyword, strlen(keyword)) && line[strlen(keyword)] == ' ' &&
                 sscanf(line + strlen(keyword) + 1, "%lu", &v) == 1) { if (pk->nkeys < OB_SEASON_PLAYED) pk->keys[pk->nkeys++] = (uint32_t)v; }
        else if (sscanf(line, "file %63s %lu", name, &fsize) == 2) {
            char fp[DIRLEN + OB_FS_NAME + 2];
            if (fsize > size - at || strchr(name, '/') || strchr(name, '\\') || name[0] == '.') break;
            if (dir) { ob_fs_join(fp, sizeof fp, dir, name); if (ob_fs_write(fp, data + at, (size_t)fsize)) break; }
            at += (size_t)fsize + 1;
        } else if (!strcmp(line, "end")) ended = 1;
    }
    free(data);
    if (!ended || pk->team < 0 || pk->team >= OB_SEASON_TEAMS || !pk->league[0]) return -1;
    return 0;
}

int ob_season_pack(ob_season *s, int team, const char *path)
{
    char dir[DIRLEN], leaf[16];
    if (load(s) || s->league.master) { snprintf(s->why, sizeof s->why, "no league copy here"); return -1; }
    if (team < 0 || team >= OB_SEASON_TEAMS || team_index(s->league.team, s->league.nteam, team) < 0) {
        snprintf(s->why, sizeof s->why, "that team is not one of this copy's");
        return -1;
    }
    snprintf(leaf, sizeof leaf, "t%d", team);
    if (make(s, "x", leaf, dir, sizeof dir)) return -1;
    ob_fs_clear(dir);
    ob_fs_mkdir(dir);
    if (!s->ops.export_team || s->ops.export_team(s->ops.ud, team, dir)) { snprintf(s->why, sizeof s->why, "the game's export failed"); return -1; }
    if (pack_write(s, path, team, s->me, "export", "played", s->played, s->nplayed, dir)) {
        snprintf(s->why, sizeof s->why, "could not write the team's file");
        return -1;
    }
    return 0;
}

/* (the master) team's fresh copy for player into the file path */
static int copy_out(ob_season *s, int team, const char *player, const char *kind, const uint32_t *keys, int nkeys, const char *path)
{
    char dir[DIRLEN], leaf[24];
    snprintf(leaf, sizeof leaf, "f-t%d", team);
    if (make(s, "out", leaf, dir, sizeof dir)) return -1;
    ob_fs_clear(dir);
    ob_fs_mkdir(dir);
    if (!s->ops.copy_team || s->ops.copy_team(s->ops.ud, team, dir) ||
        pack_write(s, path, team, player, kind, "merged", keys, nkeys, dir)) {
        snprintf(s->why, sizeof s->why, "the host could not make a copy");
        return -1;
    }
    ob_fs_clear(dir);
    return 0;
}

int ob_season_take_pack(ob_season *s, const char *path, const char *back)
{
    pack pk;
    char dir[DIRLEN], leaf[24];
    uint32_t fresh[OB_SEASON_PLAYED];
    int nfresh = 0, i;
    if (load(s) || !s->league.master) { snprintf(s->why, sizeof s->why, "no master league here"); return -1; }
    snprintf(leaf, sizeof leaf, "f-t-in");
    if (make(s, "in", leaf, dir, sizeof dir)) return -1;
    if (pack_read(path, &pk, "played", dir) || strcmp(pk.kind, "export")) {
        snprintf(s->why, sizeof s->why, "not a team's file, or a damaged one");
        ob_fs_clear(dir);
        return -1;
    }
    if (strcmp(pk.league, s->league.name) || strcmp(pk.id, s->league.id)) {
        snprintf(s->why, sizeof s->why, "that team's league is %s, not this one", pk.league);
        ob_fs_clear(dir);
        return -1;
    }
    if (!pk.player[0] || (s->owner[pk.team][0] && ob_fs_stricmp(s->owner[pk.team], pk.player))) {
        snprintf(s->why, sizeof s->why, "that team is %s's", s->owner[pk.team][0] ? s->owner[pk.team] : "nobody");
        ob_fs_clear(dir);
        return -1;
    }
    {                                           /* the same file again: its copy back, no merge (it would count twice) */
        uint8_t *data;
        size_t size;
        uint32_t h = 1;
        if (!ob_fs_read(path, &data, &size)) { h = (uint32_t)ob_fs_hash(data, size) | 1u; free(data); }
        if (s->took[pk.team] == h) { ob_fs_clear(dir); return copy_out(s, pk.team, pk.player, "copy", pk.keys, pk.nkeys, back); }
        s->took[pk.team] = h;
    }
    for (i = 0; i < pk.nkeys; i++) if (!was_merged(s, pk.keys[i], pk.team)) fresh[nfresh++] = pk.keys[i];
    if (!s->ops.merge_team || s->ops.merge_team(s->ops.ud, pk.team, dir, fresh, nfresh)) {
        snprintf(s->why, sizeof s->why, "the merge of %s's team failed", pk.player);
        s->took[pk.team] = 0;
        ob_fs_clear(dir);
        return -1;
    }
    ob_fs_clear(dir);
    if (!s->owner[pk.team][0]) snprintf(s->owner[pk.team], sizeof s->owner[pk.team], "%s", pk.player);
    for (i = 0; i < pk.nkeys; i++) add_merged(s, pk.keys[i], pk.team);
    save_ledger(s);
    return copy_out(s, pk.team, pk.player, "copy", pk.keys, pk.nkeys, back);
}

int ob_season_pack_copy(ob_season *s, const char *name, int team, const char *path)
{
    if (team < 0 || team >= OB_SEASON_TEAMS || !name || !name[0] || load(s) || !s->league.master) return -1;
    if (s->owner[team][0] && ob_fs_stricmp(s->owner[team], name)) { snprintf(s->why, sizeof s->why, "that team is %s's", s->owner[team]); return -1; }
    snprintf(s->owner[team], sizeof s->owner[team], "%s", name);
    save_ledger(s);
    return copy_out(s, team, name, "first", NULL, 0, path);
}

int ob_season_import_pack(ob_season *s, const char *path)
{
    pack pk;
    char dir[DIRLEN];
    int first, i, j, k;
    snprintf(dir, sizeof dir, "%s/new", s->base);
    ob_fs_mkdir(dir);
    snprintf(dir, sizeof dir, "%s/new/file", s->base);
    if (pack_read(path, &pk, "merged", dir) || (strcmp(pk.kind, "copy") && strcmp(pk.kind, "first"))) {
        snprintf(s->why, sizeof s->why, "not a copy from the host, or a damaged one");
        ob_fs_clear(dir);
        return -1;
    }
    first = !strcmp(pk.kind, "first");
    if (!first && (load(s) || strcmp(pk.league, s->league.name) || strcmp(pk.id, s->league.id))) {
        snprintf(s->why, sizeof s->why, "that copy is of %s, not this league", pk.league);
        ob_fs_clear(dir);
        return -1;
    }
    if (!s->ops.import_team || s->ops.import_team(s->ops.ud, pk.team, dir)) {
        snprintf(s->why, sizeof s->why, "the game's import failed");
        ob_fs_clear(dir);
        return -1;
    }
    ob_fs_clear(dir);
    if (first) { s->given |= 1u << pk.team; load(s); return 0; }
    /* the games the host merged go from the ledger (a copy for more teams keeps them till a sync: the host
     * takes each team's half once) */
    if (s->league.nteam > 1) return 0;
    for (i = k = 0; i < s->nplayed; i++) {
        int gone = 0;
        for (j = 0; j < pk.nkeys; j++) gone |= s->played[i] == pk.keys[j];
        if (!gone) s->played[k++] = s->played[i];
    }
    s->nplayed = k;
    s->sent_played = 0;
    save_ledger(s);
    return 0;
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
    case CLAIM: got_claim(s, from, &r); break;
    case REFUSED: {
        char why[80];
        int t = (int)get8(&r);
        getstr(&r, why, sizeof why);
        if (from == s->host && t == s->claim) { s->claim = -1; snprintf(s->why, sizeof s->why, "%s", why); s->changes++; }
        break;
    }
    case TEAMS: got_teams(s, from); break;
    case ROSTER: got_roster(s, from, &r); break;
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
    s->mirror_have &= ~(1u << machine);
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

int ob_season_mirror(const ob_season *s, int from, char *dir, int size)
{
    if (from < 0 || from >= OB_NET_MAX_MACHINES || !(s->mirror_have >> from & 1)) return 0;
    snprintf(dir, (size_t)size, "%s/mirror/m%d", s->base, from);
    return 1;
}

void ob_season_mirror_drop(ob_season *s)
{
    char dir[DIRLEN];
    int k;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) {
        snprintf(dir, sizeof dir, "%s/mirror/m%d", s->base, k);
        ob_fs_clear(dir);
    }
    snprintf(dir, sizeof dir, "%s/mirror", s->base);
    ob_fs_clear(dir);
    s->mirror_have = 0;
}
