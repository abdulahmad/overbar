/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The select-sides screen (local, and a host with two guests whose messages go through a queue
 * here), the hotkeys filter, chat, and the menu lane (pause and menus in the step). The room glue over real sockets is in test_net. */
#include "overbar/sides.h"
#include "overbar/chat.h"
#include "overbar/lane.h"
#include "overbar/save.h"
#include "mdrt/menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); } } while (0)

/* ---- local ---- */

static int applied;
static ob_sides_seat last[OB_SIDES_MAX_SEATS];
static int nlast;
static void apply(void *ud, const ob_sides_seat *seat, int n)
{
    (void)ud;
    applied++;
    nlast = n;
    memcpy(last, seat, (size_t)n * sizeof *seat);
}

static ob_sides_game game2(void)
{
    ob_sides_game g;
    memset(&g, 0, sizeof g);
    g.teams = 2;
    g.seats = 2;
    g.team_name[0] = "Away";
    g.team_name[1] = "Home";
    g.apply = apply;
    return g;
}

static void local(void)
{
    ob_sides s;
    ob_sides_game g = game2();
    ob_sides_seat seat[OB_SIDES_MAX_SEATS];
    int8_t owner[8];
    applied = 0;
    ob_sides_init(&s, &g, 0, NULL, NULL);
    CHECK(s.host && !s.online, "not online: this machine decides");
    CHECK(ob_sides_add(&s, 0, "Keyboard") == 0 && ob_sides_add(&s, 1, "Pad 1") == 1 && ob_sides_add(&s, 2, "Pad 2") == 2, "three devices");
    ob_sides_open(&s);
    CHECK(s.open && ob_sides_seats(&s, seat, 8) == 0, "all in the middle");
    CHECK(ob_sides_input(&s, 0, MDRT_MENU_BACK) == 0, "BACK in the middle: the port may leave");
    ob_sides_input(&s, 1, MDRT_MENU_RIGHT);            /* Pad 1 home */
    ob_sides_input(&s, 0, MDRT_MENU_LEFT);             /* Keyboard away */
    ob_sides_input(&s, 2, MDRT_MENU_LEFT);             /* Pad 2: the seats are full */
    CHECK(s.d[2].team == OB_SIDES_MIDDLE, "a third can't sit with two seats");
    CHECK(ob_sides_seats(&s, seat, 8) == 2 && seat[0].team == 0 && seat[0].local == 0 && seat[1].team == 1 && seat[1].local == 1,
          "the table: away first");
    ob_sides_input(&s, 1, MDRT_MENU_LEFT);             /* back to the middle */
    CHECK(s.d[1].team == OB_SIDES_MIDDLE, "home, left: the middle");
    ob_sides_input(&s, 1, MDRT_MENU_LEFT);
    ob_sides_input(&s, 0, MDRT_MENU_OK);
    CHECK(s.d[0].ready && !ob_sides_all_ready(&s), "one ready of two");
    ob_sides_input(&s, 1, MDRT_MENU_OK);
    ob_sides_input(&s, 2, MDRT_MENU_RIGHT);            /* Pad 2: home is full too? no: two seats taken */
    CHECK(s.d[2].team == OB_SIDES_MIDDLE, "no seat left for the third");
    CHECK(ob_sides_all_ready(&s) && !applied, "everyone ready, not started");
    ob_sides_input(&s, 2, MDRT_MENU_OK);               /* any device of this machine starts it */
    CHECK(applied == 1 && s.done && !s.open && nlast == 2, "started: the game's hook has the table");
    CHECK(last[0].team == 0 && last[1].team == 0, "both away (both moved left)");
    ob_sides_owners(&s, owner, 8, 2);
    CHECK(owner[0] == 0 && owner[3] == 0 && owner[4] == -1, "pads: two a seat");
    ob_sides_open(&s);
    CHECK(!s.done && s.d[0].team == OB_SIDES_MIDDLE && !s.d[0].ready, "open again: all in the middle");
    ob_sides_input(&s, 0, MDRT_MENU_OK);
    CHECK(applied == 2 && nlast == 0, "nobody seated: the computer plays itself");
    /* per_team */
    g.seats = 4; g.per_team = 2;
    ob_sides_init(&s, &g, 0, NULL, NULL);
    ob_sides_add(&s, 0, "a"); ob_sides_add(&s, 1, "b"); ob_sides_add(&s, 2, "c");
    ob_sides_open(&s);
    ob_sides_input(&s, 0, MDRT_MENU_RIGHT); ob_sides_input(&s, 1, MDRT_MENU_RIGHT); ob_sides_input(&s, 2, MDRT_MENU_RIGHT);
    CHECK(s.d[2].team == OB_SIDES_MIDDLE && s.d[1].team == 1, "two a team at most");
    ob_sides_input(&s, 1, MDRT_MENU_OK);
    ob_sides_input(&s, 1, MDRT_MENU_BACK);
    CHECK(!s.d[1].ready && s.d[1].team == 1, "BACK: not ready");
    ob_sides_input(&s, 1, MDRT_MENU_BACK);
    CHECK(s.d[1].team == OB_SIDES_MIDDLE, "BACK again: the middle");
    ob_sides_remove(&s, 0);
    CHECK(s.n == 2 && ob_sides_find(&s, 0, 0) < 0, "a device unplugged");
}

