/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The lockstep core (overbar/net.h): machines in this process over 127.0.0.1, each stepping with
 * its own random buttons; every machine must see the same buttons for every step, each machine's
 * own where it put them (delay steps on). Also: packet loss, both connect, a refused identity and
 * version, a desync, and a goodbye. And rollback: a toy game whose state is a hash of every
 * button it was run with, played with lag and loss; every final step must agree. Rooms: messages
 * (a large one, guest to guest), two matches with other seats, a watcher that joins a match as it
 * goes on, a player who leaves mid-match; with and without loss. OB_NET_TEST_ROOMS=1: only those. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif
#include "overbar/overbar.h"
#include "overbar/room.h"
#include "mdrt/menu.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); } } while (0)

#define MAXSTEPS 1200
#define BASE_PORT 47310

static void nap(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec t = { 0, 1000000 };
    nanosleep(&t, NULL);
#endif
}

static double seconds(void)
{
#ifdef _WIN32
    return GetTickCount() / 1000.0;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
#endif
}

typedef struct mach {
    ob_net *n;
    uint32_t rng;
    int steps, have_local, nlocal;
    uint16_t local[OB_NET_MAX_PADS];
    uint16_t gave[MAXSTEPS][OB_NET_MAX_PADS];   /* its own buttons, by the step they were given at */
    uint16_t saw[MAXSTEPS][OB_NET_MAX_PADS];    /* every pad's, by step */
} mach;

static mach M[4];

static ob_net *make(int role, int port, const char *addr, int here, int all, int delay, const char *id, const char *name,
                    const void *set, size_t setn, int pads)
{
    ob_online o;
    ob_net_game g;
    char err[200];
    ob_net *n;
    memset(&o, 0, sizeof o);
    memset(&g, 0, sizeof g);
    snprintf(o.name, sizeof o.name, "%s", name);
    o.role = role;
    snprintf(o.addr, sizeof o.addr, "%s", addr ? addr : "");
    o.port = port;
    o.delay = delay;
    o.here = here;
    o.all = all;
    g.identity = id;
    g.pads = pads;
    g.settings = set;
    g.settings_size = setn;
    n = ob_net_new(&o, &g, err, sizeof err);
    if (!n) printf("ob_net_new: %s\n", err);
    return n;
}

static void reset(int k)
{
    int i;
    for (i = 0; i < k; i++) {
        ob_net *n = M[i].n;
        memset(&M[i], 0, sizeof M[i]);
        M[i].n = n;
        M[i].rng = 0x9E3779B9u * (uint32_t)(i + 1);
    }
}

static void free_all(int k)
{
    int i;
    for (i = 0; i < k; i++) { ob_net_free(M[i].n); M[i].n = NULL; }
}

/* polls every machine until all are GO (1) or one ends or time runs out (0) */
static int connect_all(int k, double secs)
{
    double until = seconds() + secs;
    while (seconds() < until) {
        int i, go = 0;
        for (i = 0; i < k; i++) {
            int s = ob_net_poll(M[i].n);
            if (s == OB_NET_ENDED) { printf("ended while connecting: %s\n", ob_net_why(M[i].n)); return 0; }
            go += s == OB_NET_GO;
        }
        if (go == k) return 1;
        nap();
    }
    printf("connecting timed out\n");
    return 0;
}

static uint32_t rnd(mach *m) { m->rng ^= m->rng << 13; m->rng ^= m->rng >> 17; m->rng ^= m->rng << 5; return m->rng; }

/* every machine steps to `steps` with random buttons; 1 when all got there */
static int run(int k, int steps, double secs)
{
    double until = seconds() + secs;
    int i;
    for (;;) {
        int done = 0, moved = 0;
        for (i = 0; i < k; i++) {
            mach *m = &M[i];
            int r, p;
            if (m->steps >= steps) { ob_net_poll(m->n); done++; continue; }
            if (!m->have_local) {
                unsigned pads = ob_net_my_pads(m->n);
                int s = m->steps + ob_net_delay(m->n);
                m->nlocal = 0;
                for (p = 0; p < OB_NET_MAX_PADS; p++)
                    if (pads >> p & 1) {
                        m->local[m->nlocal++] = (uint16_t)rnd(m);
                        if (s < MAXSTEPS) m->gave[s][p] = m->local[m->nlocal - 1];
                    }
                m->have_local = 1;
            }
            r = ob_net_step(m->n, m->local, m->saw[m->steps]);
            if (r < 0) { printf("machine %d ended at step %d: %s\n", i, m->steps, ob_net_why(m->n)); return 0; }
            if (r == 1) {
                CHECK(ob_net_step_count(m->n) == (uint32_t)m->steps + 1, "the step count follows");
                m->steps++;
                m->have_local = 0;
                moved = 1;
            }
        }
        if (done == k) return 1;
        if (seconds() > until) { printf("stepping timed out\n"); return 0; }
        if (!moved) nap();
    }
}

/* every machine saw the same buttons; each one's own, delay steps after it gave them; the first
 * delay steps all zero */
static void compare(int k, int steps, const char *what)
{
    int i, s, p, bad = 0, delay = ob_net_delay(M[0].n);
    unsigned all = ob_net_all_pads(M[0].n);
    for (i = 1; i < k; i++)
        if (memcmp(M[i].saw, M[0].saw, (size_t)steps * sizeof M[0].saw[0])) bad++;
    for (s = 0; s < steps; s++)
        for (i = 0; i < k; i++) {
            unsigned mine = ob_net_my_pads(M[i].n);
            for (p = 0; p < OB_NET_MAX_PADS; p++) {
                uint16_t want;
                if (s < delay) want = 0;
                else if (mine >> p & 1) want = M[i].gave[s][p];
                else if (!(all >> p & 1)) want = 0;
                else continue;
                if (M[i].saw[s][p] != want) bad++;
            }
        }
    if (bad) printf("%s: %d mismatches\n", what, bad);
    CHECK(!bad, what);
}

