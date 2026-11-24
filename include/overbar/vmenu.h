/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The overlay menu drawn in vector type at the window's own resolution (style C: large light type over a shade at the left, soft shadows, the
 * game's name as the title with its platform under it). It draws the same mdrt_menu the pixel menu does, so pages and input are unchanged. */
#ifndef OVERBAR_VMENU_H
#define OVERBAR_VMENU_H
#include <stdint.h>
#include "mdrt/menu.h"

/* draws into a straight-alpha ARGB buffer (cleared to 0 by the caller); name / platform may be NULL */
void ob_vmenu_draw(mdrt_menu *m, uint32_t *px, int w, int h, int pitch, const char *name, const char *platform);
/* the item under a pixel of the last drawing's size, or -1 */
/* 1: the pixel is at the list's bottom edge (or below it), -1: at its top edge, 0: neither, or the list has nothing more that way */
int ob_vmenu_edge(const mdrt_menu *m, int x, int y, int w, int h);
int ob_vmenu_at(const mdrt_menu *m, int x, int y, int w, int h);
#endif