/* ---- online: a host and two guests; messages queued and delivered in order ---- */

typedef struct qmsg { int from, to; size_t n; uint8_t b[600]; } qmsg;
static qmsg Q[256];
static int qn;
static ob_sides S[3];
static int gapplied[3];
static int sent_by[3];

static void q_send(void *ud, int to, const void *data, size_t size)
{
    int me = (int)(size_t)ud, k;
    sent_by[me]++;
    for (k = 0; k < 3; k++) {
        if (k == me || (to >= 0 && to != k)) continue;
        if (qn >= 256 || size > sizeof Q[0].b) { printf("queue full\n"); return; }
        Q[qn].from = me; Q[qn].to = k; Q[qn].n = size;
        memcpy(Q[qn].b, data, size);
        qn++;
    }
}

static void deliver(void)
{
    int i;
    for (i = 0; i < qn; i++) ob_sides_message(&S[Q[i].to], Q[i].from, Q[i].b, Q[i].n);   /* (may queue more) */
    qn = 0;
}

static void gapply(void *ud, const ob_sides_seat *seat, int n) { (void)seat; (void)n; gapplied[(int)(size_t)ud]++; }

static void online(void)
{
    ob_sides_game g = game2();
    ob_sides_seat a[8], b[8];
    int k, na, nb;
    int8_t owner[8];
    ob_sides_hotkey keys[2] = { { 0x10000u, 0 }, { 0x20000u, 1 } };
    for (k = 0; k < 3; k++) {
        g.apply = gapply;
        g.ud = (void *)(size_t)k;
        ob_sides_init(&S[k], &g, k, q_send, (void *)(size_t)k);
    }
    CHECK(S[0].host && !S[1].host && S[1].online, "the host decides");
    ob_sides_add(&S[0], 0, "Ann's keys");
    ob_sides_add(&S[1], 0, "Ben's pad");
    ob_sides_add(&S[2], 3, "Cal's pad");
    ob_sides_open(&S[0]);
    deliver(); deliver();
    for (k = 0; k < 3; k++) CHECK(S[k].n == 3 && S[k].open, "every machine sees three devices, open");
    CHECK(ob_sides_find(&S[2], 1, 0) == 1 && !strcmp(S[2].d[1].name, "Ben's pad"), "a guest sees another's device");
    /* a guest's machine that comes late gets its own again (machine_came from the host) */
    ob_sides_machine_came(&S[1], 0);
    deliver(); deliver();
    CHECK(S[0].n == 3, "said again: not twice");
    ob_sides_input(&S[1], 0, MDRT_MENU_RIGHT);
    ob_sides_input(&S[0], 0, MDRT_MENU_LEFT);
    CHECK(S[1].d[1].team == OB_SIDES_MIDDLE, "a guest's own model waits for the host");
    deliver(); deliver();
    CHECK(S[1].d[1].team == 1 && S[2].d[0].team == 0, "the host's picks reach every machine");
    ob_sides_input(&S[1], 0, MDRT_MENU_OK);
    ob_sides_input(&S[0], 0, MDRT_MENU_OK);
    deliver(); deliver();
    CHECK(ob_sides_all_ready(&S[2]), "everyone ready (Cal watches)");
    ob_sides_input(&S[2], 3, MDRT_MENU_OK);            /* a guest can't start it */
    ob_sides_input(&S[1], 0, MDRT_MENU_OK);
    deliver(); deliver();
    CHECK(!S[0].done && !gapplied[0], "only the host starts the game");
    ob_sides_input(&S[0], 0, MDRT_MENU_OK);
    deliver(); deliver();
    for (k = 0; k < 3; k++) CHECK(S[k].done && gapplied[k] == 1, "started on every machine, the hook called once");
    na = ob_sides_seats(&S[0], a, 8);
    nb = ob_sides_seats(&S[2], b, 8);
    CHECK(na == 2 && nb == 2 && a[0].owner == 0 && a[1].owner == 1 && b[1].owner == 1 && b[1].local == 0, "the same table everywhere");
    ob_sides_owners(&S[2], owner, 8, 1);
    CHECK(owner[0] == 0 && owner[1] == 1 && owner[2] == -1, "pad owners for ob_net_match");
    CHECK(ob_sides_seat_of(&S[1], 1, 0) == 1 && ob_sides_seat_of(&S[1], 2, 3) == -1, "seat of a device");
    CHECK(ob_sides_teams_of(&S[1], 0) == 1 && ob_sides_teams_of(&S[1], 1) == 2 && ob_sides_teams_of(&S[1], 2) == 0, "teams of a machine");
    CHECK(ob_sides_hotkeys_filter(0x30001u, keys, 2, ob_sides_teams_of(&S[1], 1)) == 0x20001u, "Ben's away line keys cleared");
    CHECK(ob_sides_hotkeys_filter(0x30001u, keys, 2, ob_sides_teams_of(&S[1], 2)) == 0x00001u, "a watcher changes nobody's lines");
    CHECK(ob_sides_hotkeys_filter(0x30001u, keys, 2, 3) == 0x30001u, "both teams: all kept");
    /* Ben leaves: his device goes everywhere */
    ob_sides_open(&S[0]);
    ob_sides_machine_gone(&S[0], 1);
    deliver();
    CHECK(S[2].n == 2 && ob_sides_find(&S[2], 1, 0) < 0, "a machine gone: its devices too");
    /* bad messages */
    {
        uint8_t junk[3] = { 4, 1, 1 };
        CHECK(!ob_sides_message(&S[2], 0, junk, sizeof junk) && S[2].n == 2, "a short state is dropped");
        CHECK(!ob_sides_message(&S[2], 1, junk, sizeof junk), "a state from a guest is dropped");
        CHECK(!ob_sides_message(&S[0], 0, junk, sizeof junk), "the host takes nothing from itself");
    }
    {
        uint32_t px[320 * 200];
        int lit = 0, i;
        memset(px, 0, sizeof px);
        ob_sides_draw(&S[2], px, 320, 200, 320);
        for (i = 0; i < 320 * 200; i++) lit += px[i] != 0;
        CHECK(lit > 1000, "the screen draws");
    }
}

