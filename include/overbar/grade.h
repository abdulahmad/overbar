/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Movie colour: a movie's shots found (a cut is a big change in the colours from one frame to the next) and,
 * for each, a small correction worked out from the shot's own pictures, kept in a small table and applied live:
 *
 *   black and white points  the shot's darkest and brightest (0.5% / 99.8%) stretched to black and white,
 *                           never more than to 0.12 / 0.70, with a soft shoulder over 0.8 so ice keeps its detail
 *   colour balance          its bright, nearly grey pixels (ice, boards, shirts) made neutral, at most 12% a channel
 *   saturation              muted shots get up to 35% more, colourful ones none
 *
 * For full motion video whose colours were squeezed into a console's palette. Generic: frames in, a table out.
 * No SDL in here.
 */
#ifndef OVERBAR_GRADE_H
#define OVERBAR_GRADE_H
#include <stdint.h>

typedef struct { float lo, hi, gain[3], sat; } ob_grade_params;     /* gain: red, green, blue */
typedef struct { int start, end; ob_grade_params p; } ob_grade_scene;  /* frames start .. end - 1 */

/* making it: give it a movie's frames (w x h ARGB) in order, then take the table */
typedef struct ob_grade_maker ob_grade_maker;
ob_grade_maker *ob_grade_make(int w, int h);
int ob_grade_add(ob_grade_maker *m, const uint32_t *frame);           /* 1 when this frame starts a new shot */
int ob_grade_end(ob_grade_maker *m, ob_grade_scene **scenes);         /* the shots (malloc'd), or -1; frees m */

/* its file: a few bytes a shot; source is the port's id for the movie. load: -1 when it is not there, another
 * movie's or broken */
int ob_grade_save(const char *path, uint32_t source, const ob_grade_scene *s, int n);
int ob_grade_load(const char *path, uint32_t source, ob_grade_scene **scenes);

/* applied: in to out (may be the same), n pixels ARGB */
typedef struct { uint16_t lut[3][256]; int sat; } ob_grade_lut;     /* (sat: 256 = none) */
void ob_grade_prepare(const ob_grade_params *p, ob_grade_lut *l);
void ob_grade_apply(const ob_grade_lut *l, const uint32_t *in, uint32_t *out, long n);
const ob_grade_scene *ob_grade_find(const ob_grade_scene *s, int n, int frame);   /* the shot of a frame, NULL none */
#endif
