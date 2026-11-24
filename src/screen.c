/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The full-window screen (screen.h): the one an add-on put up, if any.
 */
#include "overbar/screen.h"
#include "overbar/overbar.h"
#include <string.h>

static ob_screen cur;
static int up;

void ob_screen_show(const ob_screen *s)
{
    if (s && s->draw) { cur = *s; up = 1; }
    else { memset(&cur, 0, sizeof cur); up = 0; }
}
int ob_screen_up(void) { return up; }
int ob_screen_draw(uint32_t *px, int w, int h, int pitch)
{
    if (!up || !px || w < 1 || h < 1) return 0;
    cur.draw(cur.ud, px, w, h, pitch);
    return 1;
}
void ob_screen_input(const ob_screen_ev *e)
{
    if (up && cur.input && e) cur.input(cur.ud, e);
}
const ob_look *ob_screen_look(void)
{
    static ob_look plain;
    static int done;
    if (!done) {
        memset(&plain, 0, sizeof plain);
        plain.mode = OB_MODE_SHARP;
        plain.sharp = OB_SHARP_1X;
        plain.height = OB_HEIGHT_FIT;
        plain.mask_scale = 1;
        plain.bright = 100;
        done = 1;
    }
    return &plain;
}