/* ---- chat ---- */

static uint8_t chat_out[256];
static size_t chat_outn;
static int chat_sends;
static void chat_send(void *ud, const void *d, size_t n) { (void)ud; chat_sends++; memcpy(chat_out, d, n); chat_outn = n; }

static void type_keys(ob_chat *c, const char *s)
{
    for (; *s; s++) {
        int sc = 0, sh = 0;
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') sc = 4 + ch - 'a';
        else if (ch >= 'A' && ch <= 'Z') { sc = 4 + ch - 'A'; sh = 1; }
        else if (ch == ' ') sc = 44;
        else if (ch == '!') { sc = 30; sh = 1; }
        else if (ch == '?') { sc = 56; sh = 1; }
        if (sh) ob_chat_key(c, 225, 1);
        ob_chat_key(c, sc, 1);
        ob_chat_key(c, sc, 0);
        if (sh) ob_chat_key(c, 225, 0);
    }
}

static void chat(void)
{
    ob_chat a, b, off;
    const ob_chat_line *l;
    int i;
    ob_chat_init(&a, "Ann", chat_send, NULL);
    ob_chat_init(&b, "Ben", chat_send, NULL);
    ob_chat_init(&off, "Solo", NULL, NULL);
    CHECK(!ob_chat_key(&off, 23, 1), "not online: T is the game's");
    CHECK(!ob_chat_key(&a, 22, 1) && !a.typing, "S is the game's");
    CHECK(ob_chat_key(&a, 23, 1) && a.typing, "T opens the line");
    CHECK(ob_chat_key(&a, 23, 0), "its release is chat's too");
    type_keys(&a, "Hi Ben!x");
    ob_chat_key(&a, 42, 1);                            /* backspace */
    CHECK(!strcmp(a.edit, "Hi Ben!"), "typed with shift");
    ob_chat_key(&a, 40, 1);
    CHECK(!a.typing && chat_sends == 1 && a.count == 1, "Enter sends and closes");
    CHECK(ob_chat_message(&b, chat_out, chat_outn), "a line comes");
    l = ob_chat_get(&b, 0);
    CHECK(l && !strcmp(l->who, "Ann") && !strcmp(l->text, "Hi Ben!"), "who and what");
    ob_chat_key(&b, 23, 1);
    ob_chat_key(&b, 40, 1);
    CHECK(chat_sends == 1 && !b.typing, "an empty line is not sent");
    ob_chat_key(&b, 23, 1);
    type_keys(&b, "nope");
    ob_chat_key(&b, 41, 1);
    CHECK(chat_sends == 1 && !b.typing && !b.edit[0], "Escape drops it");
    CHECK(!ob_chat_message(&b, "x", 1), "a line without its 0 is dropped");
    for (i = 0; i < 40; i++) ob_chat_add(&b, "", "news");
    CHECK(b.count == 41 && ob_chat_get(&b, OB_CHAT_LINES) == NULL && ob_chat_get(&b, OB_CHAT_LINES - 1), "the oldest go");
    ob_chat_key(&b, 23, 1);
    for (i = 0; i < 200; i++) ob_chat_key(&b, 4, 1);
    CHECK(b.len == OB_CHAT_TEXT, "the line stops at its length");
    {
        uint32_t px[320 * 200];
        int lit = 0;
        memset(px, 0, sizeof px);
        ob_chat_draw(&b, px, 320, 200, 320);
        for (i = 0; i < 320 * 200; i++) lit += px[i] != 0;
        CHECK(lit > 1000, "the open line draws");
        ob_chat_close(&b);
        memset(px, 0, sizeof px);
        ob_chat_draw(&b, px, 320, 200, 320);
        lit = 0;
        for (i = 0; i < 320 * 200; i++) lit += px[i] != 0;
        CHECK(lit > 100, "toasts draw");
        for (i = 0; i < OB_CHAT_TOAST_FRAMES; i++) ob_chat_tick(&b);
        memset(px, 0, sizeof px);
        ob_chat_draw(&b, px, 320, 200, 320);
        lit = 0;
        for (i = 0; i < 320 * 200; i++) lit += px[i] != 0;
        CHECK(lit == 0 && !ob_chat_shows(&b), "toasts go");
    }
}

