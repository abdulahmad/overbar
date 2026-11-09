/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Saves of an online match as it goes, on the host, for Recover game after a crash (a dropped player
 * comes back through ob_net's held seats, ob_net_game.hold_seats). Save points:
 *   each faceoff: the last 3 kept;  every 2 minutes of game clock: the last 3;  each period's start: all;
 *   each time a player drops out (ob_net_waiting_for): the last 3, kept when the match is ended for it.
 * A save holds a shared state of a step whose every button is known (ob_net_share_confirmed: the newest
 * one at or after the save point), the match's set-up (settings, seats, delay) and a summary for the
 * Recover list: the teams and who played them, score, period and clock, the league and which game it is.
 * Saves live in a folder of the host's (one file each) and go once the match's result is in the league
 * (ob_saves_done).
 *
 * The game's part: in the simulation, ob_saves_event at a faceoff and a period's start, ob_saves_clock as
 * the game clock runs (both safe to call again on a rollback's replay: a point is saved once); outside it,
 * on the host, ob_saves_poll once a frame. Recover: ob_saves_list, then ob_save_load into ob_net_match's
 * state, settings and owners, and ob_net_match_begin. No SDL. */
#ifndef OVERBAR_SAVE_H
#define OVERBAR_SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/net.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { OB_SAVE_FACEOFF = 1, OB_SAVE_CLOCK, OB_SAVE_PERIOD, OB_SAVE_DROP };
#define OB_SAVE_KEEP 3              /* faceoff and clock saves kept */
#define OB_SAVE_CLOCK_EVERY 120     /* game seconds between clock saves */
#define OB_SAVE_TEXT 47

typedef struct ob_save_summary {
    char league[OB_SAVE_TEXT + 1];  /* "" an exhibition */
    char game[OB_SAVE_TEXT + 1];    /* "Season game 23", "Playoffs, round 2, game 3" ("" none) */
    char team[2][OB_SAVE_TEXT + 1]; /* away, home */
    char players[2][OB_SAVE_TEXT + 1];   /* who played each ("Ann, Ben"; "" the computer) */
    int score[2];
    int period;                     /* 1, 2, 3; 4 and on overtime */
    int clock;                      /* seconds left in the period */
} ob_save_summary;

typedef struct ob_saves {
    char dir[260];
    char identity[256];
    uint32_t key;                   /* this match's, in its files' names */
    uint8_t *settings;
    size_t settings_size;
    int8_t owner[OB_NET_MAX_PADS];
    int delay;
    struct { int kind; uint32_t step; ob_save_summary sum; } pend[8];   /* points waiting for their buttons */
    int npend;
    uint32_t done_step[5];          /* by kind: the newest point's step (+1) taken */
    int clock_mark;                 /* game seconds / OB_SAVE_CLOCK_EVERY last saved at */
    int seq[5];                     /* files written, by kind */
    ob_save_summary now;            /* the newest summary the game gave (a drop's) */
    int have_now;
    unsigned dropped;               /* (ob_saves_poll) the machines waited for, last seen */
    int saved;                      /* files written in all */
    char why[160];                  /* the last failure ("" none) */
} ob_saves;

/* (the host) a match begins: its saves go to dir (made if missing). key: 0 makes one */
int ob_saves_begin(ob_saves *s, const char *dir, const char *identity, const ob_net_match *m, uint32_t key);
void ob_saves_free(ob_saves *s);
/* (in the simulation) a faceoff or a period's start at step, with the summary as it is now */
void ob_saves_event(ob_saves *s, int kind, uint32_t step, const ob_save_summary *now);
/* (in the simulation) the game clock: game_seconds played in all (periods before included) */
void ob_saves_clock(ob_saves *s, uint32_t step, int game_seconds, const ob_save_summary *now);
/* (the host, outside the simulation, once a frame) writes the points whose buttons are all known, and a save when a
 * player drops out (the newest state whose buttons were all known, with the newest summary). Files written now */
int ob_saves_poll(ob_saves *s, ob_net *n);
/* the same with any source of shared states (tests): confirmed, and share as ob_net_share_confirmed */
int ob_saves_poll_with(ob_saves *s, uint32_t confirmed, size_t (*share)(void *ud, uint32_t *step, void *out, size_t cap), void *ud);
/* the match's result is in the league: its files go (not when it was ended because a player dropped: the drop's
 * save stays for Recover game) */
void ob_saves_done(ob_saves *s);

/* ---- Recover game ---- */

typedef struct ob_save_entry {
    char path[300];
    uint32_t key, step;
    int kind;
    int64_t when;                   /* written, seconds since 1970 */
    ob_save_summary sum;
} ob_save_entry;
/* the saves in dir of this game (identity), newest first. Their count */
int ob_saves_list(const char *dir, const char *identity, ob_save_entry *e, int max);
/* one line for the list: "2nd period 12:34  Montreal 2 - 1 Boston  (Ann vs Ben)  My league, Season game 23" */
void ob_save_line(const ob_save_entry *e, char *line, int size);

typedef struct ob_save_file {
    ob_save_entry e;
    int8_t owner[OB_NET_MAX_PADS];
    int delay;
    uint8_t *settings, *state;
    size_t settings_size, state_size;
} ob_save_file;
/* reads a save: 0, or -1 (another game, damaged) */
int ob_save_load(const char *path, const char *identity, ob_save_file *f);
/* the match to begin from it (pointers into f) */
void ob_save_match(const ob_save_file *f, ob_net_match *m);
void ob_save_file_free(ob_save_file *f);

#ifdef __cplusplus
}
#endif

#endif
