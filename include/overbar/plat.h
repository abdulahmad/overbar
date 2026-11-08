/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Overbar's SDL2 platform layer (docs/PLATFORM.md): the window, the picture, the overlay,
 * the event pump and the controllers, shared by every port. A port gives hooks for what is its own.
 * Built with OVERBAR_SDL; the rest of the library does not need SDL.
 */
#ifndef OVERBAR_PLAT_H
#define OVERBAR_PLAT_H

#if defined(OB_SDL_BASE) && !defined(OVERBAR_SDL)
#define OVERBAR_SDL 1             /* a client build (OB_SDL_BASE) has SDL */
#endif
#include "overbar/overbar.h"

#ifdef OVERBAR_SDL
#include <SDL.h>

#define OB_PLAT_SLOTS 8                       /* controllers, each in the slot it was given when plugged in */

typedef struct ob_plat_hooks {
    const char *title;
    int win_w, win_h;                         /* the window's first size on a desktop */
    ob_class cls;                             /* the picture's shape for ob_view */
    int src_w, src_h;                         /* the picture's size */
    const uint32_t *(*picture)(void);         /* ARGB8888, src_w pixels a row; the current frame */
    const uint32_t *(*frame)(int *w, int *h); /* instead of picture: a frame whose size changes (src_w, src_h are its first size) */

    /* all optional (NULL: no-op) */
    void (*changed)(void);                    /* after an overlay change (the port saves its settings) */
    int  (*action)(int id);                   /* an overlay action the layer does not know (OB_ID_CONTROLLERS ...) */
    int  (*captures)(void);                   /* 1 while the port reads raw keys and buttons itself (a pad setup screen) */
    void (*raw_key)(int scancode);            /* a key press while captures() */
    void (*raw_button)(int slot, int button); /* a controller button (SDL_CONTROLLER_BUTTON_*, or -1 / -2 for a left / right trigger) while captures() */
    int  (*event)(const SDL_Event *e);        /* every event first; 1: the port used it */
    void (*draw_over)(const SDL_Rect *dst);   /* the port's own layer over the picture (banner, setup screen), under the overlay */
    void (*quit)(void);                       /* the player closed the game: save and leave (should not return) */
    void (*controller)(int slot, int added);  /* a controller came or went */
} ob_plat_hooks;

/* SDL_Init (video, audio, game controllers) and the profile (handheld or desktop); call before ob_host_init. 0 on failure. */
int ob_plat_init(ob_profile *profile);
/* The window, the renderer, the textures, the bar and the controllers already plugged in. 0 on failure. */
int ob_plat_open(const ob_plat_hooks *hooks, ob_host *h);
void ob_plat_close(void);

void ob_plat_pump(void);                      /* the window's events: overlay keys, pads, mouse, text, the bar */
void ob_plat_present(void);                   /* draws the picture, the port's layer, the stats and the overlay, and flips */
void ob_plat_frame_done(int is_new);          /* count a frame shown (is_new: the game produced a new picture) */

SDL_Window *ob_plat_window(void);
SDL_Renderer *ob_plat_renderer(void);
int ob_plat_window_size(int *w, int *h);      /* the area the picture is placed in (the bar not counted) */

/* controllers */
SDL_GameController *ob_plat_controller(int slot);       /* NULL: none there */
int ob_plat_controller_slot(SDL_JoystickID id);         /* -1: none */
#endif /* OVERBAR_SDL */

#endif