/* ---- the menu lane ---- */

static void lane(void)
{
    ob_lane_in a, b;
    ob_lane_sim s, t;
    ob_lane_word w;
    uint16_t all[OB_NET_MAX_PADS], wa[2], wb[2], again[2];
    int8_t owner[OB_NET_MAX_PADS];
    int f, i;
    uint32_t step;
    memset(owner, -1, sizeof owner);
    owner[0] = 0; owner[1] = 1;
    ob_lane_owners(owner, 5);                          /* machines 0 and 2 */
    CHECK(owner[0] == 0 && owner[8] == 0 && owner[9] == 0 && owner[10] == -1 && owner[12] == 2 && owner[13] == 2 && owner[15] == -1,
          "the lane's pads: two a machine past the seats");
    ob_lane_in_init(&a);
    ob_lane_in_mouse(&a, 639, 479, 3);
    ob_lane_in_key(&a, 0x1C, 1);
    ob_lane_in_pack(&a, 0, wa);
    ob_lane_unpack(wa, &w);
    CHECK(w.key == 0x1C && w.down && w.key_flip && !w.pause_flip && w.x == 639 && w.y == 479 && w.buttons == 3, "a word packs and unpacks");
    ob_lane_in_key(&a, 0x1C, 0);
    ob_lane_in_pack(&a, 0, again);
    CHECK(again[0] == wa[0] && again[1] == wa[1] && a.n == 1, "a step packed again (a replay): the same words, nothing lost");
    ob_lane_in_mouse(&a, 5000, -3, 1);
    ob_lane_in_pack(&a, 1, wa);
    ob_lane_unpack(wa, &w);
    CHECK(w.key == 0x1C && !w.down && !w.key_flip && w.x == 1023 && w.y == 0 && w.buttons == 1, "the release; the mouse kept in range");

    /* two machines: b pauses, types, a's pause is dropped, b resumes; two sims see the same */
    ob_lane_in_init(&a);
    ob_lane_in_init(&b);
    ob_lane_sim_init(&s);
    ob_lane_sim_init(&t);
    for (step = 0; step < 60; step++) {
        int fs, ft, keys = 0;
        if (step == 10) ob_lane_in_pause(&b);
        if (step == 12) { ob_lane_in_key(&b, 0x48, 1); ob_lane_in_key(&b, 0x48, 0); ob_lane_in_key(&a, 0x50, 1); }
        if (step == 14) ob_lane_in_pause(&a);
        if (step == 20) ob_lane_in_pause(&b);
        if (step == 30) ob_lane_in_pause(&a);
        if (step == 40) ob_lane_in_key(&a, 0x01, 1);
        memset(all, 0, sizeof all);
        ob_lane_in_pack(&a, step, wa);
        ob_lane_in_pack(&b, step, wb);
        memcpy(all + 8, wa, sizeof wa);
        memcpy(all + 10, wb, sizeof wb);
        fs = ob_lane_step(&s, all, 3, step);
        ft = ob_lane_step(&t, all, 3, step);
        CHECK(fs == ft && !memcmp(&s, &t, sizeof s), "every machine's lane alike");
        if (step == 10) CHECK((fs & OB_LANE_PAUSED) && s.holder == 1 && s.paused_at == 11, "b paused at its step");
        if (step == 12 || step == 13) { CHECK((fs & OB_LANE_KEY) && s.key == 0x48 && s.down == (step == 12), "b's keys reach the game"); keys++; }
        if (step == 14) CHECK(s.holder == 1 && !(fs & OB_LANE_KEY), "a's pause and key are dropped while b holds it");
        if (step == 20) CHECK((fs & OB_LANE_RESUMED) && s.holder == -1, "b resumes");
        if (step == 30) CHECK((fs & OB_LANE_PAUSED) && s.holder == 0, "then a pauses");
        if (step == 40) {
            CHECK((fs & OB_LANE_KEY) && s.key == 0x01, "a's key");
            ob_lane_resume(&s); ob_lane_resume(&t);    /* the game's own Resume */
        }
        if (step > 40) CHECK(s.holder == -1 && !fs, "resumed by the game; nothing after");
        (void)keys;
    }
    /* a repeated guess (rollback) of the last words does nothing */
    ob_lane_sim_init(&s);
    ob_lane_in_init(&b);
    ob_lane_in_pause(&b);
    ob_lane_in_pack(&b, 0, wb);
    memset(all, 0, sizeof all);
    memcpy(all + 10, wb, sizeof wb);
    for (f = 0, i = 0; i < 5; i++) f |= ob_lane_step(&s, all, 2, (uint32_t)i) & OB_LANE_RESUMED;
    CHECK(s.holder == 1 && !f, "the same words again: no second pause");
    CHECK(ob_lane_holder_may(&s, 2, 1) && !ob_lane_holder_may(&s, 2, 0), "the holder edits only its team's lines");
}