static void two_machines(void)
{
    static uint8_t set[5000];
    const void *got;
    size_t n, i;
    char line[96];
    for (i = 0; i < sizeof set; i++) set[i] = (uint8_t)(i * 7 + 3);
    M[0].n = make(0, BASE_PORT, NULL, 1, 2, 2, "test 1", "Ann", set, sizeof set, 2);
    M[1].n = make(1, BASE_PORT, "127.0.0.1", 1, 2, 9, "test 1", "Bob", NULL, 0, 2);
    CHECK(M[0].n && M[1].n, "two machines open");
    if (!M[0].n || !M[1].n) { free_all(2); return; }
    reset(2);
    CHECK(connect_all(2, 5), "two machines connect");
    CHECK(ob_net_is_host(M[0].n) && !ob_net_is_host(M[1].n), "the host hosts");
    CHECK(ob_net_my_pads(M[0].n) == 1 && ob_net_my_pads(M[1].n) == 2, "host pad 1, guest pad 2");
    CHECK(ob_net_delay(M[1].n) == 2, "the host's delay");
    got = ob_net_settings(M[1].n, &n);
    CHECK(got && n == sizeof set && !memcmp(got, set, n), "the guest has the host's settings");
    CHECK(!strcmp(ob_net_name_of(M[1].n, 0), "Ann") && !strcmp(ob_net_name_of(M[0].n, 1), "Bob"), "names exchanged");
    CHECK(run(2, 600, 20), "two machines step");
    compare(2, 600, "two machines agree");
    ob_net_status(M[1].n, line, sizeof line);
    CHECK(strstr(line, " ms, delay 2, waiting ") != NULL, "status line");
    printf("two machines: %s\n", line);
    free_all(2);
}

static void three_machines(int loss)
{
    static uint8_t set[20000];
    size_t i, n;
    const void *got;
    int port = BASE_PORT + (loss ? 20 : 10);
    char addr[32], line[96];
    snprintf(addr, sizeof addr, "127.0.0.1:%d", port);
    for (i = 0; i < sizeof set; i++) set[i] = (uint8_t)(i * 13 + 1);
    /* five pads, as the SNES: the host one, the first guest two, the second one (4 in all) */
    M[0].n = make(0, port, NULL, 1, 4, 3, "test 3", "Hal", set, sizeof set, 5);
    M[1].n = make(1, 0, addr, 2, 2, 0, "test 3", "Ida", NULL, 0, 5);
    if (M[1].n && loss) ob_net_simulate(M[1].n, 2, 6, loss, 5);
    if (M[0].n && loss) ob_net_simulate(M[0].n, 2, 6, loss, 5);
    reset(3);
    {   /* the first guest in before the second, for the pads' order */
        double until = seconds() + 3;
        char st[96] = "";
        while (seconds() < until && M[0].n && !strstr(st, "3 of 4")) { ob_net_poll(M[0].n); ob_net_poll(M[1].n); ob_net_status(M[0].n, st, sizeof st); nap(); }
    }
    M[2].n = make(1, 0, addr, 1, 2, 0, "test 3", "Joe", NULL, 0, 5);
    if (M[2].n && loss) ob_net_simulate(M[2].n, 2, 6, loss, 5);
    CHECK(M[0].n && M[1].n && M[2].n, "three machines open");
    if (!M[0].n || !M[1].n || !M[2].n) { free_all(3); return; }
    CHECK(connect_all(3, 10), loss ? "three machines connect through loss" : "three machines connect");
    CHECK(ob_net_machines(M[2].n) == 3, "three machines");
    CHECK(ob_net_my_pads(M[0].n) == 1 && ob_net_my_pads(M[1].n) == 6 && ob_net_my_pads(M[2].n) == 8, "pads by join order");
    got = ob_net_settings(M[2].n, &n);
    CHECK(got && n == sizeof set && !memcmp(got, set, n), "the settings through");
    CHECK(run(3, loss ? 400 : 600, 30), loss ? "three machines step through loss" : "three machines step");
    compare(3, loss ? 400 : 600, loss ? "three machines agree through loss" : "three machines agree");
    ob_net_status(M[0].n, line, sizeof line);
    printf("three machines%s: %s\n", loss ? " (20% loss, jitter, duplicates)" : "", line);
    free_all(3);
}

static void both_connect(void)
{
    char a[32], b[32];
    snprintf(a, sizeof a, "127.0.0.1:%d", BASE_PORT + 31);
    snprintf(b, sizeof b, "127.0.0.1:%d", BASE_PORT + 30);
    M[0].n = make(2, BASE_PORT + 30, a, 1, 2, 1, "test both", "Kim", NULL, 0, 2);
    M[1].n = make(2, BASE_PORT + 31, b, 1, 2, 1, "test both", "Lee", NULL, 0, 2);
    reset(2);
    CHECK(M[0].n && M[1].n, "both open");
    if (!M[0].n || !M[1].n) { free_all(2); return; }
    CHECK(connect_all(2, 5), "both connect");
    CHECK(ob_net_is_host(M[0].n) + ob_net_is_host(M[1].n) == 1, "one of the two hosts");
    CHECK(run(2, 200, 10), "both step");
    compare(2, 200, "both agree");
    free_all(2);
}

