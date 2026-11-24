/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The CD box (overbar/box.h, docs/BOX.md): one scene for the PC games that came on a CD in a big box (NHL 95, 96, 97 PC).
 * The box opens and lays out what was in it: the manual, the technical reference card, the jewel case (it opens; the disc
 * is in it), the registration card, the EA lineup and the other cards. On the first launch the disc goes into the CD-ROM
 * drive of the 486 on the desk and the game's own installer runs on its monitor, screen by screen; later the box is
 * opened from "View Manual & Materials" and everything can still be looked at.
 * A port describes its game (bx_cd_game) and passes bx_cd_scene(&its game) to ob_box_init. The models are box/<dir>/
 * (tools/box/export.js <mockup> <out> pc95,pc96,pc97); the scans, and the installer's screens, box-art/<dir>/. */
#ifndef OVERBAR_BOX_CD_H
#define OVERBAR_BOX_CD_H
#include "overbar/box.h"

/* one step of the install, as it showed on the monitor */
enum { CD_TEXT,          /* at the DOS prompt: the line text (typed when type is set) */
       CD_SCREEN,        /* a screen of the installer: the picture at key (box-art/<dir>/<key>.png), skipped when it is not there */
       CD_COPY,          /* the same screen while files copy: the drive lights busy */
       CD_CLS };         /* the text screen cleared */
typedef struct {
    int kind;
    const char *s;       /* CD_TEXT: the line; CD_SCREEN / CD_COPY: the picture's key */
    int type;            /* CD_TEXT: typed at the prompt, key by key */
    int ms;              /* how long it stays */
    int key;             /* a key press is heard before it (the player pressed Enter or an arrow) */
} bx_cd_step;

typedef struct bx_cd_game {
    const char *dir;                 /* "pc95" */
    const char *title;               /* "NHL Hockey '95" */
    int man_pages, tech_pages;       /* man0.. and tech0.. in box-art (the reader); mp/tp the models' smaller ones */
    const bx_cd_step *install; int ninstall;
} bx_cd_game;

const bx_scene *bx_cd_scene(const bx_cd_game *g);

/* (box_cd.c and box_cd_install.c between them) */
extern const bx_cd_game *BX_CD;
void bx_cd_arranged(void);
void bx_cd_install_start(void);
int bx_cd_install_up(void);
void bx_cd_install_frame(float dt);
void bx_cd_install_draw2d(void);
void bx_cd_install_activate(int node);
void bx_cd_install_back(void);
void bx_cd_install_skip(void);
#endif
