/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The part of online play every PC port has the same: the Online page's hooks and its settings
 * file, the session object and its lock, waiting at the start of a game for the other machine, and
 * the main thread's polling. The game's own part (where a session starts and ends, the settings it
 * sends, the steps) stays in the port, which calls this.
 *
 * Threads: the game thread starts and ends sessions and runs the steps; the main thread polls
 * (ob_netgame_main_poll) and runs the Online page. Both take the port's lock around every ob_net
 * call (ob_netgame_lock). One session at a time, per program.
 *
 * Test aid: with cfg.env "NHL96", NHL96_ONLINE=host|join arms a session on 127.0.0.1 (port
 * NHL96_PORT, default 41950 + 0) as the Online page's Host / Join would, and NHL96_NETSIM =
 * "delay,jitter,loss%" makes the link bad. */
#ifndef OVERBAR_NETGAME_H
#define OVERBAR_NETGAME_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/overbar.h"
#include "overbar/net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ob_netgame_cfg {
    const char *title;          /* for the settings file's comment ("NHL Hockey 96 (SDL2 port)") */
    const char *ini_path;       /* the Online page's settings file (NULL: none kept) */
    const char *env;            /* the test variables' prefix (NULL: none) */
    const char *how;            /* what the players do once armed ("both players: start an exhibition game") */
    int pads_per_player;        /* ob_net pads one player sends (1, or 2 for a 32-bit input) */
    int test_port;              /* the test aid's default port */
    /* the port's lock (an SDL mutex, say): taken around every ob_net call of either thread */
    void (*lock)(void);
    void (*unlock)(void);
    int (*trylock)(void);       /* 0: taken */
    void (*sleep_ms)(int ms);
    /* main thread: the Online page shown while a session starts, closed again after (NULL: not done) */
    int (*page_is_open)(void);
    void (*open_page)(void);
    void (*close_page)(void);
} ob_netgame_cfg;

/* the Online page's hooks on h (call before the menu bar is built), the settings file read into
 * h->online, and the test aid armed when its variable is set. cfg is copied. */
void ob_netgame_setup(ob_host *h, const ob_netgame_cfg *cfg);
int ob_netgame_armed(void);             /* Host / Connect was pressed: the next game is online */
int ob_netgame_test(void);              /* the test aid armed it */
void ob_netgame_set_why(const char *why);   /* what the Online page says (why a session did not start) */
/* game thread, where a session's game starts: disarms, opens the session with g and waits (polling
 * here, sleeping between) until every machine is in. The session, or NULL with why set */
ob_net *ob_netgame_begin(const ob_net_game *g);
ob_net *ob_netgame_net(void);           /* the session (NULL: none); use under the lock */
/* game thread: frees the session (why: for the Online page, when the session itself says none) */
void ob_netgame_end(const char *why);
void ob_netgame_lock(void);
void ob_netgame_unlock(void);
extern const char *volatile ob_netgame_where;   /* what the game thread does under the lock (for logs) */
void ob_netgame_main_poll(void);        /* main thread, every frame */

#ifdef __cplusplus
}
#endif
#endif