static void refused_identity(void)
{
    double until = seconds() + 5;
    M[0].n = make(0, BASE_PORT + 40, NULL, 1, 2, 2, "game A", "Host", NULL, 0, 2);
    M[1].n = make(1, BASE_PORT + 40, "127.0.0.1", 1, 2, 2, "game B", "Guest", NULL, 0, 2);
    CHECK(M[0].n && M[1].n, "mismatch open");
    if (!M[0].n || !M[1].n) { free_all(2); return; }
    while (seconds() < until && ob_net_poll(M[1].n) != OB_NET_ENDED) { ob_net_poll(M[0].n); nap(); }
    CHECK(ob_net_state(M[1].n) == OB_NET_ENDED && strstr(ob_net_why(M[1].n), "another game"), "another identity refused");
    CHECK(ob_net_state(M[0].n) == OB_NET_CONNECTING, "the host waits on");
    printf("identity: %s\n", ob_net_why(M[1].n));
    free_all(2);
}

/* a HELLO of protocol 99, by hand: the host answers REFUSE (protocol-independent layout) */
static void refused_version(void)
{
    ob_net *h = make(0, BASE_PORT + 50, NULL, 1, 2, 2, "game", "Host", NULL, 0, 2);
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nb = 1;
#else
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    struct sockaddr_in to;
    unsigned char hello[64], reply[300];
    int n = 0, got = 0, i;
    double until = seconds() + 3;
    CHECK(h != NULL, "version host open");
    if (!h) return;
#ifdef _WIN32
    ioctlsocket(s, FIONBIO, &nb);
#endif
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(BASE_PORT + 50);
    to.sin_addr.s_addr = htonl(0x7F000001u);
    hello[n++] = 'O'; hello[n++] = 'B'; hello[n++] = 99; hello[n++] = 1;   /* version 99, HELLO */
    hello[n++] = 1;                                                       /* role: join */
    for (i = 0; i < 8; i++) hello[n++] = (unsigned char)(0x11 * (i + 1)); /* nonce */
    while (seconds() < until && got <= 0) {
        sendto(s, (const char *)hello, n, 0, (struct sockaddr *)&to, sizeof to);
        ob_net_poll(h);
        nap();
#ifdef _WIN32
        got = recv(s, (char *)reply, sizeof reply - 1, 0);
#else
        got = (int)recv(s, reply, sizeof reply - 1, MSG_DONTWAIT);
#endif
    }
    CHECK(got > 13 && reply[3] == 3 && !memcmp(reply + 4, hello + 5, 8), "a REFUSE for the nonce");
    if (got > 13) {
        int len = reply[12];
        reply[13 + len < got ? 13 + len : got] = 0;
        CHECK(strstr((char *)reply + 13, "another version") != NULL, "another version refused");
        printf("version: %s\n", (char *)reply + 13);
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    ob_net_free(h);
}

static void desync(void)
{
    double until;
    M[0].n = make(0, BASE_PORT + 60, NULL, 1, 2, 2, "game", "Host", NULL, 0, 2);
    M[1].n = make(1, BASE_PORT + 60, "127.0.0.1", 1, 2, 2, "game", "Guest", NULL, 0, 2);
    reset(2);
    if (!M[0].n || !M[1].n || !connect_all(2, 5)) { CHECK(0, "desync connect"); free_all(2); return; }
    CHECK(run(2, 100, 10), "desync steps");
    ob_net_check(M[0].n, 60, 0x1234);
    ob_net_check(M[1].n, 60, 0x1234);
    CHECK(run(2, 130, 10), "an agreeing check passes");
    ob_net_check(M[0].n, 120, 0xAAAA);
    ob_net_check(M[1].n, 120, 0xBBBB);
    until = seconds() + 5;
    while (seconds() < until) {
        int a = ob_net_poll(M[0].n), b = ob_net_poll(M[1].n);   /* (both, every time) */
        if (a == OB_NET_ENDED && b == OB_NET_ENDED) break;
        nap();
    }
    CHECK(!strcmp(ob_net_why(M[0].n), "desync at step 120"), "the host sees the desync");
    CHECK(!strcmp(ob_net_why(M[1].n), "desync at step 120"), "the guest is told the desync");
    printf("desync: %s / %s\n", ob_net_why(M[0].n), ob_net_why(M[1].n));
    free_all(2);
}

static void goodbye(void)
{
    double until;
    char addr[32];
    snprintf(addr, sizeof addr, "127.0.0.1:%d", BASE_PORT + 70);
    M[0].n = make(0, BASE_PORT + 70, NULL, 1, 3, 1, "game", "Host", NULL, 0, 4);
    M[1].n = make(1, 0, addr, 1, 2, 1, "game", "Amy", NULL, 0, 4);
    M[2].n = make(1, 0, addr, 1, 2, 1, "game", "Bea", NULL, 0, 4);
    reset(3);
    if (!M[0].n || !M[1].n || !M[2].n || !connect_all(3, 5)) { CHECK(0, "goodbye connect"); free_all(3); return; }
    CHECK(run(3, 50, 10), "goodbye steps");
    ob_net_quit(M[2].n);
    CHECK(ob_net_state(M[2].n) == OB_NET_ENDED && !strcmp(ob_net_why(M[2].n), "you left"), "the one who quits ends");
    until = seconds() + 5;
    while (seconds() < until) {
        int a = ob_net_poll(M[0].n), b = ob_net_poll(M[1].n);   /* (both, every time) */
        if (a == OB_NET_ENDED && b == OB_NET_ENDED) break;
        nap();
    }
    CHECK(!strcmp(ob_net_why(M[0].n), "Bea left"), "the host sees who left");
    CHECK(!strcmp(ob_net_why(M[1].n), "Bea left"), "the other guest is told who left");
    CHECK(ob_net_step(M[1].n, NULL, M[1].saw[0]) == -1, "a step after the end says so");
    printf("goodbye: %s / %s\n", ob_net_why(M[0].n), ob_net_why(M[1].n));
    free_all(3);
}

/* ---- rollback ---- */

#define RB_STEPS 600
typedef struct toy {
    uint64_t h;                         /* the game: every button it ran with, hashed */
    uint64_t final[RB_STEPS + 200];     /* its hash after each final step */
    uint16_t btn[RB_STEPS + 200][OB_NET_MAX_PADS];
    int finals, restarts, replays;
    uint32_t high;                      /* the most steps run: new steps go at 120 a second, replays at once */
    uint32_t saves, loads;
} toy;
static toy T[3];
static int rb_resumable = 1;      /* 0: the game never at a resumable point (it plays in lockstep) */

static size_t toy_save(void *user, void *out, size_t cap)
{
    toy *t = user;
    t->saves++;
    if (cap >= sizeof t->h) memcpy(out, &t->h, sizeof t->h);
    return sizeof t->h;
}
static int toy_load(void *user, const void *in, size_t size)
{
    toy *t = user;
    if (size != sizeof t->h) return -1;
    memcpy(&t->h, in, size);
    t->loads++;
    return 0;
}
/* machine i's buttons for step s: held for a while, then another */
static uint16_t toy_buttons(int i, int pad, uint32_t s)
{
    uint32_t x = (uint32_t)(i * 7919 + pad * 104729) ^ (s / (uint32_t)(5 + 3 * i + pad)) * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (uint16_t)(x & 0x0FFF);
}

static ob_net *make_rb(int role, int port, const char *addr, int here, int all, int delay, const char *name, toy *t, int window)
{
    ob_online o;
    ob_net_game g;
    ob_net_states st;
    char err[200];
    ob_net *n;
    memset(&o, 0, sizeof o);
    memset(&g, 0, sizeof g);
    snprintf(o.name, sizeof o.name, "%s", name);
    o.role = role;
    snprintf(o.addr, sizeof o.addr, "%s", addr ? addr : "");
    o.port = port; o.delay = delay; o.here = here; o.all = all;
    st.user = t; st.save = toy_save; st.load = toy_load;
    g.identity = "toy"; g.pads = 4; g.states = &st; g.rollback = window;
    n = ob_net_new(&o, &g, err, sizeof err);
    if (!n) printf("ob_net_new: %s\n", err);
    return n;
}

static int rb_run(int k, double secs)
{
    double start = seconds(), until = start + secs;
    for (;;) {
        int i, done = 0, moved = 0;
        for (i = 0; i < k; i++) {
            ob_net *n = M[i].n;
            toy *t = &T[i];
            uint32_t cur = ob_net_step_count(n);
            uint16_t local[OB_NET_MAX_PADS], all[OB_NET_MAX_PADS];
            unsigned pads = ob_net_my_pads(n);
            int p, j = 0, r;
            if (t->finals >= RB_STEPS && cur > RB_STEPS) { ob_net_poll(n); done++; continue; }
            if (cur >= t->high && cur > (uint32_t)((seconds() - start) * 120)) { ob_net_poll(n); continue; }
            for (p = 0; p < OB_NET_MAX_PADS; p++)
                if (pads >> p & 1) local[j++] = toy_buttons(i, p, cur + (uint32_t)ob_net_delay(n));
            r = ob_net_frame(n, rb_resumable, local, all);
            if (r < 0) { printf("machine %d ended at step %u: %s\n", i, (unsigned)cur, ob_net_why(n)); return 0; }
            if (r == OB_NET_RESTART) { t->restarts++; moved = 1; continue; }
            if (r != OB_NET_RUN) continue;
            moved = 1;
            if (cur + 1 > t->high) t->high = cur + 1;
            for (p = 0; p < OB_NET_MAX_PADS; p++) t->h = (t->h ^ all[p]) * 0x100000001B3ull + cur;
            t->replays += ob_net_replaying(n);
            if (cur < RB_STEPS + 200) { t->final[cur] = t->h; memcpy(t->btn[cur], all, sizeof all); }   /* (a replay's replaces) */
            if (cur % 60 == 59) ob_net_check(n, cur, t->h);
            if (ob_net_confirmed(n) > RB_STEPS + 1) t->finals = RB_STEPS;   /* (every step up to here as it stays) */
        }
        if (done == k) return 1;
        if (seconds() > until) { printf("rollback stepping timed out\n"); return 0; }
        if (!moved) nap();
    }
}

static void rollback(int lag_ms, int loss)
{
    char addr[32], line[120];
    int port = BASE_PORT + 80 + lag_ms / 10 + loss, i, s, p, bad = 0;
    snprintf(addr, sizeof addr, "127.0.0.1:%d", port);
    memset(T, 0, sizeof T);
    M[0].n = make_rb(0, port, NULL, 1, 3, 1, "Rob", &T[0], 0);
    M[1].n = make_rb(1, 0, addr, 1, 3, 0, "Sue", &T[1], 0);
    M[2].n = make_rb(1, 0, addr, 1, 3, 0, "Tim", &T[2], 0);
    CHECK(M[0].n && M[1].n && M[2].n, "rollback machines open");
    if (!M[0].n || !M[1].n || !M[2].n) { free_all(3); return; }
    for (i = 0; i < 3; i++) ob_net_simulate(M[i].n, lag_ms / 2, lag_ms / 4, loss, 2);
    reset(3);
    if (!connect_all(3, 10)) { CHECK(0, "rollback connect"); free_all(3); return; }
    CHECK(rb_run(3, 40), "rollback machines step");
    for (i = 0; i < 3; i++) {
        CHECK(T[i].finals >= RB_STEPS, "every step confirmed");
    }
    for (i = 1; i < 3; i++)
        for (s = 0; s < RB_STEPS; s++) if (T[i].final[s] != T[0].final[s]) { bad++; break; }
    for (s = 1; s < RB_STEPS; s++)       /* each machine's own buttons as it gave them (delay 1) */
        for (i = 0; i < 3; i++)
            for (p = 0; p < 3; p++)
                if (T[0].btn[s][p] != toy_buttons((int)(ob_net_my_pads(M[0].n) >> p & 1 ? 0 : ob_net_my_pads(M[1].n) >> p & 1 ? 1 : 2), p, (uint32_t)s)) { bad++; i = 3; s = RB_STEPS; break; }
    CHECK(!bad, "rollback machines agree on every final step");
    if (rb_resumable) CHECK(T[1].loads > 0 && T[1].restarts > 0 && T[1].replays > 0, "guesses went wrong and were played again");
    else CHECK(!T[0].loads && !T[1].loads && !T[2].loads && !T[1].saves, "no states without a resumable point");
    CHECK(ob_net_state(M[0].n) == OB_NET_GO, "no desync");
    ob_net_status(M[1].n, line, sizeof line);
    CHECK(strstr(line, "rollback") != NULL, "rollback status line");
    printf("rollback (%d ms, %d%% loss): %s; %d rollbacks, %d steps replayed, %u states saved\n", lag_ms, loss, line,
           T[1].restarts, T[1].replays, (unsigned)T[1].saves);
    free_all(3);
}

/* ---- rooms ---- */

#define RM_MAX 1000
typedef struct rtoy {
    uint64_t h;
    uint64_t final[RM_MAX];
    int playing, over, joined, restarts;
    uint32_t first;                     /* the first step it ran (a machine that joined late: the state's) */
    uint32_t high;
    double start;
} rtoy;
static rtoy R[3];

static size_t rtoy_save(void *user, void *out, size_t cap)
{
    rtoy *t = user;
    if (cap >= sizeof t->h) memcpy(out, &t->h, sizeof t->h);
    return sizeof t->h;
}
static int rtoy_load(void *user, const void *in, size_t size)
{
    rtoy *t = user;
    if (size != sizeof t->h) return -1;
    memcpy(&t->h, in, size);
    return 0;
}
static size_t rtoy_share(void *user, const void *state, size_t size, void *out, size_t cap)
{
    (void)user;
    if (cap >= size) memcpy(out, state, size);
    return size;
}
static int rtoy_take(void *user, const void *in, size_t size) { ((rtoy *)user)->joined = 1; return rtoy_load(user, in, size); }

static ob_net *make_room(int role, int port, const char *addr, int here, const char *name, rtoy *t)
{
    ob_online o;
    ob_net_game g;
    ob_net_states st;
    char err[200];
    ob_net *n;
    static const char set[] = "the room's settings";
    memset(&o, 0, sizeof o);
    memset(&g, 0, sizeof g);
    snprintf(o.name, sizeof o.name, "%s", name);
    o.role = role;
    snprintf(o.addr, sizeof o.addr, "%s", addr ? addr : "");
    o.port = port; o.delay = 1; o.here = here; o.all = 2;
    st.user = t; st.save = rtoy_save; st.load = rtoy_load; st.share = rtoy_share; st.take = rtoy_take;
    g.identity = "room toy"; g.pads = 4; g.states = &st; g.room = 1;
    g.settings = set; g.settings_size = sizeof set;
    n = ob_net_new(&o, &g, err, sizeof err);
    if (!n) printf("ob_net_new: %s\n", err);
    return n;
}

static void poll_all(int k) { int i; for (i = 0; i < k; i++) if (M[i].n) ob_net_poll(M[i].n); }

/* polls until every machine of the k is in the lobby (1) */
static int lobby_all(int k, double secs)
{
    double until = seconds() + secs;
    while (seconds() < until) {
        int i, in = 0;
        for (i = 0; i < k; i++) {
            int s = ob_net_poll(M[i].n);
            if (s == OB_NET_ENDED) { printf("machine %d ended: %s\n", i, ob_net_why(M[i].n)); return 0; }
            in += s == OB_NET_LOBBY;
        }
        if (in == k) return 1;
        nap();
    }
    printf("lobby timed out\n");
    return 0;
}

/* one machine of a room's match: a step when it may */
static int room_tick(int i, uint32_t end)
{
    ob_net *n = M[i].n;
    rtoy *t = &R[i];
    uint32_t cur;
    uint16_t local[OB_NET_MAX_PADS], all[OB_NET_MAX_PADS];
    unsigned pads;
    int p, j = 0, r;
    if (!n) return 0;
    if (!t->playing) {
        if (ob_net_poll(n) != OB_NET_GO || t->over) return 0;
        t->playing = 1;
        t->h = 0;
        t->first = ob_net_step_count(n);
        t->high = t->first;
        t->start = seconds();
    }
    cur = ob_net_step_count(n);
    if (cur >= end && ob_net_confirmed(n) >= end) { ob_net_match_end(n); t->playing = 0; t->over = 1; return 1; }
    if (cur >= t->high && cur - t->first > (uint32_t)((seconds() - t->start) * 120)) { ob_net_poll(n); return 0; }
    pads = ob_net_my_pads(n);
    for (p = 0; p < OB_NET_MAX_PADS; p++)
        if (pads >> p & 1) local[j++] = toy_buttons(i, p, cur + (uint32_t)ob_net_delay(n));
    r = ob_net_frame(n, 1, local, all);
    if (r < 0) { printf("machine %d ended at step %u: %s\n", i, (unsigned)cur, ob_net_why(n)); t->playing = 0; t->over = 1; return -1; }
    if (r == OB_NET_OVER) { t->playing = 0; t->over = 1; return 1; }
    if (r == OB_NET_RESTART) { t->restarts++; if (t->joined == 1) { t->joined = 2; t->first = t->high = ob_net_step_count(n); } return 1; }
    if (r != OB_NET_RUN) return 0;
    if (cur + 1 > t->high) t->high = cur + 1;
    for (p = 0; p < OB_NET_MAX_PADS; p++) t->h = (t->h ^ all[p]) * 0x100000001B3ull + cur;
    if (cur < RM_MAX) t->final[cur] = t->h;
    if (cur % 60 == 59) ob_net_check(n, cur, t->h);
    return 1;
}

static void room_reset(void) { memset(R, 0, sizeof R); }

/* every machine's final hashes the host's, from the first step it ran to end */
static int room_agree(int k, uint32_t end)
{
    int i, bad = 0;
    uint32_t s;
    for (i = 1; i < k; i++) {
        if (!M[i].n) continue;
        for (s = R[i].first; s < end; s++) if (R[i].final[s] != R[0].final[s]) { printf("machine %d differs at step %u (first %u)\n", i, (unsigned)s, (unsigned)R[i].first); bad++; break; }
    }
    return !bad;
}

static void rooms(int loss)
{
    char addr[32], err[120], line[120];
    int port = BASE_PORT + 120 + loss, i, from, type, got, owner_ok;
    size_t size;
    static uint8_t big[300000], back[300000];
    ob_net_match m;
    double until;
    snprintf(addr, sizeof addr, "127.0.0.1:%d", port);
    room_reset();
    for (i = 0; i < (int)sizeof big; i++) big[i] = (uint8_t)(i * 31 + 7);
    M[0].n = make_room(0, port, NULL, 1, "Ann", &R[0]);
    M[1].n = make_room(1, 0, addr, 1, "Ben", &R[1]);
    M[2].n = NULL;
    CHECK(M[0].n && M[1].n, "room machines open");
    if (!M[0].n || !M[1].n) { free_all(2); return; }
    if (loss) { ob_net_simulate(M[0].n, 3, 6, loss, 2); ob_net_simulate(M[1].n, 3, 6, loss, 2); }
    CHECK(ob_net_state(M[0].n) == OB_NET_LOBBY, "the host's room is open at once");
    if (!lobby_all(2, 10)) { CHECK(0, "the guest comes into the room"); free_all(2); return; }
    CHECK(ob_net_me(M[1].n) == 1 && ob_net_members(M[1].n) == 3 && ob_net_members(M[0].n) == 3, "two machines in the room");
    CHECK(!strcmp(ob_net_name_of(M[1].n, 0), "Ann") && !strcmp(ob_net_name_of(M[0].n, 1), "Ben"), "room names");
    {
        const void *rs = ob_net_settings(M[1].n, &size);
        CHECK(rs && size == sizeof "the room's settings" && !strcmp(rs, "the room's settings"), "the room's settings");
    }

    /* messages: a small one to the host, a large one from it to all */
    CHECK(!ob_net_send(M[1].n, 0, 5, "hello", 5), "a guest sends");
    CHECK(!ob_net_send(M[0].n, OB_NET_ALL, 7, big, sizeof big), "the host sends a large message");
    CHECK(ob_net_send(M[0].n, 0, 7, "x", 1) < 0 && ob_net_send(M[0].n, 1, 250, "x", 1) < 0, "no message to itself or of the core's types");
    until = seconds() + 20;
    got = 0;
    while (seconds() < until && got < 2) {
        char small[16];
        poll_all(2);
        if (ob_net_recv(M[0].n, &from, &type, small, sizeof small, &size) == 1) { CHECK(from == 1 && type == 5 && size == 5 && !memcmp(small, "hello", 5), "the host has the guest's"); got++; }
        if (ob_net_recv(M[1].n, &from, &type, back, sizeof back, &size) == 1) { CHECK(from == 0 && type == 7 && size == sizeof big && !memcmp(back, big, size), "the guest has the large one"); got++; }
        nap();
    }
    CHECK(got == 2, "both messages through");

    /* match 1: Ann pad 1, Ben pad 2; Cal comes in at step 120 to watch */
    memset(&m, 0, sizeof m);
    for (i = 0; i < OB_NET_MAX_PADS; i++) m.owner[i] = -1;
    m.owner[0] = 0; m.owner[1] = 1; m.delay = -1;
    m.settings = "match one"; m.settings_size = 10;
    CHECK(!ob_net_match_begin(M[0].n, &m, err, sizeof err), "match 1 begins");
    CHECK(ob_net_match_begin(M[0].n, &m, err, sizeof err) < 0, "not two at once");
    until = seconds() + 60;
    while (seconds() < until && !(R[0].over && R[1].over && (M[2].n && R[2].over))) {
        int moved = 0;
        if (!M[2].n && ob_net_step_count(M[0].n) >= 120) {
            M[2].n = make_room(1, 0, addr, 0, "Cal", &R[2]);
            if (M[2].n && loss) ob_net_simulate(M[2].n, 3, 6, loss, 2);
        }
        for (i = 0; i < 3; i++) { int r = room_tick(i, 900); if (r < 0) until = 0; moved |= r > 0; }
        if (!moved) nap();
    }
    CHECK(R[0].over && R[1].over && M[2].n && R[2].over, "match 1 played to its end on all three");
    if (!M[2].n) { printf("steps %u %u\n", (unsigned)ob_net_step_count(M[0].n), (unsigned)ob_net_step_count(M[1].n)); free_all(3); return; }
    {
        const void *ms = ob_net_settings(M[1].n, &size);
        CHECK(ms && size == 10 && !strcmp(ms, "match one"), "the match's settings");
    }
    CHECK(R[2].joined && R[2].first > 0 && R[2].first < 900, "the watcher joined as the match went on");
    CHECK(room_agree(3, 900), "match 1: every machine's steps agree");
    CHECK(ob_net_my_pads(M[2].n) == 0 && ob_net_here_of(M[0].n, 2) == 0, "the watcher plays no pad");
    CHECK(lobby_all(3, 10), "back in the lobby after match 1");
    printf("room match 1%s: watcher joined at step %u\n", loss ? " (loss)" : "", (unsigned)R[2].first);

    /* a message from one guest to the other, through the host */
    CHECK(!ob_net_send(M[2].n, 1, 9, "from Cal", 8), "guest to guest");
    until = seconds() + 10;
    got = 0;
    while (seconds() < until && !got) {
        char small[16];
        poll_all(3);
        if (ob_net_recv(M[1].n, &from, &type, small, sizeof small, &size) == 1)
            got = from == 2 && type == 9 && size == 8 && !memcmp(small, "from Cal", 8);
        nap();
    }
    CHECK(got, "guest to guest arrives");
    CHECK(ob_net_recv(M[0].n, &from, &type, back, sizeof back, &size) == 0, "the host does not keep a message for a guest");

    /* match 2: Ben pad 1, Cal pad 2, the host watches */
    room_reset();
    for (i = 0; i < OB_NET_MAX_PADS; i++) m.owner[i] = -1;
    m.owner[0] = 1; m.owner[1] = 2;
    m.settings = "match two"; m.settings_size = 10;
    CHECK(!ob_net_match_begin(M[0].n, &m, err, sizeof err), "match 2 begins");
    until = seconds() + 60;
    while (seconds() < until && !(R[0].over && R[1].over && R[2].over)) {
        int moved = 0;
        for (i = 0; i < 3; i++) { int r = room_tick(i, 300); if (r < 0) until = 0; moved |= r > 0; }
        if (!moved) nap();
    }
    CHECK(R[0].over && R[1].over && R[2].over, "match 2 played to its end");
    owner_ok = ob_net_my_pads(M[0].n) == 0 && ob_net_my_pads(M[1].n) == 1 && ob_net_my_pads(M[2].n) == 2;
    CHECK(owner_ok, "match 2's seats");
    CHECK(room_agree(3, 300), "match 2: every machine's steps agree");
    CHECK(ob_net_match_id(M[2].n) == 2, "two matches in one room");
    CHECK(lobby_all(3, 10), "back in the lobby after match 2");

    /* match 3: Ben leaves at step 60; the match ends for the others, the room stays */
    room_reset();
    m.owner[0] = 0; m.owner[1] = 1;
    CHECK(!ob_net_match_begin(M[0].n, &m, err, sizeof err), "match 3 begins");
    until = seconds() + 30;
    while (seconds() < until && !(R[0].over && R[2].over)) {
        int moved = 0;
        if (M[1].n && R[1].playing && ob_net_step_count(M[1].n) >= 60) { ob_net_free(M[1].n); M[1].n = NULL; }
        for (i = 0; i < 3; i++) { int r = M[i].n ? room_tick(i, 400) : 0; moved |= r > 0; }
        if (!moved) nap();
    }
    CHECK(R[0].over && R[2].over && ob_net_step_count(M[0].n) < 400, "a player leaving ends the match");
    CHECK(strstr(ob_net_match_why(M[0].n), "left") && strstr(ob_net_match_why(M[2].n), "left"), "the others told who left");
    CHECK(lobby_all(1, 5) && ob_net_state(M[2].n) == OB_NET_LOBBY, "the room stays");
    until = seconds() + 5;
    while (seconds() < until && ob_net_members(M[2].n) != 5) { ob_net_poll(M[0].n); ob_net_poll(M[2].n); nap(); }
    CHECK(ob_net_members(M[0].n) == 5 && ob_net_members(M[2].n) == 5, "who is in, after one left");
    ob_net_status(M[2].n, line, sizeof line);
    printf("room%s: %s; match 3: %s\n", loss ? " (loss)" : "", line, ob_net_match_why(M[2].n));
    free_all(3);
}


/* the room's glue (overbar/room.h) over real sockets: a guest's device said before it is in, picks,
 * the host starting, the seat table as a match's owners, chat, and who left */
static ob_sides GS[3];
static ob_chat GC[3];
static ob_room GR[3];
static int glue_game_msgs;
static void glue_game(void *ud, int from, int type, const void *d, size_t n) { (void)ud; (void)from; (void)d; (void)n; if (type == 7) glue_game_msgs++; }

static void glue_poll(int k) { int i; for (i = 0; i < k; i++) if (M[i].n) { ob_net_poll(M[i].n); ob_room_poll(&GR[i], glue_game, NULL); } }

static int glue_until(int k, int (*ok)(void), double secs)
{
    double until = seconds() + secs;
    while (seconds() < until) { glue_poll(k); if (ok()) return 1; nap(); }
    return 0;
}

static int g_three(void) { return GS[0].n == 3 && GS[1].n == 3 && GS[2].n == 3; }
static int g_picked(void) { int b = ob_sides_find(&GS[2], 1, 0); return GS[2].n == 3 && b >= 0 && GS[2].d[b].ready && GS[2].d[0].ready; }
static int g_done(void) { return GS[0].done && GS[1].done && GS[2].done; }
static int g_chat(void) { return GC[0].count >= 1 && GC[2].count >= 1 && glue_game_msgs == 1; }
static int g_left(void) { return GS[0].n == 2 && GS[1].n == 2 && GC[1].count >= 2; }

static void room_glue(void)
{
    char addr[32], err[120], line[OB_ROOM_LINES][OB_ROOM_LINE + 1];
    int port = BASE_PORT + 140, i, nl;
    ob_sides_game g;
    ob_net_match m;
    snprintf(addr, sizeof addr, "127.0.0.1:%d", port);
    room_reset();
    glue_game_msgs = 0;
    M[0].n = make_room(0, port, NULL, 1, "Ann", &R[0]);
    M[1].n = make_room(1, 0, addr, 1, "Ben", &R[1]);
    M[2].n = NULL;
    CHECK(M[0].n && M[1].n, "glue: machines open");
    if (!M[0].n || !M[1].n || !lobby_all(2, 10)) { CHECK(0, "glue: Ben in the room"); free_all(3); return; }
    M[2].n = make_room(1, 0, addr, 1, "Cal", &R[2]);       /* after Ben, so the numbers are known: Ben 1, Cal 2 */
    if (!M[2].n) { CHECK(0, "glue: Cal's machine opens"); free_all(3); return; }
    memset(&g, 0, sizeof g);
    g.teams = 2; g.seats = 2;
    for (i = 0; i < 3; i++) {
        ob_chat_init(&GC[i], i == 0 ? "Ann" : i == 1 ? "Ben" : "Cal", ob_room_chat_send, M[i].n);
    }
    if (!lobby_all(3, 10)) { CHECK(0, "glue: all in the room"); free_all(3); return; }
    for (i = 0; i < 3; i++) {
        /* (a guest's number is known once it is in: the port makes the screen then) */
        ob_sides_init(&GS[i], &g, ob_net_me(M[i].n), ob_room_sides_send, M[i].n);
        ob_room_init(&GR[i], M[i].n, &GS[i], &GC[i]);
        ob_sides_add(&GS[i], 0, i == 0 ? "Ann's keys" : i == 1 ? "Ben's pad" : "Cal's pad");
    }
    ob_sides_open(&GS[0]);
    CHECK(glue_until(3, g_three, 10), "glue: every machine sees the three devices");
    ob_sides_input(&GS[0], 0, MDRT_MENU_LEFT);
    CHECK(ob_net_me(M[1].n) == 1 && ob_net_me(M[2].n) == 2, "glue: Ben 1, Cal 2");
    ob_sides_input(&GS[1], 0, MDRT_MENU_RIGHT);
    glue_poll(3);
    ob_sides_input(&GS[0], 0, MDRT_MENU_OK);
    ob_sides_input(&GS[1], 0, MDRT_MENU_OK);
    CHECK(glue_until(3, g_picked, 10), "glue: picks reach the host and back");
    ob_sides_input(&GS[0], 0, MDRT_MENU_OK);
    CHECK(glue_until(3, g_done, 10), "glue: the host starts it on every machine");
    memset(&m, 0, sizeof m);
    ob_sides_owners(&GS[0], m.owner, OB_NET_MAX_PADS, 1);
    m.delay = -1;
    CHECK(m.owner[0] == 0 && m.owner[1] == 1 && m.owner[2] == -1, "glue: the table's owners");
    CHECK(ob_net_match_begin(M[0].n, &m, err, sizeof err) == 0, "glue: the match begins with them");
    ob_chat_key(&GC[1], 23, 1); ob_chat_key(&GC[1], 11, 1); ob_chat_key(&GC[1], 12, 1); ob_chat_key(&GC[1], 40, 1);  /* "hi" */
    ob_net_send(M[1].n, 0, 7, "game", 4);
    CHECK(glue_until(3, g_chat, 10), "glue: chat on every machine, the game's own message to the game");
    CHECK(!strcmp(ob_chat_get(&GC[2], 0)->who, "Ben") && !strcmp(ob_chat_get(&GC[2], 0)->text, "hi"), "glue: Ben said hi");
    nl = ob_room_lines(&GR[0], line, OB_ROOM_LINES);
    CHECK(nl >= 7 && strstr(line[1], "Ann") && strstr(line[3], "Cal") && strstr(line[5], "Ann's keys") && strstr(line[6], "Ben's pad"),
          "glue: the room page's lines");
    if (nl < 7 || !strstr(line[6], "Ben")) for (i = 0; i < nl; i++) printf("  %s\n", line[i]);
    ob_net_free(M[2].n);
    M[2].n = NULL;
    CHECK(glue_until(2, g_left, 10), "glue: Cal left: his device goes, chat says so");
    CHECK(!strcmp(ob_chat_get(&GC[1], 0)->text, "Cal left"), "glue: news line");
    free_all(3);
}

int main(void)
{
    char lan[64];
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    if (getenv("OB_NET_TEST_ROOMS")) {    /* (only the rooms) */
        rooms(0);
        rooms(5);
        room_glue();
        if (fails) { printf("%d net check(s) failed\n", fails); return 1; }
        printf("room tests ok\n");
        return 0;
    }
    two_machines();
    three_machines(0);
    three_machines(20);
    both_connect();
    refused_identity();
    refused_version();
    desync();
    goodbye();
    rollback(40, 0);
    rollback(80, 10);
    rb_resumable = 0;
    rollback(30, 1);
    rooms(0);
    rooms(5);
    room_glue();
    printf("LAN address: \"%s\"\n", ob_net_lan_address(lan, sizeof lan));
    if (fails) { printf("%d net check(s) failed\n", fails); return 1; }
    printf("net tests ok\n");
    return 0;
}
