/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A console's online features, on a room's match (overbar/net.h). On a console the whole machine is
 * the state and its menus run inside it, so one match lasts the whole stay; what a local player does
 * to the console itself (reset it, load a state) the host does for everyone, on the same step:
 *
 *   - Reset: the host's buttons carry OB_CONSOLE_RESET for one step; every machine powers its console
 *     on again after that step.
 *   - The host's state for everyone: the host sends a state over the room's messages (type
 *     OB_NET_TYPE_CONSOLE); once every machine of the room has it, the host's buttons carry
 *     OB_CONSOLE_LOAD (with OB_CONSOLE_ODD saying which of the two newest states) for one step, and every
 *     machine loads it after that step.
 *   - Host only in the menus: while the game says it is in its menus (a per-game hook, read from the
 *     simulation so it is the same on every machine), only the host's pads' buttons go to the game.
 *
 * The bits ride on the host's pads (ob_net_game.host_bits = OB_CONSOLE_BITS): the core clears them from
 * every other machine's buttons and never holds them in a guess, so a guessed step has no reset or load
 * and the real one rolls back to it. A step's event is the port's to carry out after the step runs, once
 * each time the step is run (a rollback that plays it again does it again). */
#ifndef OVERBAR_CONSOLE_H
#define OVERBAR_CONSOLE_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_CONSOLE_RESET 0x8000u        /* the host's buttons: the console reset after this step */
#define OB_CONSOLE_LOAD 0x4000u         /* the host's buttons: its newest state loaded after this step */
#define OB_CONSOLE_ODD 0x2000u          /* (with LOAD) the state of odd number */
#define OB_CONSOLE_BITS 0xE000u         /* ob_net_game.host_bits; a game's buttons must leave them free */

enum { OB_CONSOLE_NONE = 0, OB_CONSOLE_DO_RESET = 1, OB_CONSOLE_DO_LOAD = 2 };

typedef struct ob_console {
    uint8_t *state[2];                  /* the two newest states, by number's parity */
    size_t size[2];
    uint32_t id;                        /* (the host) the newest state's number (0: none yet) */
    unsigned acked, sent;               /* (the host) the machines that have it, and those it went to */
    int sending;                        /* (the host) it is on its way, not yet bound to a step */
    int reset;                          /* (the host) a reset asked for, not yet in its buttons */
    uint32_t bound_at;                  /* (the host) the step the last load was put in (+1; 0 none) */
    long resets, loads;                 /* events given by ob_console_step (replays too) */
} ob_console;

void ob_console_init(ob_console *c);
void ob_console_free(ob_console *c);
/* (the host) the console reset for everyone, at its next buttons */
void ob_console_reset(ob_console *c);
/* (the host, in a match) a state for everyone (the game's own bytes, as a local load takes them): sent to the
 * room's machines now, loaded by all on one step once each has it. 0, or -1 (not the host, too big, or the
 * last one still on its way) */
int ob_console_load(ob_console *c, ob_net *n, const void *state, size_t size);
int ob_console_busy(const ob_console *c);   /* (the host) a state on its way, not yet put in a step */
/* this machine's buttons, just before ob_net_frame (count: ob_net_my_pads' pads): the host's control bits
 * put on its first pad (a reset asked for, a state every machine has); any other machine's cleared */
void ob_console_local(ob_console *c, ob_net *n, uint16_t *local, int count);
/* after OB_NET_RUN: the step's event (OB_CONSOLE_NONE, _DO_RESET or _DO_LOAD, then *state and *size are the
 * state to load), with all[]'s (pads of them) control bits cleared. menus: the game is in its menus (the
 * step's simulation says so): every pad but the host's reads as nothing */
int ob_console_step(ob_console *c, ob_net *n, uint16_t *all, int pads, int menus, const void **state, size_t *size);
/* a message of type OB_NET_TYPE_CONSOLE from machine `from`. 1 when it was the console's */
int ob_console_message(ob_console *c, ob_net *n, int from, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
