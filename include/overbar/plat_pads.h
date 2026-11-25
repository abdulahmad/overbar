/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The players' pads for every port on the platform layer: each player's keys and controller, the
 * controller setup screen, and the settings that hold them. The controller setup screen's drawing
 * and the button assignment come from PulseMan (MIT, see NOTICE) through the SNES port.
 * A port gives its buttons (the first four are the directions) and a place to keep settings.
 */
#ifndef OVERBAR_PLAT_PADS_H
#define OVERBAR_PLAT_PADS_H

#include "overbar/plat.h"
#include "overbar/sides.h"

#ifdef OVERBAR_SDL
#define OB_PADS_MAX 5

typedef struct ob_pad_button {
    const char *name;              /* in the settings file ("up", "b") */
    uint32_t bit;                  /* the port's bit for it */
    const char *key1, *key2;       /* SDL key names, the defaults for players 1 and 2 */
    const char *ctrl;              /* the default controller button (SDL's name; "" none) */
    const char *role;              /* what it does in the game, for the setup screen (NULL: nothing said) */
} ob_pad_button;

typedef struct ob_pads_cfg {
    int players;                   /* up to OB_PADS_MAX */
    int n;                         /* buttons; the first four are up, down, left, right */
    const ob_pad_button *b;
    int start;                     /* the button that saves the setup screen */
    int src_w, src_h;              /* the picture's size, which the setup screen is drawn at */
    /* the port's settings: get returns the value (writing def into the file when it is new); set changes it; save writes the file */
    const char *(*get)(const char *key, const char *def);
    void (*set)(const char *key, const char *value);
    void (*save)(void);
    /* the select-sides screen (ob_pads_sides_open): the two columns' names (NULL: "Away", "Home"; team 0 is player 1) and the game's own look */
    const char *team_name[2];
    const ob_sides_skin *skin;
    /* the mouse as a pad (a player the screen gives it to): its left, right and middle buttons press this button of b, plus one (0: none) */
    int mouse_btn[3];
} ob_pads_cfg;

void ob_pads_init(const ob_pads_cfg *c);              /* reads pad<N>.<button>, player<N>.controller, controller.<button>, controller.dead_zone */
uint32_t ob_pads_read(int player);                    /* the buttons held now (0 while the setup screen is open) */
int ob_pads_player(int slot);                         /* the first player (1 ..) using the controller in that slot; 0: none */
int ob_pads_extra(void);                              /* 1 when player 3 or later has keys or a controller (the multitap / 4 Way) */
SDL_GameController *ob_pads_controller(int player);   /* NULL: none */

/* The select-sides screen (sides.h): the keyboard, the mouse and every controller start in the middle; each goes to a column
 * (player 1's, player 2's; a second device on a side is player 3 or 4) and says it is ready, and OK starts. A device left in the
 * middle does not play. Opens over the game from the Input page (host.sides_menu) or by a port's call; the port draws nothing. */
void ob_pads_sides_open(void);
int ob_pads_sides_active(void);
/* The mouse as a pad: the player it was given on the screen (-1: none). It steers like NHL 95 PC: the pointer's distance from the
 * middle of the window is the stick, the buttons are cfg.mouse_btn. ob_pads_mouse_draw draws the small green cursor; call it
 * where the port draws over the picture (ob_pads_setup_draw's place). */
int ob_pads_mouse_player(void);
void ob_pads_mouse_draw(const SDL_Rect *dst);

/* the controller setup screen; wire these into ob_plat_hooks */
void ob_pads_setup_open(int slot);                    /* -1: the player picks the controller by pressing a button on it */
int ob_pads_setup_active(void);
void ob_pads_setup_key(int scancode);
void ob_pads_setup_button(int slot, int button);     /* SDL_CONTROLLER_BUTTON_*, or -1 / -2 for a left / right trigger */
void ob_pads_tick(void);                              /* once a frame while it is open */
void ob_pads_setup_draw(const SDL_Rect *dst);
#endif
#endif
