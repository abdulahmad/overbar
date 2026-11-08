/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Inside Overbar only: the MiSTer tables menu.c reads from the filter files, for look.c.
 */
#ifndef OVERBAR_LOOK_TABLES_H
#define OVERBAR_LOOK_TABLES_H
#include "overbar/overbar.h"

/* One shadow mask pattern of a Shadow_Masks file: for output heights from res up, w x h cells of
 * red, green, blue factors (1 + on/16 where the cell's colour has the channel, off/16 where not). */
typedef struct {
    int res, w, h;
    float rgb[16 * 16 * 3];
} ob_mask_tile;

typedef struct {
    const float (*htap)[4];         /* 64 phases of 4 taps */
    const float (*vtap)[256][4];    /* [0] dark pixels, [1] bright, 256 phases of 4 taps */
    const float *gamma;             /* 256 values, 0..1 */
    const ob_mask_tile *masks;      /* at least one (no file: a single 1.0 cell) */
    int nmasks;
} ob_mister_tables;

/* the tables of look's four MiSTer files, read again when a path changed */
void ob_mister_tables_get(const ob_look *look, ob_mister_tables *t);

#endif
