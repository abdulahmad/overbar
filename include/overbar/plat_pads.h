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
} ob_pads_cfg;

void ob_pads_init(const ob_pads_cfg *c);              /* reads pad<N>.<button>, player<N>.controller, controller.<button>, controller.dead_zone */
uint32_t ob_pads_read(int player);                    /* the buttons held now (0 while the setup screen is open) */
int ob_pads_extra(void);                              /* 1 when player 3 or later has keys or a controller (the multitap / 4 Way) */
SDL_GameController *ob_pads_controller(int player);   /* NULL: none */

/* the controller setup screen; wire these into ob_plat_hooks */
void ob_pads_setup_open(int slot);                    /* -1: the player picks the controller by pressing a button on it */
int ob_pads_setup_active(void);
void ob_pads_setup_key(int scancode);
void ob_pads_setup_button(int slot, int button);     /* SDL_CONTROLLER_BUTTON_*, or -1 / -2 for a left / right trigger */
void ob_pads_tick(void);                              /* once a frame while it is open */
void ob_pads_setup_draw(const SDL_Rect *dst);
#endif
#endif
