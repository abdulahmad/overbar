/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Online play's shared part for the PC ports (overbar/netgame.h). Taken from NHL 95 PC's
 * port/netplay.c, where it began. */
#include "overbar/netgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ob_netgame_cfg cfg;
static ob_online nh_cfg;
static ob_net *nh_net;
static volatile int nh_armed, nh_test, nh_want_page, nh_close_page, nh_opened_page;
static char nh_why[200];
const char *volatile ob_netgame_where = "";

void ob_netgame_lock(void) { if (cfg.lock) cfg.lock(); }
void ob_netgame_unlock(void) { if (cfg.unlock) cfg.unlock(); }
int ob_netgame_armed(void) { return nh_armed; }
int ob_netgame_test(void) { return nh_test; }
ob_net *ob_netgame_net(void) { return nh_net; }
void ob_netgame_set_why(const char *why) { snprintf(nh_why, sizeof nh_why, "%s", why ? why : ""); }

static void ini_save(const ob_online *o)
{
    FILE *f;
    if (!cfg.ini_path) return;
    f = fopen(cfg.ini_path, "w");
    if (!f) return;
    fprintf(f, "; %s: the Online page's last settings. role: 0 host, 1 join, 2 both connect\n", cfg.title ? cfg.title : "");
    fprintf(f, "name=%s\nrole=%d\naddress=%s\nport=%d\ndelay=%d\n", o->name, o->role, o->addr, o->port, o->delay);
    fclose(f);
}

static void ini_load(ob_online *o)
{
    char line[256];
    FILE *f;
    if (!cfg.ini_path) return;
    f = fopen(cfg.ini_path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, '='), *e;
        if (!v || line[0] == ';') continue;
        *v++ = 0;
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
        if (!strcmp(line, "name")) snprintf(o->name, sizeof o->name, "%s", v);
        else if (!strcmp(line, "role")) o->role = atoi(v) >= 0 && atoi(v) <= 2 ? atoi(v) : 0;
        else if (!strcmp(line, "address")) snprintf(o->addr, sizeof o->addr, "%s", v);
        else if (!strcmp(line, "port")) o->port = atoi(v) > 0 && atoi(v) < 65536 ? atoi(v) : OB_NET_PORT_DEFAULT;
        else if (!strcmp(line, "delay")) o->delay = atoi(v) < 0 ? 0 : atoi(v) > 10 ? 10 : atoi(v);
    }
    fclose(f);
}

static int online_start(const ob_online *o, char *msg, int msg_size)
{
    int busy;
    ob_netgame_lock();
    busy = nh_net != NULL;
    ob_netgame_unlock();
    if (busy) { snprintf(msg, (size_t)msg_size, "a session is on: Leave it first"); return -1; }
    nh_cfg = *o;
    nh_cfg.here = cfg.pads_per_player;
    nh_cfg.all = 2 * cfg.pads_per_player;
    ini_save(o);
    nh_why[0] = 0;
    nh_armed = 1;
    return 0;
}

static void online_leave(void)
{
    nh_armed = 0;
    ob_netgame_lock();
    if (nh_net) ob_net_quit(nh_net);        /* the game thread sees it end: waiting to start, or at the next step */
    ob_netgame_unlock();
    snprintf(nh_why, sizeof nh_why, "you left");
}

static int online_status(char *line, int size)
{
    int on = 0;
    line[0] = 0;
    ob_netgame_lock();
    if (nh_net) {
        ob_net_status(nh_net, line, size);
        on = ob_net_state(nh_net) != OB_NET_ENDED;
    }
    ob_netgame_unlock();
    if (!nh_net) {
        if (nh_armed) {
            snprintf(line, (size_t)size, "%s%s%s", nh_why, nh_why[0] ? "; " : "", cfg.how ? cfg.how : "");
            on = 1;
        } else snprintf(line, (size_t)size, "%s", nh_why);
    }
    return on;
}

static const char *online_lan(char *buf, int size)
{
    const char *a = ob_net_lan_address(buf, size);
    return a && a[0] ? a : NULL;
}

static const char *env(const char *name)
{
    char v[64];
    if (!cfg.env) return NULL;
    snprintf(v, sizeof v, "%s_%s", cfg.env, name);
    return getenv(v);
}

