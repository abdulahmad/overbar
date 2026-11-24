/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Vector text for the overlay: Inter (SIL OFL 1.1) drawn with stb_truetype (public domain) into an ARGB buffer with straight alpha (what a
 * blended SDL texture wants). Every glyph is drawn once per face and size and kept (with its blurred shadow); drawing a line is then a copy.
 * No SDL, no files: the faces are in the program. */
#ifndef OVERBAR_VFONT_H
#define OVERBAR_VFONT_H
#include <stdint.h>

typedef enum { OB_VF_LIGHT, OB_VF_REGULAR, OB_VF_MEDIUM, OB_VF_SEMIBOLD, OB_VF_FACES } ob_vf_face;

/* the width in pixels of a UTF-8 line at a size (the em in pixels), with extra pixels between letters */
float ob_vf_width(ob_vf_face face, float px, const char *s, float tracking);
/* the line with its left end at x and its baseline at y. rgb 0xRRGGBB, a 0..255; shadow > 0 draws a soft shadow first, offset by that many
 * pixels down and right and blurred over that many again (0: none). Returns the x after it. */
float ob_vf_draw(uint32_t *buf, int w, int h, int pitch, float x, float y, ob_vf_face face, float px, uint32_t rgb, int a,
                 const char *s, float tracking, int shadow);
/* one pixel of colour rgb at coverage a (0..255) over what is there (straight alpha) */
void ob_vf_blend(uint32_t *p, uint32_t rgb, int a);
/* a rounded rectangle, anti-aliased: fill and an outline of the given width (0: none); colours 0xAARRGGBB */
void ob_vf_rrect(uint32_t *buf, int w, int h, int pitch, float x, float y, float rw, float rh, float r, uint32_t fill, uint32_t line, float lw);
/* a disc */
void ob_vf_disc(uint32_t *buf, int w, int h, int pitch, float cx, float cy, float r, uint32_t fill, uint32_t line, float lw);
/* a filled triangle (anti-aliased by 4x4 samples) */
void ob_vf_tri(uint32_t *buf, int w, int h, int pitch, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba);
/* frees the glyph cache */
void ob_vf_free(void);
#endif
