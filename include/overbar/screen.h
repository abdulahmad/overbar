/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A full-window screen: something an add-on shows instead of the game's picture (a title card, an intro, a reader).
 * While it is up the platform layer draws it at the window's own size and gives it the keyboard, the mouse and the
 * controllers' menu buttons; the port holds the game. The overlay still opens over it. No SDL in here.
 */
#ifndef OVERBAR_SCREEN_H
#define OVERBAR_SCREEN_H
#include <stdint.h>

typedef enum {
    OB_SCREEN_UP = 1, OB_SCREEN_DOWN, OB_SCREEN_LEFT, OB_SCREEN_RIGHT,
    OB_SCREEN_OK,                 /* Enter, Space, A, Start */
    OB_SCREEN_BACK,               /* Esc, Backspace, B */
    OB_SCREEN_MOVE,               /* the mouse moved to x, y */
    OB_SCREEN_PRESS,              /* the left button went down at x, y */
    OB_SCREEN_RELEASE,            /* and came up */
    OB_SCREEN_WHEEL,              /* a mouse wheel's notch: y +1 up, -1 down */
    OB_SCREEN_ZOOM,               /* a pinch (a touch screen's, or a touchpad's, which Windows sends as Ctrl + scroll): amount is how much,
                                     as a share of the window (+ apart, - together); x is the same in 1/1000 */
    OB_SCREEN_PAN,                /* a touchpad's two-finger scroll: dx, dy in scroll notches (fractions), as the system scrolls */
    OB_SCREEN_MPRESS,             /* the middle button went down at x, y */
    OB_SCREEN_MRELEASE            /* and came up */
} ob_screen_kind;

typedef struct ob_screen_ev {
    int kind;
    int x, y;                     /* in the pixels draw() gets */
    int pad;                      /* 1 when it came from a controller */
    float amount, dx, dy;         /* OB_SCREEN_ZOOM, OB_SCREEN_PAN */
} ob_screen_ev;

typedef struct ob_screen {
    /* draw the whole window: px is w x h (pitch in pixels), opaque ARGB */
    void (*draw)(void *ud, uint32_t *px, int w, int h, int pitch);
    void (*input)(void *ud, const ob_screen_ev *e);
    void *ud;
} ob_screen;

void ob_screen_show(const ob_screen *s);     /* puts it up (copied); NULL takes it down */
int ob_screen_up(void);
/* the layer and ports that draw themselves: 1 if a screen is up and was drawn */
int ob_screen_draw(uint32_t *px, int w, int h, int pitch);
void ob_screen_input(const ob_screen_ev *e);
/* a plain look that shows a picture of the window's own size as it is (for ob_gl_render / ob_render_cpu) */
struct ob_look;
const struct ob_look *ob_screen_look(void);
#endif