void ob_netgame_setup(ob_host *h, const ob_netgame_cfg *c)
{
    ob_online_ops ops;
    const char *e;
    cfg = *c;
    if (cfg.pads_per_player < 1) cfg.pads_per_player = 1;
    memset(&ops, 0, sizeof ops);
    ops.max_pads = 2;
    ops.max_delay = 10;
    ops.roles = 3;
    ops.start = online_start;
    ops.leave = online_leave;
    ops.status = online_status;
    ops.lan_address = online_lan;
    if (h) {
        ini_load(&h->online);
        h->online.here = 1;
        h->online.all = 2;
        ob_online_set_ops(h, &ops);
    }
    if ((e = env("ONLINE")) != NULL) {
        memset(&nh_cfg, 0, sizeof nh_cfg);
        snprintf(nh_cfg.name, sizeof nh_cfg.name, "%s", !strcmp(e, "host") ? "Host" : "Guest");
        nh_cfg.role = !strcmp(e, "host") ? 0 : 1;
        snprintf(nh_cfg.addr, sizeof nh_cfg.addr, "127.0.0.1");
        nh_cfg.port = env("PORT") ? atoi(env("PORT")) : cfg.test_port ? cfg.test_port : OB_NET_PORT_DEFAULT;
        nh_cfg.here = cfg.pads_per_player;
        nh_cfg.all = 2 * cfg.pads_per_player;
        nh_armed = 1;
        nh_test = 1;
        fprintf(stderr, "online: armed as %s on port %d\n", e, nh_cfg.port);
    }
}

ob_net *ob_netgame_begin(const ob_net_game *g)
{
    char err[160];
    int st;
    const char *sim;
    if (!nh_armed) return NULL;
    nh_armed = 0;
    ob_netgame_lock();
    nh_net = ob_net_new(&nh_cfg, g, err, sizeof err);
    ob_netgame_unlock();
    if (!nh_net) { snprintf(nh_why, sizeof nh_why, "%s", err); return NULL; }
    if ((sim = env("NETSIM")) != NULL) {
        int d = 0, j = 0, l = 0;
        sscanf(sim, "%d,%d,%d", &d, &j, &l);
        ob_net_simulate(nh_net, d, j, l, 0);
        fprintf(stderr, "online: simulating %d ms delay, %d ms jitter, %d%% loss\n", d, j, l);
    }
    nh_why[0] = 0;
    nh_want_page = 1;                               /* the Online page shows what it waits for */
    for (;;) {
        ob_netgame_lock();
        st = nh_net ? ob_net_poll(nh_net) : OB_NET_ENDED;
        ob_netgame_unlock();
        if (st != OB_NET_CONNECTING) break;
        if (cfg.sleep_ms) cfg.sleep_ms(2);
    }
    nh_close_page = 1;
    if (st != OB_NET_GO) { ob_netgame_end("the session did not start"); return NULL; }
    return nh_net;
}

void ob_netgame_end(const char *why)
{
    if (nh_test) fprintf(stderr, "online: session ends: %s\n", nh_net && ob_net_why(nh_net)[0] ? ob_net_why(nh_net) : why ? why : "");
    ob_netgame_lock();
    if (nh_net) {
        if (ob_net_state(nh_net) == OB_NET_ENDED && ob_net_why(nh_net)[0]) snprintf(nh_why, sizeof nh_why, "%s", ob_net_why(nh_net));
        else if (why) snprintf(nh_why, sizeof nh_why, "%s", why);
        ob_net_free(nh_net);
        nh_net = NULL;
    } else if (why) snprintf(nh_why, sizeof nh_why, "%s", why);
    ob_netgame_unlock();
}

void ob_netgame_main_poll(void)
{
    if (cfg.trylock && cfg.trylock() == 0) {
        if (nh_net) ob_net_poll(nh_net);
        ob_netgame_unlock();
    }
    if (nh_want_page) {
        nh_want_page = 0;
        if (cfg.page_is_open && cfg.open_page && !cfg.page_is_open()) { cfg.open_page(); nh_opened_page = 1; }
    }
    if (nh_close_page) {
        nh_close_page = 0;
        if (nh_opened_page && cfg.page_is_open && cfg.close_page && cfg.page_is_open()) cfg.close_page();
        nh_opened_page = 0;
    }
}
