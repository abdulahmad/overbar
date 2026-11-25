/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The select-sides screen's device icons (see sides_icons.c). */
#ifndef OVERBAR_SIDES_ICONS_H
#define OVERBAR_SIDES_ICONS_H

enum { OB_ICON_PAD, OB_ICON_KEYBOARD, OB_ICON_MOUSE, OB_ICONS };

typedef struct ob_icon {
    const char *const *rows;        /* one string per row, one letter per game pixel */
    int w, h;
} ob_icon;

extern const ob_icon ob_icons[OB_ICONS];

#endif
