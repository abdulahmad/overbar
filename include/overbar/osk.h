/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * On-screen keyboard: a grid moved with the d-pad (or arrow keys), OK types, Back deletes. It does not know the game: each press goes out
 * through key(scancode, down), where the scancode is a USB HID usage code (the same values as SDL's scancodes), so a port can turn it into
 * its game's own keyboard (the DOS keyboard interrupt for the PC ports). No SDL here; the platform layer routes the input and draws it
 * (ob_plat_osk_*). */
#ifndef OVERBAR_OSK_H
#define OVERBAR_OSK_H

#include <stdint.h>

#define OB_OSK_LSHIFT 225                 /* sent around a letter typed with Shift on */

typedef struct ob_osk {
    int open;
    int col, row;                         /* the key under the cursor */
    int shift;                            /* latched for the next key */
    void (*key)(int scancode, int down, void *ud);
    void *ud;
} ob_osk;

void ob_osk_init(ob_osk *k, void (*key)(int scancode, int down, void *ud), void *ud);
void ob_osk_open(ob_osk *k);
void ob_osk_close(ob_osk *k);
/* in: MDRT_MENU_UP / DOWN / LEFT / RIGHT / OK / BACK (as the overlay). 1 when used (always while open). */
int ob_osk_input(ob_osk *k, int mdrt_input);
/* draws the keyboard over the bottom of a w x h ARGB picture */
void ob_osk_draw(const ob_osk *k, uint32_t *px, int w, int h, int pitch);
/* the key at a place, for tests: its label ("A", "Space", "Done") */
const char *ob_osk_label(int col, int row);

#endif
