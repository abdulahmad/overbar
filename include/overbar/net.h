/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Netplay in two modes. A game that can save and load its state at a resumable point gives
 * ob_net_game.states and plays with rollback (ob_net_frame): the other machines' buttons are
 * guessed (each pad's last known, held) so a step never waits for the network, and when a guess
 * turns out wrong the state from before that step is loaded and the steps since are played again,
 * quietly (ob_net_replaying), with the right buttons. A game without states plays in lockstep:
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

#define OB_NET_PROTOCOL 5               /* the messages' version: every machine must have the same (2: each INPUT
                                           carries the sender's step and lead, for rollback's time sync; 3: rooms,
                                           matches and messages; 4: 16 pads; 5: a match
                                           begun from a state, seats held for a player who dropped) */
#define OB_NET_MAX_MACHINES 4           /* the host and three guests */
#define OB_NET_MAX_PADS 16              /* seats' pads first; a room's match can give each machine a menu lane past them (overbar/lane.h) */
#define OB_NET_MAX_DELAY 30             /* steps */
#define OB_NET_MAX_ROLLBACK 30          /* steps a rollback may play again, at most */
#define OB_NET_MAX_SETTINGS (16u << 20) /* bytes of the host's settings */
#define OB_NET_TIMEOUT_MS 10000         /* a machine not heard from for this long ends the session */
#define OB_NET_MAX_MESSAGE (16u << 20)  /* bytes of one message (ob_net_send) */
#define OB_NET_MESSAGE_TYPES 240        /* a game's message types: 0 .. 239 */
/* Overbar's own modules' types (240 .. 249; 250 .. are the core's) */
#define OB_NET_TYPE_SIDES 240           /* the select-sides screen (overbar/sides.h) */
#define OB_NET_TYPE_CHAT 241            /* chat (overbar/chat.h) */
#define OB_NET_TYPE_FILES 242           /* folders sent (overbar/files.h) */
#define OB_NET_TYPE_SEASON 243          /* the online season (overbar/season.h) */
#define OB_NET_TYPE_LAST 249

/* OB_NET_LOBBY: (a room) everyone in it is connected and no match of this machine's goes on */
enum { OB_NET_CONNECTING, OB_NET_GO, OB_NET_ENDED, OB_NET_LOBBY };

typedef struct ob_net ob_net;

/* A game's states, for rollback. save: the simulation as it is now, at a resumable point before
 * the next step, into out (cap bytes); returns its size, writing nothing when that is more than
 * cap (the core makes room and asks again), or 0 when it cannot (then that step is not one a
 * rollback can go back to). load: back to bytes save gave; 0, or -1 (the session ends). A state
 * must hold everything the steps after it depend on (all the game's memory, the sound chip's,
 * timers), and nothing outside the game: loading one must leave the next steps exactly as they
 * were the first time. Neither is called outside ob_net_frame. */
typedef struct ob_net_states {
    void *user;
    size_t (*save)(void *user, void *out, size_t cap);
    int (*load)(void *user, const void *in, size_t size);
    /* (a room; NULL: none) share: a state save gave (state, size), as bytes another machine of the same build
     * can load: size written to out (cap bytes), more than cap writing nothing, 0 when it cannot. take: such
     * bytes loaded, as load does (0, or -1). With them a machine can join a match that goes on */
    size_t (*share)(void *user, const void *state, size_t size, void *out, size_t cap);
    int (*take)(void *user, const void *in, size_t size);
} ob_net_states;

