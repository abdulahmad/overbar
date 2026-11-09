/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The menu lane: pause and the game's own menus (the pause menu, the line editor) inside an online
 * match. Each machine of the match has two more of ob_net's pads (OB_LANE_FIRST + 2 x its number, and
 * the one after), and puts its menu input in them every step: one key press or release, the mouse,
 * and a pause request. So pausing and every key of a menu happen on the same step on every machine,
 * inside the simulation, and rollback covers them like buttons.
 *
 * Anyone pauses at any time: the first machine whose pause request comes while the game runs holds
 * the menu; only its keys and mouse reach the game until it resumes (its pause request again, or the
 * game's own Resume: ob_lane_resume). Others' requests meanwhile are dropped. The holder's teams
 * (ob_sides_teams_of, the match's seat table) are what its line editor may open: the port's fence.
 *
 * Outside the simulation, ob_lane_in gathers this machine's input and packs one step's words; it is
 * read only while this machine holds the menu, or for the pause request (the port routes its keys
 * there instead of the game's own keyboard then). A guess that repeats a machine's last words (rollback)
 * does nothing: a key and a pause request each flip a bit, and only a change counts.
 *
 * In the simulation, ob_lane_sim is part of the game's state (the port saves and loads it with the
 * game's) and ob_lane_step runs once a step on every machine, before the game reads its input. */
#ifndef OVERBAR_LANE_H
#define OVERBAR_LANE_H

#include <stdint.h>
#include "overbar/net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_LANE_FIRST 8             /* machine k's lane: pads OB_LANE_FIRST + 2k and + 2k + 1 */
#define OB_LANE_PADS 2
#define OB_LANE_KEYS 32             /* key events queued at most */

/* the lane's pads to machine k for each machine of machines (bit k), into a match's owner table */
void ob_lane_owners(int8_t *owner, unsigned machines);

/* ---- this machine's input (outside the simulation) ---- */

typedef struct ob_lane_in {
    uint16_t key[OB_LANE_KEYS];     /* scancode | down << 8 */
    int head, n;
    int x, y, buttons;              /* the mouse (the game's pixels; buttons: bit 0 left, 1 right) */
    int key_flip, pause_flip;       /* the bits last sent */
    int pause_wanted;
    uint32_t step;                  /* the step last packed (+1; 0 none) */
    uint16_t last[2];
} ob_lane_in;

void ob_lane_in_init(ob_lane_in *l);
void ob_lane_in_key(ob_lane_in *l, int scancode, int down);  /* a key (the game's own code, 1 .. 255) */
void ob_lane_in_mouse(ob_lane_in *l, int x, int y, int buttons);
void ob_lane_in_pause(ob_lane_in *l);                        /* pause, or resume when this machine holds the menu */
/* the words for step `step` (ob_net_step_count): the next key, the mouse, the pause request. A step already
 * packed (a replay) gets the same words again, and nothing queued is lost */
void ob_lane_in_pack(ob_lane_in *l, uint32_t step, uint16_t out[OB_LANE_PADS]);

/* a step's words, unpacked */
typedef struct ob_lane_word {
    int key, down, key_flip, pause_flip;
    int x, y, buttons;
} ob_lane_word;
void ob_lane_unpack(const uint16_t w[OB_LANE_PADS], ob_lane_word *o);

/* ---- in the simulation (part of the game's state) ---- */

typedef struct ob_lane_sim {
    int8_t holder;                  /* the machine that holds the menu (-1: the game runs) */
    uint8_t seen[OB_NET_MAX_MACHINES];   /* each machine's bits last seen: 1 key, 2 pause */
    uint8_t key, down, has_key;     /* this step's key from the holder */
    int16_t x, y;                   /* the holder's mouse */
    uint8_t buttons;
    uint32_t paused_at;             /* the step it paused at (+1; 0 never) */
} ob_lane_sim;

enum { OB_LANE_PAUSED = 1, OB_LANE_RESUMED = 2, OB_LANE_KEY = 4 };

void ob_lane_sim_init(ob_lane_sim *s);
/* once a step, in the simulation: all[] the step's pads (ob_net_frame's), machines the match's (bit k),
 * step the step's number. OB_LANE_PAUSED / RESUMED when it paused / resumed on this step, OB_LANE_KEY when
 * the holder pressed or released a key (s->key, s->down) */
int ob_lane_step(ob_lane_sim *s, const uint16_t *all, unsigned machines, uint32_t step);
/* the game's own Resume (from the holder's keys, so on the same step everywhere) */
void ob_lane_resume(ob_lane_sim *s);
/* may the holder edit team `team`'s lines? holder_teams: ob_sides_teams_of(sides, s->holder) */
int ob_lane_holder_may(const ob_lane_sim *s, unsigned holder_teams, int team);

#ifdef __cplusplus
}
#endif

#endif
