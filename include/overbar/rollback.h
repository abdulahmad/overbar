/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Rollback for a game that runs in a thread of its own and cannot be stepped from outside: the PC
 * ports, whose original code runs natively and asks for its input from deep inside its own loop.
 * A state is the game's memory (overbar/snap.h, as differences to a base) plus the game thread's
 * stack from the resumable point up to the thread's base. Loading one puts both back and jumps to
 * the resumable point, so the game goes on as if the call there had just returned the first time.
 *
 *   rb = ob_rollback_new(&game, stack_base);   stack_base: an address in the game thread's first
 *                                              frame (above everything a state goes back over)
 *   ob_net_game.states = ob_rollback_states(rb);
 *   ... at the game's input point, in the game thread, outside any interrupt:
 *   r = ob_rollback_frame(rb, step, user);     step calls ob_net_frame (with the port's lock)
 *
 * ob_rollback_frame never returns OB_NET_RESTART: on it, it jumps back into the call that took the
 * state (which calls step again). Everything step reads that is not in a state must be outside the
 * stack (statics): after the jump the stack is as it was then.
 *
 * The game's hooks keep interrupts out while a state is taken or put back (hold / release); the
 * tag hold gives when a state is taken comes back to release once the game is back at that state. */
#ifndef OVERBAR_ROLLBACK_H
#define OVERBAR_ROLLBACK_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/net.h"
#include "overbar/snap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ob_rollback ob_rollback;

typedef struct ob_rollback_game {
    void *user;
    /* the memory a state holds, besides the stack: count written to r (room for max), or -1 */
    int (*regions)(void *user, ob_snap_region *r, int max);
    ob_snap_watch_fn watch;         /* (NULL: every page is compared) */
    /* before a state is taken or put back: a word of the caller's to keep with it (an interrupt
     * flag, say), and interrupts off. NULL: none */
    uint32_t (*hold)(void *user);
    /* after: taken (loaded 0, tag from hold just now) or back at a loaded state (loaded 1, the tag
     * hold gave when that state was taken). NULL: none */
    void (*release)(void *user, uint32_t tag, int loaded);
    int slots;                      /* states kept (0: 32; more than the rollback's reach) */
    int max_regions;                /* (0: 1024) */
} ob_rollback_game;

ob_rollback *ob_rollback_new(const ob_rollback_game *g, void *stack_base);
void ob_rollback_free(ob_rollback *rb);
const ob_net_states *ob_rollback_states(ob_rollback *rb);
/* the resumable point: step(user) is ob_net_frame (or a stand-in); what it returns, except that
 * OB_NET_RESTART jumps to the loaded state's ob_rollback_frame */
int ob_rollback_frame(ob_rollback *rb, int (*step)(void *user), void *user);
void ob_rollback_reset(ob_rollback *rb);        /* forget every state (a session's end) */
/* Its states can be shared (ob_net_states.share / take: a player joining a match under way, a recovered match).
 * A shared state holds the regions whole and the stack, with their addresses; it is taken only where they are
 * at the same addresses (the same build, the game's memory at fixed places, the game thread's stack at the same
 * base) and at the same resumable point. Why the last one was not taken ("" taken): */
const char *ob_rollback_why(ob_rollback *rb);
/* a line for logs: states taken, their time, the last one's size */
const char *ob_rollback_stats(ob_rollback *rb);
/* FNV-1a of the regions as they are now (desync hunting) */
uint64_t ob_rollback_hash(ob_rollback *rb);
/* the same per region, into h (room for max): count */
int ob_rollback_region_hashes(ob_rollback *rb, uint64_t *h, int max);

#ifdef __cplusplus
}
#endif
#endif