typedef struct ob_net_game {
    const char *identity;       /* must match on every machine (build, game data, sim settings); up to 255 bytes */
    int pads;                   /* pads the game has (2..OB_NET_MAX_PADS); each machine plays o->here of them,
                                   the host's first, then the guests' in join order */
    const void *settings;       /* the host's, sent to the guests before step 0 (a guest's are not used) */
    size_t settings_size;       /* (0: none) */
    const ob_net_states *states;   /* NULL: lockstep; else rollback through ob_net_frame (copied) */
    int rollback;               /* most steps one rollback plays again (0: 12; at most OB_NET_MAX_ROLLBACK).
                                   Each machine's own: it changes how far it guesses, never what it plays */
    int room;                   /* 1: a room (below): the session lasts across matches; 0: the session is one
                                   game, begun once every seat is taken (o->all) */
    int hold_seats;             /* (a room, rollback with share / take) a player who drops does not end the match: it
                                   waits for them (ob_net_waiting_for), and goes on from its newest state whose buttons
                                   were all known once a machine of the same name comes back */
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
uint32_t ob_net_step_count(ob_net *n);  /* steps run so far (after a rollback: back to the state's step) */

/* Rollback's step, called where ob_net_step would be: before each step, with this machine's
 * buttons for it as ob_net_step takes them. resumable: whether the game could go on from here
 * after a state is loaded (its game loop's wait; 0 in menus and loading screens, where it only
 * guesses as far back as its last state, then waits as lockstep does). The core saves a state
 * here when it may need one.
 *   OB_NET_RUN (1): all[] holds the buttons of step ob_net_step_count() - 1 (guessed for pads
 *     whose buttons have not come): run it. ob_net_replaying says whether it is a replay.
 *   OB_NET_WAIT (0): not yet (the guesses would go further than a rollback can reach, or this
 *     machine is ahead of the others and gives a step back): don't run the game, present the same
 *     picture, call again soon.
 *   OB_NET_RESTART (2): a guess was wrong and a state has been loaded (load was called): the step
 *     count is back at the state's step. Go on from the resumable point the state was taken at
 *     (longjmp there, or just carry on when the game's loop is the caller's) and call again.
 *   -1: the session ended.
 * Without states, it is ob_net_step (resumable unused, never RESTART). */
enum { OB_NET_WAIT = 0, OB_NET_RUN = 1, OB_NET_RESTART = 2 };
int ob_net_frame(ob_net *n, int resumable, const uint16_t *local, uint16_t *all);
/* the step ob_net_frame last gave is played again after a rollback (it was played before with a
 * guess): run it as fast as possible with no sound out, no picture presented and nothing done
 * outside the game (no rumble, files, stats); the game's own sound state still runs */
int ob_net_replaying(ob_net *n);
uint32_t ob_net_confirmed(ob_net *n);   /* steps whose every button is known (a prefix) */

/* every so often (every 60 steps, say): the simulation's hash after running step `step`. The host
 * compares the guests' with its own; a difference ends the session as "desync at step N". With
 * rollback, give it each time the step is run (a replay's replaces the guess's): it is compared
 * once every button up to that step is known. */
void ob_net_check(ob_net *n, uint32_t step, uint64_t sim_hash);

void ob_net_quit(ob_net *n);            /* goodbye to all (they end at once, told who left); state ENDED */
const char *ob_net_why(ob_net *n);      /* why it ended ("" while it goes on) */
/* one line for the Online page: "34 ms, delay 2, waiting 0.3%" while playing (waiting: the steps
 * that had to wait for someone's buttons), what it waits for while connecting, or why it ended */
void ob_net_status(ob_net *n, char *line, int size);
/* this machine's address on its own network ("192.168.1.20"; "" none); fits ob_online_ops.lan_address */
const char *ob_net_lan_address(char *buf, int size);

/* ---- rooms ----------------------------------------------------------------------------------
 *
 * A room (ob_net_game.room = 1) is a session that lasts until its host leaves: machines join it
 * whenever they like (up to OB_NET_MAX_MACHINES; o->here may be 0, a machine that only watches),
 * and it plays any number of matches, each begun by the host with a seat table. Its state is
 * OB_NET_LOBBY between matches and OB_NET_GO in one; ob_net_frame / ob_net_step say OB_NET_OVER
 * when this machine's match has ended (ob_net_match_why). The game's settings (g->settings) are
 * the room's, given to each machine as it joins; a match brings its own.
 *
 * Every machine in the room is in the host's match, also one that joins while it goes on: when the
 * states can be shared (ob_net_states.share and take), the newcomer is sent the newest state whose
 * buttons are all known, and its first resumable ob_net_frame takes it (OB_NET_RESTART), so it must
 * have started the same match from the match's settings first. Otherwise the newcomer waits in the
 * lobby for the next match.
 * A machine that leaves the room ends the match if it played in it.
 *
 * Messages go between the machines of a room, in order and without loss, whatever their size (up
 * to OB_NET_MAX_MESSAGE): chat, a game's set-up, files. They are not part of the steps. */
enum { OB_NET_OVER = 3 };               /* (ob_net_frame, ob_net_step) */
#define OB_NET_ALL (-1)                 /* ob_net_send: every other machine of the room */

typedef struct ob_net_match {
    int8_t owner[OB_NET_MAX_PADS];  /* the machine that plays each pad (-1: nobody); pads past the game's ignored */
    int delay;                      /* input delay, steps (-1: the room's) */
    const void *settings;           /* sent with it to every machine (ob_net_settings) */
    size_t settings_size;
    /* (rollback with share / take; NULL: from the start) a shared state (ob_net_share_confirmed, a save) every
     * machine takes at its first resumable ob_net_frame (OB_NET_RESTART), the match going on from state_step */
    const void *state;
    size_t state_size;
    uint32_t state_step;
} ob_net_match;

/* (the host, in its lobby) a match: every machine of the room takes part (those without pads watch). 0, or -1
 * with why in err (an owner not in the room, a match already on) */
int ob_net_match_begin(ob_net *n, const ob_net_match *m, char *err, int err_size);
/* this machine's match is over (its game ended): back to the lobby. The host's ends it for every machine
 * that has not ended it yet, at the step the host is at. With rollback, end it after a step only once every
 * button up to that step is known (ob_net_confirmed() > it), or a wrong guess before it stays unplayed */
void ob_net_match_end(ob_net *n);
uint32_t ob_net_match_id(ob_net *n);    /* the newest match (1, 2 ...; 0 none) */
const char *ob_net_match_why(ob_net *n);    /* why this machine's last match ended ("" its game did, or none) */
int ob_net_me(ob_net *n);               /* this machine's number (the host 0) */
unsigned ob_net_members(ob_net *n);     /* the machines in the room (bit k: machine k) */
int ob_net_here_of(ob_net *n, int machine);     /* the players at a machine (as it said when it joined) */
int ob_net_ping_of(ob_net *n, int machine);     /* its round trip to the host, ms (-1: not known) */
uint32_t ob_net_room_changes(ob_net *n);        /* counts each change of who is in the room */
/* (the host, hold_seats) the machines whose players dropped out of the match on and are waited for (bit k: machine k) */
unsigned ob_net_waiting_for(ob_net *n);
/* (rollback with share) the newest state of this match whose buttons are all known, shared (as share does):
 * its size (more than cap: nothing written; 0: none yet), its step in *step. For saves */
size_t ob_net_share_confirmed(ob_net *n, uint32_t *step, void *out, size_t cap);
/* a message of type (0 .. OB_NET_TYPE_LAST) to a machine of the room (OB_NET_ALL: every other one);
 * 0, or -1 (no such machine, too large, not in a room yet) */
int ob_net_send(ob_net *n, int to, int type, const void *data, size_t size);
/* the next message that came: 1 (its sender, type, bytes and size), 0 none, -1 when buf (cap bytes) is too
 * small (*size says how large it is; it stays first) */
int ob_net_recv(ob_net *n, int *from, int *type, void *buf, size_t cap, size_t *size);

/* a bad network, for tests and trying the delay: every datagram this machine sends delayed by
 * delay_ms plus up to jitter_ms, loss_pct per cent lost and dup_pct per cent sent twice */
void ob_net_simulate(ob_net *n, int delay_ms, int jitter_ms, int loss_pct, int dup_pct);

#ifdef __cplusplus
}
#endif

#endif
