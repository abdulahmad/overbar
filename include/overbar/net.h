/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Lockstep netplay: two to four machines play the same deterministic game, each step run with
 * every pad's buttons known. Nothing is saved or loaded: each machine sends its own pads' buttons
 * for step k + delay while it runs step k, and a step runs only once every machine's buttons for
 * it have come (else the client waits a moment and asks again). Steps 0 .. delay-1 have no buttons
 * (all zero) on every machine, so all start the same.
 *
 * The machines are a star: the host and up to three guests, each guest talking to the host alone;
 * the host passes every guest's buttons on to the others. Each packet carries every step of
 * buttons the other has not acknowledged (up to 64), so a lost one costs nothing but a moment.
 * The settings come from the Online page (ob_online). UDP over IPv4 or IPv6, Winsock on Windows
 * (link ws2_32). No threads; nothing blocks.
 *
 * Adapted from PulseMan's rollback netplay (its handshake, identity check, names, settings in
 * acknowledged chunks, star of machines, "both connect", desync check, goodbye). */
#ifndef OVERBAR_NET_H
#define OVERBAR_NET_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/overbar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_NET_PROTOCOL 1               /* the messages' version: every machine must have the same */
#define OB_NET_MAX_MACHINES 4           /* the host and three guests */
#define OB_NET_MAX_PADS 8
#define OB_NET_MAX_DELAY 30             /* steps */
#define OB_NET_MAX_SETTINGS (16u << 20) /* bytes of the host's settings */
#define OB_NET_TIMEOUT_MS 10000         /* a machine not heard from for this long ends the session */

enum { OB_NET_CONNECTING, OB_NET_GO, OB_NET_ENDED };

typedef struct ob_net ob_net;

typedef struct ob_net_game {
    const char *identity;       /* must match on every machine (build, game data, sim settings); up to 255 bytes */
    int pads;                   /* pads the game has (2..OB_NET_MAX_PADS); each machine plays o->here of them,
                                   the host's first, then the guests' in join order */
    const void *settings;       /* the host's, sent to the guests before step 0 (a guest's are not used) */
    size_t settings_size;       /* (0: none) */
} ob_net_game;

/* Opens the socket and starts connecting (host: o->port; join: o->addr[:port], o->port when no
 * port is given; both connect: o->port here and the other's o->addr[:port]). The host takes
 * o->delay and o->all; every machine its o->here and o->name. NULL on failure, with why in err. */
ob_net *ob_net_new(const ob_online *o, const ob_net_game *g, char *err, int err_size);
void ob_net_free(ob_net *n);            /* says goodbye first if the session is on */

/* OB_NET_CONNECTING, OB_NET_GO (everyone is in and has the settings: run step 0) or OB_NET_ENDED
 * (ob_net_why) */
int ob_net_state(ob_net *n);
/* pumps the socket (sends, resends, receives); call it often, also while connecting and while a
 * step waits. Returns the state. */
int ob_net_poll(ob_net *n);
int ob_net_is_host(ob_net *n);
const void *ob_net_settings(ob_net *n, size_t *size);   /* the host's, on every machine once GO (NULL: none) */
unsigned ob_net_my_pads(ob_net *n);     /* the pads this machine plays (bit p: pad p), once GO */
unsigned ob_net_all_pads(ob_net *n);    /* every pad someone plays, once GO */
int ob_net_delay(ob_net *n);            /* the host's input delay, in steps (once the host answered) */
int ob_net_machines(ob_net *n);         /* machines in the session, once GO */
const char *ob_net_name_of(ob_net *n, int machine);   /* the host 0, then the guests by join order */

/* Runs the next step. local: this machine's pads' buttons, in the order of ob_net_my_pads' bits.
 * 1: all[] (OB_NET_MAX_PADS entries, pad by pad; pads nobody plays 0) holds the buttons of step
 * ob_net_step_count() - 1, which the client now runs. 0: not yet (call again soon; the local given
 * first for this step is the one used). -1: the session ended. */
int ob_net_step(ob_net *n, const uint16_t *local, uint16_t *all);
uint32_t ob_net_step_count(ob_net *n);  /* steps run so far */

/* every so often (every 60 steps, say): the simulation's hash after running step `step`. The host
 * compares the guests' with its own; a difference ends the session as "desync at step N". */
void ob_net_check(ob_net *n, uint32_t step, uint64_t sim_hash);

void ob_net_quit(ob_net *n);            /* goodbye to all (they end at once, told who left); state ENDED */
const char *ob_net_why(ob_net *n);      /* why it ended ("" while it goes on) */
/* one line for the Online page: "34 ms, delay 2, waiting 0.3%" while playing (waiting: the steps
 * that had to wait for someone's buttons), what it waits for while connecting, or why it ended */
void ob_net_status(ob_net *n, char *line, int size);
/* this machine's address on its own network ("192.168.1.20"; "" none); fits ob_online_ops.lan_address */
const char *ob_net_lan_address(char *buf, int size);

/* a bad network, for tests and trying the delay: every datagram this machine sends delayed by
 * delay_ms plus up to jitter_ms, loss_pct per cent lost and dup_pct per cent sent twice */
void ob_net_simulate(ob_net *n, int delay_ms, int jitter_ms, int loss_pct, int dup_pct);

#ifdef __cplusplus
}
#endif

#endif