/* ---- match saves ---- */

static uint32_t fake_at;
static size_t fake_share(void *ud, uint32_t *step, void *out, size_t cap)
{
    static const char st[] = "a shared state";
    (void)ud;
    *step = fake_at;
    if (out && cap >= sizeof st) memcpy(out, st, sizeof st);
    return sizeof st;
}

static int count_files(const char *dir)
{
    ob_save_entry e[32];
    return ob_saves_list(dir, "game X", e, 32);
}

static void saves(void)
{
    const char *dir = "build_test_saves";
    ob_saves s;
    ob_save_summary m;
    ob_net_match match;
    ob_save_entry e[32];
    ob_save_file f;
    ob_net_match back;
    char line[200];
    int i, n;
    memset(&match, 0, sizeof match);
    memset(match.owner, -1, sizeof match.owner);
    match.owner[0] = 0; match.owner[1] = 1; match.delay = 2;
    match.settings = "set-up"; match.settings_size = 7;
    memset(&m, 0, sizeof m);
    snprintf(m.league, sizeof m.league, "Abdul's league");
    snprintf(m.game, sizeof m.game, "Season game 23");
    snprintf(m.team[0], sizeof m.team[0], "Montreal"); snprintf(m.team[1], sizeof m.team[1], "Boston");
    snprintf(m.players[0], sizeof m.players[0], "Ann"); snprintf(m.players[1], sizeof m.players[1], "Ben");
    m.period = 1; m.clock = 20 * 60;
    CHECK(!ob_saves_begin(&s, dir, "game X", &match, 0x1234), "saves begin");
    ob_saves_done(&s);                              /* (a clean folder from an earlier run) */
    ob_saves_event(&s, OB_SAVE_PERIOD, 0, &m);
    ob_saves_event(&s, OB_SAVE_FACEOFF, 5, &m);
    ob_saves_event(&s, OB_SAVE_FACEOFF, 5, &m);     /* a replay of the same step */
    CHECK(s.npend == 2, "a point is queued once");
    fake_at = 3;
    CHECK(ob_saves_poll_with(&s, 4, fake_share, NULL) == 1 && s.npend == 1, "only points whose buttons are known; the faceoff waits for a state");
    fake_at = 7;
    CHECK(ob_saves_poll_with(&s, 8, fake_share, NULL) == 1 && s.npend == 0, "then the faceoff");
    ob_saves_clock(&s, 50, 119, &m);
    CHECK(s.npend == 0, "no clock save before 2 minutes");
    m.clock = 18 * 60; m.score[1] = 1;
    ob_saves_clock(&s, 60, 120, &m);
    ob_saves_clock(&s, 61, 121, &m);
    CHECK(s.npend == 1, "a clock save at 2 minutes, once");
    for (i = 0; i < 5; i++) ob_saves_event(&s, OB_SAVE_FACEOFF, 100 + (uint32_t)i * 50, &m);
    fake_at = 400;
    ob_saves_poll_with(&s, 400, fake_share, NULL);
    n = count_files(dir);
    CHECK(n == 1 + 3 + 1, "the period's, the last 3 faceoffs, the clock's");
    m.period = 2; m.clock = 20 * 60;
    ob_saves_event(&s, OB_SAVE_PERIOD, 500, &m);
    fake_at = 501;
    ob_saves_poll_with(&s, 501, fake_share, NULL);
    n = ob_saves_list(dir, "game X", e, 32);
    CHECK(n == 6 && e[0].kind == OB_SAVE_PERIOD && e[0].step == 501 && e[0].sum.period == 2, "newest first");
    CHECK(ob_saves_list(dir, "game Y", e + 10, 10) == 0, "another game's saves are not listed");
    ob_save_line(&e[0], line, sizeof line);
    CHECK(strstr(line, "2nd period 20:00") && strstr(line, "Montreal 0 - 1 Boston") && strstr(line, "(Ann vs Ben)") && strstr(line, "Abdul's league, Season game 23"),
          "the Recover list's line");
    CHECK(!ob_save_load(e[0].path, "game X", &f) && f.state_size == 15 && !strcmp((char *)f.state, "a shared state") && f.delay == 2
          && f.owner[1] == 1 && f.owner[2] == -1 && f.settings_size == 7, "a save reads back");
    ob_save_match(&f, &back);
    CHECK(back.state_step == 501 && back.state == f.state && back.settings_size == 7 && back.owner[0] == 0, "the match to begin from it");
    ob_save_file_free(&f);
    CHECK(ob_save_load(e[0].path, "game Y", &f) < 0, "another game's save is refused");
    ob_saves_done(&s);
    CHECK(count_files(dir) == 0, "the result is in: the match's saves go");
    ob_saves_free(&s);
    remove(dir);
}

int main(void)
{
    local();
    online();
    chat();
    lane();
    saves();
    if (fails) { printf("%d sides/chat check(s) failed\n", fails); return 1; }
    printf("sides, chat, lane and saves ok\n");
    return 0;
}
