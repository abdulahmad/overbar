/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The select-sides screen: the one every port shows on going to play a game, for the controllers
 * plugged into this machine and, online, for those of every machine in the room. Each device (a
 * controller, the keyboard's player 1 keys ...) starts in the middle; LEFT and RIGHT move it to a
 * team's column, OK says it is ready, BACK takes that back (or returns it to the middle). When every
 * device on a team is ready, OK from one of the deciding machine's devices starts the game (the
 * host's online, the only machine there is otherwise). Devices left in the middle do not play; none
 * on a team is the computer against itself.
 *
 * The model is separate from its drawing and its input, as the on-screen keyboard: no SDL here. A
 * device is (owner, local): owner the machine it is plugged into (0: this machine when not online, the
 * host online; ob_net_me), local its number there (the port's own: a player slot, a controller).
 *
 * Online the host's model is the only one that changes: a guest's devices and presses go to it as
 * messages (OB_NET_TYPE_SIDES), and it sends the whole state back to every machine after each change.
 * ob_sides.send carries them (the port: ob_net_send); ob_sides_message takes the ones that come.
 *
 * What it gives the game: the seat table (each seat a team and the device that plays it, by team then
 * by the order the devices came in), the owners of ob_net's pads for ob_net_match_begin, and the
 * game's hook (ob_sides_game.apply) that writes the table into the game's own controller setup.
 *
 * Keys for one team only (line changes, the goalie): a game lists the input bits that act for one
 * team (ob_sides_hotkey); ob_sides_hotkeys_filter clears those of the teams a machine has no seat on.
 * Run it in the step on every machine for each machine's input (it is deterministic: the seat table is
 * the match's), and before sending, so nobody changes the other team's lines. */
#ifndef OVERBAR_SIDES_H
#define OVERBAR_SIDES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_SIDES_MAX_DEVICES 16
#define OB_SIDES_MAX_TEAMS 2
#define OB_SIDES_MAX_SEATS 8        /* = OB_NET_MAX_PADS */
#define OB_SIDES_NAME 23
#define OB_SIDES_MIDDLE (-1)

typedef struct ob_sides_seat {
    int team;                       /* 0 .. teams - 1 */
    int owner, local;               /* the device that plays it */
    const char *name;               /* the device's */
} ob_sides_seat;

typedef struct ob_sides_game {
    int teams;                      /* columns: 2 (team 0 on the left) */
    int seats;                      /* the game's controller slots in all: 2 for 94 / 95 PC, 4 for 96 PC */
    int per_team;                   /* most devices on one team (0: seats) */
    const char *team_name[OB_SIDES_MAX_TEAMS];   /* the columns' headings ("Away", "Home"; ob_sides_set_team) */
    /* writes the seat table into the game's controller setup, once the game starts (NULL: the port reads it) */
    void (*apply)(void *ud, const ob_sides_seat *seat, int n);
    void *ud;
} ob_sides_game;

typedef struct ob_sides_device {
    int8_t owner, local;
    int8_t team;                    /* OB_SIDES_MIDDLE, or 0 .. teams - 1 */
    uint8_t ready;
    char name[OB_SIDES_NAME + 1];
} ob_sides_device;

typedef struct ob_sides {
    ob_sides_game g;
    char team[OB_SIDES_MAX_TEAMS][OB_SIDES_NAME + 1];
    int n;
    ob_sides_device d[OB_SIDES_MAX_DEVICES];
    int open;
    int done;                       /* the game started with this table */
    int me;                         /* this machine */
    int host;                       /* 1: this machine decides (the host, or not online) */
    int online;
    uint32_t changes;               /* counts each change */
    int nmine;                      /* (a guest) its own devices, said again to the host when it comes */
    struct { int8_t local; char name[OB_SIDES_NAME + 1]; } mine[OB_SIDES_MAX_DEVICES];
    /* online: a message to a machine (-1: every other one), type OB_NET_TYPE_SIDES; NULL not online */
    void (*send)(void *ud, int to, const void *data, size_t size);
    void *send_ud;
} ob_sides;

/* a screen for a game (g copied). me: this machine (ob_net_me; 0 when not online); send NULL when not online. */
void ob_sides_init(ob_sides *s, const ob_sides_game *g, int me, void (*send)(void *ud, int to, const void *data, size_t size), void *ud);
void ob_sides_open(ob_sides *s);    /* shows it again with every device in the middle, not ready */
void ob_sides_close(ob_sides *s);
void ob_sides_set_team(ob_sides *s, int team, const char *name);   /* (the deciding machine) a column's heading */

/* a device of this machine (a guest's goes to the host) / gone. The index, or -1 */
int ob_sides_add(ob_sides *s, int local, const char *name);
void ob_sides_remove(ob_sides *s, int local);
/* (the host) a machine left the room: its devices go */
void ob_sides_machine_gone(ob_sides *s, int machine);
/* a machine came into the room: the host sends it the state; a guest, seeing the host (0), says its devices again */
void ob_sides_machine_came(ob_sides *s, int machine);

/* an input (MDRT_MENU_LEFT / RIGHT / OK / BACK) from a device of this machine. 1 when the screen used it */
int ob_sides_input(ob_sides *s, int local, int mdrt_input);
/* a message of type OB_NET_TYPE_SIDES from machine `from`. 1 when the state changed */
int ob_sides_message(ob_sides *s, int from, const void *data, size_t size);

int ob_sides_find(const ob_sides *s, int owner, int local);         /* the device's index, or -1 */
int ob_sides_all_ready(const ob_sides *s);                          /* every device on a team is ready */
/* the seat table, by team then by the devices' order (seat[n], n <= g.seats). Its size */
int ob_sides_seats(const ob_sides *s, ob_sides_seat *seat, int max);
int ob_sides_seat_of(const ob_sides *s, int owner, int local);      /* the device's seat, or -1 (in the middle) */
/* the owners of ob_net's pads (ob_net_match.owner): seat k plays pads k * per_seat .. k * per_seat + per_seat - 1,
 * the rest nobody's (-1) */
void ob_sides_owners(const ob_sides *s, int8_t *owner, int pads, int per_seat);
unsigned ob_sides_teams_of(const ob_sides *s, int machine);         /* bit t: the machine has a seat on team t */

typedef struct ob_sides_hotkey {
    uint32_t bits;                  /* the input bits */
    int team;                       /* the team they act for */
} ob_sides_hotkey;
/* in with the bits of every hotkey for a team not in teams (ob_sides_teams_of) cleared */
uint32_t ob_sides_hotkeys_filter(uint32_t in, const ob_sides_hotkey *key, int n, unsigned teams);

/* draws the screen over a w x h ARGB picture */
void ob_sides_draw(const ob_sides *s, uint32_t *px, int w, int h, int pitch);

#ifdef __cplusplus
}
#endif

#endif
