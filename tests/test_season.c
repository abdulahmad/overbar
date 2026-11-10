/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The online season (overbar/season.h), folders sent (overbar/files.h) and virtual drives
 * (overbar/vdrive.h), over messages passed in memory between three machines. A toy league: a folder
 * with LEAGUE.ID and a file for each team, T<n>.DB, a line for each game it played ("<key> <other>");
 * the master also counts each team's games in COUNT.DB. Teams: A 0, B 1 (the host's), C 2 (Cal on
 * machine 1), D 3 (Dee on machine 2). The season: A vs B on the host; C vs D with the host away (as the
 * original does it with the opponent's floppy: both copies on each machine, each team's result into its own
 * copy, the other's a mirror); then both sync, Cal's first sync broken off after the merge. Every game must
 * be in the master once and each copy the same as the master after. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/files.h"
#include "overbar/season.h"
#include "overbar/vdrive.h"
#include "../src/fsdir.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s (line %d)\n", what, __LINE__); fails++; } } while (0)

#define ROOT "build_test_season"
#define NM 3
#define NTEAMS 6

/* ---- messages in memory ---- */

typedef struct msg { int from, to, type; uint8_t *data; size_t size; struct msg *next; } msg;
static msg *head, *tail;
static int online[NM];                  /* in the room */
static int cut_from = -1, cut_to = -1, cut_after = -1;  /* drop messages from -> to after this many more */
static int wants_seen, want_count;      /* the last WANT's count of files */

typedef struct mach { int k; ob_files f; ob_season s; char league[200]; int teams[4], nteams; } mach;
static mach M[NM];

static void post(int from, int to, int type, const void *data, size_t size)
{
    msg *m;
    if (!online[from] || !online[to]) return;
    if (from == cut_from && to == cut_to && cut_after >= 0) { if (cut_after == 0) return; cut_after--; }
    m = (msg *)calloc(1, sizeof *m);
    m->from = from; m->to = to; m->type = type; m->size = size;
    m->data = (uint8_t *)malloc(size ? size : 1);
    memcpy(m->data, data, size);
    if (tail) tail->next = m; else head = m;
    tail = m;
}
static void files_send(void *ud, int to, const void *data, size_t size) { post(((mach *)ud)->k, to, 242, data, size); }
static void season_send(void *ud, int to, const void *data, size_t size) { post(((mach *)ud)->k, to, 243, data, size); }

static void pump(void)
{
    while (head) {
        msg *m = head;
        head = m->next;
        if (!head) tail = NULL;
        if (online[m->to] && online[m->from]) {
            if (m->type == 242) {
                if (m->data[0] == 2) { wants_seen++; want_count = m->data[5] | m->data[6] << 8; }
                ob_files_message(&M[m->to].f, m->from, m->data, m->size);
            } else ob_season_message(&M[m->to].s, m->from, m->data, m->size);
        }
        free(m->data);
        free(m);
    }
}

/* a machine leaves the room: the others are told, its messages go */
static void leave(int k)
{
    int i;
    online[k] = 0;
    for (i = 0; i < NM; i++) if (i != k && online[i]) { ob_files_machine_gone(&M[i].f, k); ob_season_machine_gone(&M[i].s, k); }
    for (i = 0; i < NM; i++) if (i != k) { ob_files_machine_gone(&M[k].f, i); ob_season_machine_gone(&M[k].s, i); }
    pump();
}

/* ---- the toy league ---- */

static void path_of(char *out, size_t size, const char *dir, const char *name) { snprintf(out, size, "%s/%s", dir, name); }

static char *read_text(const char *dir, const char *name)
{
    char p[400];
    uint8_t *d;
    size_t n;
    path_of(p, sizeof p, dir, name);
    if (ob_fs_read(p, &d, &n)) return NULL;
    d = (uint8_t *)realloc(d, n + 1);
    d[n] = 0;
    return (char *)d;
}
static void write_text(const char *dir, const char *name, const char *t)
{
    char p[400];
    path_of(p, sizeof p, dir, name);
    ob_fs_write(p, t, strlen(t));
}
static void append(const char *dir, const char *name, const char *line)
{
    char *t = read_text(dir, name), *u;
    size_t n = t ? strlen(t) : 0;
    u = (char *)malloc(n + strlen(line) + 2);
    sprintf(u, "%s%s\n", t ? t : "", line);
    write_text(dir, name, u);
    free(t);
    free(u);
}
static int has_line(const char *dir, const char *name, const char *line)
{
    char *t = read_text(dir, name), *p;
    size_t n = strlen(line);
    int c = 0;
    for (p = t; p && (p = strstr(p, line)); p += n) if ((p == t || p[-1] == '\n') && p[n] == '\n') c++;
    free(t);
    return c;
}
static void copy_all(const char *from, const char *to)
{
    char names[64][OB_FS_NAME], p[400], q[400];
    int n = ob_fs_list(from, names, 64), i;
    ob_fs_mkdir(to);
    for (i = 0; i < n; i++) {
        uint8_t *d;
        size_t size;
        path_of(p, sizeof p, from, names[i]);
        path_of(q, sizeof q, to, names[i]);
        if (!ob_fs_read(p, &d, &size)) { ob_fs_write(q, d, size); free(d); }
    }
}
static int count_of(const char *dir, int team)
{
    char *t = read_text(dir, "COUNT.DB");
    int c[NTEAMS] = { 0 };
    if (t) sscanf(t, "%d %d %d %d %d %d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
    free(t);
    return c[team];
}
static void count_add(const char *dir, int team)
{
    char *t = read_text(dir, "COUNT.DB"), b[80];
    int c[NTEAMS] = { 0 };
    if (t) sscanf(t, "%d %d %d %d %d %d", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
    free(t);
    c[team]++;
    sprintf(b, "%d %d %d %d %d %d", c[0], c[1], c[2], c[3], c[4], c[5]);
    write_text(dir, "COUNT.DB", b);
}
static void tname(char *b, int t) { sprintf(b, "T%d.DB", t); }

/* one team's half of a game, in league folder dir */
static void half(const char *dir, unsigned key, int team, int other)
{
    char n[16], l[32];
    tname(n, team); sprintf(l, "%u %d", key, other); append(dir, n, l);
}

/* a game played in league folder dir */
static void play(const char *dir, unsigned key, int home, int away, int counted)
{
    char n[16], l[32];
    tname(n, home); sprintf(l, "%u %d", key, away); append(dir, n, l);
    tname(n, away); sprintf(l, "%u %d", key, home); append(dir, n, l);
    if (counted) { count_add(dir, home); count_add(dir, away); }
}

static int t_league(void *ud, ob_season_league *l)
{
    mach *m = (mach *)ud;
    char *t = read_text(m->league, "LEAGUE.ID");
    int i;
    if (!t) return -1;
    sscanf(t, "%31s %31s", l->name, l->id);
    free(t);
    l->master = m->k == 0;
    l->nteam = m->nteams;
    for (i = 0; i < m->nteams; i++) l->team[i] = m->teams[i];
    return 0;
}
static int t_export(void *ud, int team, const char *dir) { (void)team; copy_all(((mach *)ud)->league, dir); return 0; }
static int t_copy(void *ud, int team, const char *dir) { (void)team; copy_all(((mach *)ud)->league, dir); return 0; }
static int t_import(void *ud, int team, const char *dir)
{
    mach *m = (mach *)ud;
    int i;
    ob_fs_clear(m->league);
    copy_all(dir, m->league);
    for (i = 0; i < m->nteams; i++) if (m->teams[i] == team) return 0;
    m->teams[m->nteams++] = team;
    return 0;
}
static int merges, merged_keys;
/* a naive merge, as a game might do it: the copy's team's games not in the master yet; and every team's lines
 * for the games played away (no check: given twice, they would count twice) */
static int t_merge(void *ud, int team, const char *dir, const uint32_t *played, int nplayed)
{
    mach *m = (mach *)ud;
    char n[16], *t, *line, *next;
    int i, j;
    merges++;
    tname(n, team);
    t = read_text(dir, n);
    for (line = t; line && *line; line = next) {
        unsigned key;
        int other, away = 0;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (sscanf(line, "%u %d", &key, &other) != 2) continue;
        for (i = 0; i < nplayed; i++) away |= played[i] == key;
        if (!away && !has_line(m->league, n, line)) { append(m->league, n, line); count_add(m->league, team); }
    }
    free(t);
    for (i = 0; i < nplayed; i++) {
        merged_keys++;
        for (j = 0; j < NTEAMS; j++) {
            char nn[16], want[16], *tt, *l2, *nx;
            tname(nn, j);
            tt = read_text(dir, nn);
            sprintf(want, "%u ", (unsigned)played[i]);
            for (l2 = tt; l2 && *l2; l2 = nx) {
                nx = strchr(l2, '\n');
                if (nx) *nx++ = 0;
                if (!strncmp(l2, want, strlen(want))) { append(m->league, nn, l2); count_add(m->league, j); }
            }
            free(tt);
        }
    }
    return 0;
}
static unsigned added;                  /* teams the host's game made human for a player */
static int t_add(void *ud, int team, const char *name) { (void)ud; (void)name; added |= 1u << team; return 0; }
static int t_dir(void *ud, char *dir, int size) { snprintf(dir, (size_t)size, "%s", ((mach *)ud)->league); return 0; }

static void open_machine(int k, const char *name)
{
    mach *m = &M[k];
    ob_season_ops ops;
    char base[200];
    m->k = k;
    snprintf(m->league, sizeof m->league, ROOT "/m%d/league", k);
    snprintf(base, sizeof base, ROOT "/m%d/season", k);
    memset(&ops, 0, sizeof ops);
    ops.ud = m; ops.league = t_league; ops.export_team = t_export; ops.merge_team = t_merge;
    ops.copy_team = t_copy; ops.import_team = t_import; ops.league_dir = t_dir; ops.add_human = t_add;
    ob_files_init(&m->f, files_send, m);
    ob_season_init(&m->s, &ops, base, name, &m->f, season_send, m);
}

static int same_league(const char *a, const char *b)
{
    int t, same = 1;
    for (t = 0; t < NTEAMS; t++) {
        char n[16], *x, *y;
        tname(n, t);
        x = read_text(a, n);
        y = read_text(b, n);
        if ((x == NULL) != (y == NULL) || (x && strcmp(x, y))) same = 0;
        free(x);
        free(y);
    }
    return same;
}

/* ---- the tests ---- */

static void wipe(void)
{
    char d[200];
    const char *sub[] = { "league", "season/mirror", "season/new/t2", "season/new/t3", "season/new" };
    int k, i;
    for (k = 0; k < NM; k++) for (i = 0; i < 5; i++) { snprintf(d, sizeof d, ROOT "/m%d/%s", k, sub[i]); ob_fs_clear(d); }
    if (system("rm -rf " ROOT " 2>/dev/null")) {}
}

static void test_vdrive(void)
{
    ob_vdrive v;
    char out[400];
    unsigned tc, fc, spc, bps;
    ob_fs_mkdir(ROOT);
    ob_fs_mkdir(ROOT "/disk");
    ob_fs_mkdir(ROOT "/disk/Lg");
    write_text(ROOT "/disk", "Player.Id", "x");
    ob_vdrive_init(&v);
    ob_vdrive_mount(&v, 'A', ROOT "/disk");
    ob_vdrive_mount(&v, 'b', NULL);
    CHECK(ob_vdrive_path(&v, "a:\\PLAYER.ID", out, sizeof out) == 1 && !strcmp(out, ROOT "/disk/Player.Id"), "vdrive: a name found ignoring case");
    CHECK(ob_vdrive_path(&v, "A:\\lg\\new.db", out, sizeof out) == 1 && !strcmp(out, ROOT "/disk/Lg/NEW.DB"), "vdrive: a folder found, a new file in capitals");
    CHECK(ob_vdrive_path(&v, "A:PINFO.DB", out, sizeof out) == 1 && !strcmp(out, ROOT "/disk/PINFO.DB"), "vdrive: no backslash");
    CHECK(ob_vdrive_path(&v, "B:\\X", out, sizeof out) == -1, "vdrive: no disk in B:");
    CHECK(ob_vdrive_path(&v, "C:\\X", out, sizeof out) == 0, "vdrive: C: is the port's");
    CHECK(ob_vdrive_diskfree(&v, 'A', &tc, &fc, &spc, &bps) == 0 && tc == 65535 && fc == 65534 && bps == 512, "vdrive: free space");
    CHECK(ob_vdrive_diskfree(&v, 'B', &tc, &fc, &spc, &bps) == -1, "vdrive: no disk, no space");
    ob_vdrive_unmount(&v, 'A');
    CHECK(ob_vdrive_path(&v, "A:\\PLAYER.ID", out, sizeof out) == 0, "vdrive: unmounted");
}

static int f_done[NM], f_ok[NM];
static char f_why[NM][120];
static int f_where(void *ud, int from, const char *tag, char *dir, int size)
{
    (void)from;
    if (strcmp(tag, "box")) return 0;
    snprintf(dir, (size_t)size, ROOT "/m%d/box", ((mach *)ud)->k);
    return 1;
}
static void f_end(void *ud, int out, int peer, const char *tag, int ok, const char *why)
{
    int k = ((mach *)ud)->k;
    (void)out; (void)peer; (void)tag;
    f_done[k]++;
    f_ok[k] = ok;
    snprintf(f_why[k], sizeof f_why[k], "%s", why);
}

static void test_files(void)
{
    char d[200], n[16];
    int i;
    online[0] = online[1] = 1;
    for (i = 0; i < 2; i++) { M[i].k = i; ob_files_init(&M[i].f, files_send, &M[i]); ob_files_hooks(&M[i].f, f_where, f_end, &M[i]); }
    ob_fs_mkdir(ROOT "/m0"); ob_fs_mkdir(ROOT "/m1");
    ob_fs_mkdir(ROOT "/m0/src");
    for (i = 0; i < 5; i++) { char t[4200]; sprintf(n, "F%d.DB", i); snprintf(t, sizeof t, "file %d %0*d", i, 100 + i * 1000, 7); write_text(ROOT "/m0/src", n, t); }
    ob_fs_mkdir(ROOT "/m1/box");
    write_text(ROOT "/m1/box", "OLD.DB", "goes with a mirror");
    write_text(ROOT "/m1/box", "f0.db", "an older F0.DB, another case");
    /* broken off after the WANT and 2 files */
    cut_from = 0; cut_to = 1; cut_after = 3;    /* OFFER, then 2 DATA (the WANT goes the other way) */
    CHECK(ob_files_send(&M[0].f, 1, "box", ROOT "/m0/src", OB_FILES_MIRROR) == 0, "files: offered");
    pump();
    CHECK(want_count == 5, "files: all 5 wanted the first time");
    leave(1);
    CHECK(f_done[0] == 1 && !f_ok[0] && f_done[1] == 1 && !f_ok[1], "files: both told it ended");
    cut_after = -1;
    online[1] = 1;
    CHECK(ob_files_send(&M[0].f, 1, "box", ROOT "/m0/src", OB_FILES_MIRROR) == 0, "files: offered again");
    pump();
    CHECK(want_count == 3, "files: only the 3 that had not come asked for again");
    CHECK(f_done[0] == 2 && f_ok[0] && f_done[1] == 2 && f_ok[1], "files: both told it went");
    CHECK(same_league(ROOT "/m0/src", ROOT "/m1/box"), "files: (team files) the same");
    {
        char names[16][OB_FS_NAME];
        int c = ob_fs_list(ROOT "/m1/box", names, 16), same = c == 5;
        for (i = 0; i < c && same; i++) { char *x, *y; x = read_text(ROOT "/m0/src", names[i]); y = read_text(ROOT "/m1/box", names[i]); same = x && y && !strcmp(x, y); free(x); free(y); }
        CHECK(same, "files: the mirror holds exactly the 5 files sent, each the same");
    }
    snprintf(d, sizeof d, ROOT "/m1/box.obpart");
    CHECK(!ob_fs_is_dir(d), "files: the staging folder went");
    /* nothing new: nothing asked for */
    ob_files_send(&M[0].f, 1, "box", ROOT "/m0/src", 0);
    pump();
    CHECK(want_count == 0 && f_ok[1], "files: the same folder again sends nothing");
    /* refused */
    ob_files_send(&M[0].f, 1, "nope", ROOT "/m0/src", 0);
    pump();
    CHECK(!f_ok[0] && !strcmp(f_why[0], "refused"), "files: a tag nobody takes is refused");
    for (i = 0; i < 2; i++) ob_files_free(&M[i].f);
    printf("files: broken off after 2 of 5, sent again: 3 asked for; mirror ok\n");
}

static void sync_one(int k)
{
    CHECK(ob_season_sync(&M[k].s, 0) == 0, "season: a guest syncs");
    pump();
}

static void test_season(void)
{
    char line[120];
    int k;
    ob_fs_mkdir(ROOT);
    for (k = 0; k < NM; k++) { char d[64]; online[k] = 1; memset(&M[k], 0, sizeof M[k]); snprintf(d, sizeof d, ROOT "/m%d", k); ob_fs_mkdir(d); }
    open_machine(0, "Ann");
    open_machine(1, "Cal");
    open_machine(2, "Dee");
    ob_fs_mkdir(ROOT "/m0"); ob_fs_mkdir(ROOT "/m0/league");
    write_text(M[0].league, "LEAGUE.ID", "Hockey 4711");
    for (k = 0; k < NTEAMS; k++) { char n[16]; tname(n, k); write_text(M[0].league, n, ""); }
    M[0].teams[0] = 0; M[0].teams[1] = 1; M[0].nteams = 2;
    ob_fs_mkdir(ROOT "/m1"); ob_fs_mkdir(ROOT "/m2");

    /* the host makes C Cal's and D Dee's: their first copies */
    CHECK(ob_season_give(&M[0].s, 1, "Cal", 2) == 0 && ob_season_give(&M[0].s, 2, "Dee", 3) == 0, "season: first copies given");
    pump();
    CHECK(M[1].s.given == 1u << 2 && M[2].s.given == 1u << 3, "season: first copies imported");
    CHECK(same_league(M[0].league, M[1].league) && same_league(M[0].league, M[2].league), "season: the copies are the master's");
    CHECK(ob_season_give(&M[0].s, 2, "Dee", 2) == -1, "season: C is Cal's, not Dee's");

    /* A vs B on the host; Cal plays C vs a computer team on his copy */
    play(M[0].league, 1, 0, 1, 1);
    play(M[1].league, 2, 2, 4, 0);

    /* C vs D with the host away: each sends the other its copy (the opponent's floppy) */
    leave(0);
    online[1] = online[2] = 1;
    CHECK(ob_season_mirror_send(&M[1].s, 1u << 2) == 0 && ob_season_mirror_send(&M[2].s, 1u << 1) == 0,
          "season: Cal and Dee send their copies as mirrors");
    pump();
    CHECK(ob_season_mirror_sent(&M[1].s) == 1 && ob_season_mirror_sent(&M[2].s) == 1, "season: each took the other's");
    {
        char dc[300], dd[300];
        CHECK(ob_season_mirror(&M[2].s, 1, dc, sizeof dc) == 1 && same_league(dc, M[1].league), "season: Dee has Cal's copy");
        CHECK(ob_season_mirror(&M[1].s, 2, dd, sizeof dd) == 1 && same_league(dd, M[2].league), "season: Cal has Dee's copy");
        CHECK(!ob_season_mirror(&M[1].s, 1, dc, sizeof dc), "season: no mirror of one's own copy");
        ob_season_mirror(&M[2].s, 1, dc, sizeof dc);
        half(M[1].league, 100, 2, 3);                /* Cal's game: C's half into his copy, D's into the mirror */
        half(dd, 100, 3, 2);
        half(dc, 100, 2, 3);                         /* Dee's, the same game: C's half into the mirror, D's into hers */
        half(M[2].league, 100, 3, 2);
        ob_season_mirror_drop(&M[1].s);
        ob_season_mirror_drop(&M[2].s);
        CHECK(!ob_season_mirror(&M[2].s, 1, dc, sizeof dc) && !ob_fs_is_dir(dc) && !ob_fs_is_dir(dd), "season: the mirrors went");
        CHECK(has_line(M[2].league, "T2.DB", "100 3") == 0 && has_line(M[1].league, "T3.DB", "100 2") == 0,
              "season: neither copy kept the mirror's writes");
        CHECK(has_line(M[2].league, "T3.DB", "100 2") == 1 && has_line(M[1].league, "T2.DB", "100 3") == 1,
              "season: the game is played on both copies (neither can play it again)");
    }
    ob_season_played(&M[1].s, 100);
    ob_season_played(&M[2].s, 100);

    /* back with the host. Cal's first sync is broken off after the merge (his copy back never comes) */
    leave(1); leave(2);
    for (k = 0; k < NM; k++) online[k] = 1;
    cut_from = 0; cut_to = 1; cut_after = 2;     /* the WELCOME and the files' WANT, then nothing */
    sync_one(1);
    CHECK(M[0].s.peer[1].state == 2 && merged_keys == 1, "season: the master merged Cal's copy (game 100 handed once)");
    ob_season_status(&M[0].s, line, sizeof line);
    CHECK(!strcmp(line, "Syncing league with Cal"), "season: the host's line says so");
    leave(1);
    CHECK(M[1].s.state == OB_SEASON_FAILED && M[1].s.nplayed == 1, "season: Cal's sync failed; game 100 still his to send");
    cut_after = -1;
    online[1] = 1;
    sync_one(1);
    CHECK(M[1].s.state == OB_SEASON_SYNCED && M[1].s.nplayed == 0, "season: Cal synced; game 100 merged");
    CHECK(merged_keys == 1, "season: game 100 not handed to the merge again");
    sync_one(2);
    CHECK(M[2].s.state == OB_SEASON_SYNCED && M[2].s.nplayed == 0, "season: Dee synced");
    CHECK(merged_keys == 2, "season: game 100 handed once more, for D");
    ob_season_status(&M[2].s, line, sizeof line);
    CHECK(!strcmp(line, "League synced"), "season: Dee's line");

    /* every game once in the master */
    CHECK(has_line(M[0].league, "T0.DB", "1 1") == 1 && has_line(M[0].league, "T1.DB", "1 0") == 1, "season: A vs B once");
    CHECK(has_line(M[0].league, "T2.DB", "2 4") == 1, "season: Cal's game against the computer once");
    CHECK(has_line(M[0].league, "T2.DB", "100 3") == 1 && has_line(M[0].league, "T3.DB", "100 2") == 1, "season: C vs D once for each");
    CHECK(count_of(M[0].league, 0) == 1 && count_of(M[0].league, 1) == 1 && count_of(M[0].league, 2) == 2 && count_of(M[0].league, 3) == 1,
          "season: each game counted once");
    CHECK(same_league(M[0].league, M[2].league), "season: Dee's copy is the master's");
    sync_one(1);                                /* Cal's copy took D's half at his next sync (as with the original's update) */
    CHECK(M[1].s.state == OB_SEASON_SYNCED && merged_keys == 2, "season: Cal synced again, nothing more merged");
    CHECK(same_league(M[0].league, M[1].league) && same_league(M[0].league, M[2].league), "season: both copies are the master's again");
    CHECK(count_of(M[0].league, 2) == 2 && count_of(M[0].league, 3) == 1, "season: still each game once");

    /* Dee cannot bring Cal's team */
    M[2].teams[M[2].nteams++] = 2;
    sync_one(2);
    CHECK(M[2].s.state == OB_SEASON_SYNCED && M[2].s.nteam == 1 && M[2].s.team[0] == 3, "season: only Dee's own team taken");
    M[2].nteams--;

    /* another league */
    write_text(M[2].league, "LEAGUE.ID", "Other 1");
    sync_one(2);
    CHECK(M[2].s.state == OB_SEASON_FAILED, "season: another league is refused");
    ob_season_status(&M[2].s, line, sizeof line);
    CHECK(strstr(line, "League not synced") != NULL, "season: and says so");
    write_text(M[2].league, "LEAGUE.ID", "Hockey 4711");

    /* the ledger is kept: a new start of the host knows game 100 was merged and who owns C */
    ob_season_free(&M[0].s);
    open_machine(0, "Ann");
    {
        ob_season_league l;
        int was = 0, i;
        t_league(&M[0], &l);
        CHECK(ob_season_give(&M[0].s, 2, "Dee", 2) == -1, "season: (after a restart) C is still Cal's");
        for (i = 0; i < M[0].s.nmerged; i++) was |= M[0].s.merged[i] == 100;
        CHECK(was, "season: (after a restart) game 100 known as merged");
    }
    for (k = 0; k < NM; k++) { ob_season_free(&M[k].s); ob_files_free(&M[k].f); }
    printf("season: first copies, A vs B, C vs D away (both copies), a sync broken off after the merge: each game once\n");
}

/* players pick their teams in the room; a team as a file for a player away from it */
static void test_pick_and_pack(void)
{
    char back[200];
    int k;
    ob_fs_mkdir(ROOT);
    added = 0;
    merges = merged_keys = 0;
    for (k = 0; k < NM; k++) { char d[64]; online[k] = 1; memset(&M[k], 0, sizeof M[k]); snprintf(d, sizeof d, ROOT "/m%d", k); ob_fs_mkdir(d); }
    open_machine(0, "Ann");
    open_machine(1, "Cal");
    open_machine(2, "Dee");
    ob_fs_mkdir(ROOT "/m0/league");
    write_text(M[0].league, "LEAGUE.ID", "Hockey 4711");
    for (k = 0; k < NTEAMS; k++) { char n[16]; tname(n, k); write_text(M[0].league, n, ""); }
    M[0].teams[0] = 0; M[0].nteams = 1;

    /* Cal picks C in the room */
    CHECK(ob_season_teams(&M[1].s, 0) == 0, "pick: Cal asks who has which team");
    pump();
    CHECK(M[1].s.roster_have && !strcmp(M[1].s.roster_league, "Hockey") && M[1].s.roster_kind[0] == OB_SEASON_HOSTS &&
          M[1].s.roster_kind[2] == OB_SEASON_FREE, "pick: A is the host's, C is free");
    CHECK(ob_season_claim(&M[1].s, 0, 2) == 0, "pick: Cal asks for C");
    pump();
    CHECK(added == 1u << 2 && M[1].s.claim == OB_SEASON_TEAMS + 2 && M[1].s.given == 1u << 2, "pick: the host's game made C human, Cal has his copy");
    CHECK(same_league(M[0].league, M[1].league), "pick: Cal's copy is the master's");
    ob_season_claim(&M[1].s, 0, 0);
    pump();
    CHECK(M[1].s.claim == -1 && strstr(M[1].s.why, "host's") != NULL, "pick: the host's own team is refused");
    ob_season_claim(&M[2].s, 0, 2);
    pump();
    CHECK(M[2].s.claim == -1 && strstr(M[2].s.why, "Cal's") != NULL, "pick: Dee cannot have Cal's team");
    ob_season_teams(&M[2].s, 0);
    pump();
    CHECK(M[2].s.roster_kind[2] == OB_SEASON_OWNED && !strcmp(M[2].s.roster[2], "Cal"), "pick: the list says C is Cal's");

    /* Dee away from the room: D's first copy as a file, her games, her team as a file, a copy back */
    leave(2);
    CHECK(ob_season_pack_copy(&M[0].s, "Dee", 3, ROOT "/first.obt") == 0, "pack: the host writes D's first copy for Dee");
    CHECK(ob_season_import_pack(&M[2].s, ROOT "/first.obt") == 0 && M[2].s.given == 1u << 3 && same_league(M[0].league, M[2].league),
          "pack: Dee imports it");
    CHECK(ob_season_pack(&M[2].s, 2, ROOT "/x.obt") == -1, "pack: Dee cannot write Cal's team");
    play(M[2].league, 7, 3, 5, 0);
    ob_season_played(&M[2].s, 7);
    CHECK(ob_season_pack(&M[2].s, 3, ROOT "/dee.obt") == 0, "pack: Dee writes D as a file");
    snprintf(back, sizeof back, ROOT "/dee-back.obt");
    CHECK(ob_season_take_pack(&M[0].s, ROOT "/dee.obt", back) == 0 && merged_keys == 1, "pack: the host takes it (game 7 merged once)");
    CHECK(has_line(M[0].league, "T3.DB", "7 5") == 1 && count_of(M[0].league, 3) == 1, "pack: D's game is in the master once");
    k = merges;
    CHECK(ob_season_take_pack(&M[0].s, ROOT "/dee.obt", back) == 0 && merges == k && count_of(M[0].league, 3) == 1,
          "pack: the same file again is not merged again (its copy back only)");
    CHECK(ob_season_import_pack(&M[2].s, back) == 0 && M[2].s.nplayed == 0 && same_league(M[0].league, M[2].league),
          "pack: Dee imports the copy back; game 7 is off her ledger");
    CHECK(ob_season_take_pack(&M[0].s, back, ROOT "/y.obt") == -1, "pack: a copy is not an export");
    CHECK(ob_season_import_pack(&M[2].s, ROOT "/dee.obt") == -1, "pack: an export is not a copy");
    write_text(ROOT, "bad.obt", "OBTEAM 1\nleague Hockey\nteam 3\nfile T3.DB 9999\nxx");
    CHECK(ob_season_take_pack(&M[0].s, ROOT "/bad.obt", ROOT "/y.obt") == -1, "pack: a damaged file is refused");
    write_text(M[2].league, "LEAGUE.ID", "Other 1");
    ob_season_pack(&M[2].s, 3, ROOT "/other.obt");
    CHECK(ob_season_take_pack(&M[0].s, ROOT "/other.obt", ROOT "/y.obt") == -1 && strstr(M[0].s.why, "Other") != NULL,
          "pack: another league is refused");
    for (k = 0; k < NM; k++) { ob_season_free(&M[k].s); ob_files_free(&M[k].f); }
    printf("pick: a player picks a free team in the room; pack: a team as a file there and back, once\n");
}

int main(void)
{
    wipe();
    test_vdrive();
    test_files();
    wipe();
    test_season();
    wipe();
    test_pick_and_pack();
    wipe();
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("season, files and drives ok\n");
    return 0;
}
