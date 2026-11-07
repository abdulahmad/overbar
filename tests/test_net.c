/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The lockstep core (overbar/net.h): machines in this process over 127.0.0.1, each stepping with
 * its own random buttons; every machine must see the same buttons for every step, each machine's
 * own where it put them (delay steps on). Also: packet loss, both connect, a refused identity and
 * version, a desync, and a goodbye. */
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

int main(void)
{
    char lan[64];
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    two_machines();
    three_machines(0);
    three_machines(20);
    both_connect();
    refused_identity();
    refused_version();
    desync();
    goodbye();
    printf("LAN address: \"%s\"\n", ob_net_lan_address(lan, sizeof lan));
    if (fails) { printf("%d net check(s) failed\n", fails); return 1; }
    printf("net tests ok\n");
    return 0;
}
