/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The box (experimental): on a game's first launch, its retail box opens on screen in 3D. The player takes
 * out the manual and the media, reads the manual, and installs the disks (PC) or puts the cartridge in the console
 * (Genesis); then the game starts. Later launches go straight to the game. The Overbar menu gets "Open the box",
 * "Read the manual" and a setting for when the box shows.
 *
 * Drawn by its own software renderer into Overbar's full-window screen (overbar/screen.h), so it works the same on
 * every port. Overbar has the engine (renderer, screen, tweens, 2D, page reader, sounds, settings, menu entries); a
 * port brings its scene (an ob_box_scene: its states, hot spots and captions) and its 3D models (box/<dir>/ next to the
 * program, exported from the mockup by tools/box/export.js). The box art is the player's own scans, read at run time
 * from box-art/<dir>/ (or OB_BOX_ART, or art= in box.ini) and never shipped. See docs/BOX.md. */
#ifndef OVERBAR_BOX_H
#define OVERBAR_BOX_H
#include "overbar/overbar.h"
#include "overbar/r3d.h"
#include "overbar/screen.h"
#include "overbar/vfont.h"
#include <stdint.h>

enum { BOX_SHOW_FIRST, BOX_SHOW_REOPEN, BOX_SHOW_MANUAL };
struct bx_scene;

/* A port's start: adds the menu entries ("Open the box", "Read the manual", "Box at start") and, on the first launch,
 * puts the box up (the port holds its game while ob_screen_up()). Returns 1 if the box is up. 0 and no menu entries
 * when the scene's models are not beside the program. */
int ob_box_init(ob_host *h, const struct bx_scene *scene);
int ob_box_open(int mode);

/* ---- for a port's scene ---- */
#define PI_F 3.14159265f

/* ---- time: tweens and timers (milliseconds, both stop while the overlay is open) ---- */
void bx_tw(float *v, int n, const float *to, float ms, float delay);           /* n floats from where they are (when it starts) to to[] */
void bx_tw3(float *v3, float x, float y, float z, float ms, float delay);
void bx_tw1(float *v, float to, float ms, float delay);
void bx_after(float ms, void (*fn)(int), int arg);
void bx_time_clear(void);                                                       /* drop every tween and timer */
float bx_ease(float t);

/* ---- the 3D scene of the current game ---- */
extern r3_scene *BX_S;
r3_node *bx_n(int i);
int bx_node(const char *name);                    /* -1 and a warning when missing */
void bx_parent(int node, int parent);             /* three.js add(): the local pose kept */
void bx_attach(int node, int parent);             /* three.js attach(): the world pose kept */
void bx_set3(float *v, float x, float y, float z);
void bx_cam(float x, float y, float z);           /* where the camera is */
void bx_cam_to(float x, float y, float z, float ms);
extern float bx_look[3];                          /* and the point it looks at (the scene sets it each frame) */

/* ---- 2D: the window, its unit (1/100 of the 16:9 stage's width), text, buttons ---- */
typedef struct { uint32_t *px; int w, h, pitch; float u, sx, sy, sw, sh; } bx_canvas;   /* stage rect sx sy sw sh */
extern bx_canvas BX_C;
void bx_text(float x, float y, float size, uint32_t rgb, int a, const char *s, int face, int align);   /* align -1 left, 0 centre, 1 right; y = baseline */
float bx_text_w(float size, const char *s, int face);
void bx_rich(float cx, float y, float size, const char *s, uint32_t plain, uint32_t bold);              /* "<b>bold</b> rest", centred */
void bx_rect(float x, float y, float w, float h, uint32_t argb);
void bx_rrect(float x, float y, float w, float h, float r, uint32_t fill, uint32_t line, float lw);
void bx_image(const r3_tex *t, float x, float y, float w, float h, float alpha);                        /* a picture, smooth */
void bx_pill(float cx, float y, const char *s);                                                         /* a label tag */

/* hot spots: what the arrows, the mouse and a pad can pick this frame. A 3D one is a node (picked by the pick buffer), a 2D one a rect. */
typedef struct { int node; float x, y, w, h; const char *label; void (*fn)(int); int arg; int btn; int noauto; } bx_hot;
void bx_hot_clear(void);
void bx_hot_node(int node, const char *label);            /* the scene's activate() gets it */
void bx_button(float x, float y, const char *label, void (*fn)(int), int arg, int align);   /* drawn and hot; align -1 left edge at x, 1 right edge */
int bx_hovered_node(void);
/* an invisible hot rect (a 2D thing drawn by the scene); its label shows as a tag when it is lit. Returns 1 when lit. */
int bx_hot_rect(float x, float y, float w, float h, const char *label, void (*fn)(int), int arg);
void bx_desk(void);                                        /* the desk background over the whole window */                                /* the 3D hot spot under the mouse or picked by the keys, or -1 */

/* fade to black and back, running fn at the dark middle */
void bx_fade(void (*fn)(int), int arg);

/* ---- the page reader ---- */
void bx_view_open(const char *title, r3_tex **pages, int n, int spread, void (*on_close)(int));
int bx_view_up(void);

/* ---- art (the player's own scans, by key) ---- */
r3_tex *bx_art(const char *key);                          /* loaded once, kept; NULL when the scan is not there */

/* ---- sound (synthesized, like the mockup's) ---- */
enum { SFX_DISK_IN, SFX_DISK_OUT, SFX_BEEP, SFX_MOTOR, SFX_STEP, SFX_SEEK, SFX_HDD, SFX_KEY, SFX_SLIDE, SFX_PAGE, SFX_CLICK };
void bx_sfx(int kind, int n);

/* ---- the box's state, shared with the scenes ---- */
typedef struct {
    int first;            /* this launch is the first: the box ends in the game */
    int from_menu;        /* opened from the Overbar menu */
    int manual_only;      /* opened as "Read the manual": closing the reader goes back to the game */
    char cap[256];        /* the caption under the scene */
    char title[64];
    float caption_until;
} bx_state;
extern bx_state BX;
void bx_say(const char *html);
void bx_done(void);
void bx_manual_only_closed(int);                          /* the reader's close when manual_only */                                       /* fade out of the box into the game */
double bx_now(void);                                      /* seconds */

/* ---- a scene ---- */
typedef struct bx_scene {
    const char *dir;                                      /* the game's folder under box/ and box-art/ ("pc94", "gen94") */
    void (*enter)(int mode);                              /* BOX_SHOW_* */
    void (*frame)(float dt);                              /* poses, hot spots and look-at for this frame */
    void (*draw2d)(void);                                 /* captions and buttons over the 3D */
    void (*activate)(int node, float sx, float sy);       /* a hot node picked (screen point) */
    void (*back)(void);
    void (*drag)(int node, float dx, float dy);           /* a drag that started on node (or -1) */
    void (*manual)(void);                                 /* open the manual in the reader */
    int (*draws_3d)(void);                                /* 0 while the scene draws only 2D (the PC install) */
    void (*draw_full)(void);                              /* the 2D-only scene */
    void (*key)(int kind);                                /* the 2D-only scene's keys (else the hot list) */
    void (*skip)(void);                                   /* "Skip to the game": stop what the scene runs (or NULL) */
} bx_scene;
extern const bx_scene *BX_SC;

#endif
