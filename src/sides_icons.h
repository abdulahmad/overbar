/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The select-sides screen's device icons (see sides_icons.c). */
#ifndef OVERBAR_SIDES_ICONS_H
#define OVERBAR_SIDES_ICONS_H

#include <stdint.h>

enum { OB_ICON_PAD, OB_ICON_KEYBOARD, OB_ICON_MOUSE, OB_ICONS };

typedef struct ob_icon {
    const char *const *rows;        /* one string per row, one letter per game pixel */
    int w, h;
    const char *keys;               /* with rgb: the letters this icon names exact colours by (NULL: the shared letters) */
    const uint32_t *rgb;
} ob_icon;

extern const ob_icon ob_icons[OB_ICONS];
extern const char *const *const ob_star_rows;   /* the star: c outline, s shadow */
extern const int ob_star_w, ob_star_h;

#endif
