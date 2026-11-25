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
    if (s && (s->draw || s->picture)) { cur = *s; up = 1; }
    else { memset(&cur, 0, sizeof cur); up = 0; }
}
int ob_screen_up(void) { return up; }
const uint32_t *ob_screen_picture(int *w, int *h, int *lines)
{
    if (!up || !cur.picture) return NULL;
    int ww = 0, hh = 0, ln = 0;
    const uint32_t *p = cur.picture(cur.ud, &ww, &hh, &ln);
    if (!p || ww < 1 || hh < 1) return NULL;
    *w = ww; *h = hh; *lines = ln > 0 && ln <= hh ? ln : hh;
    return p;
}

void ob_screen_draw_over(uint32_t *px, int w, int h, int pitch)
{
    if (up && cur.draw && px && w > 0 && h > 0) cur.draw(cur.ud, px, w, h, pitch);
}

/* (a host without the look: the picture smoothly scaled into the window's 4:3 middle) */
static void plain_picture(uint32_t *px, int w, int h, int pitch)
{
    int pw, ph, lines;
    const uint32_t *p = ob_screen_picture(&pw, &ph, &lines);
    int bw = w, bh = w * 3 / 4;
    if (bh > h) { bh = h; bw = h * 4 / 3; }
    int x0 = (w - bw) / 2, y0 = (h - bh) / 2;
    for (int y = 0; y < h; y++) {
        uint32_t *row = px + (long)y * pitch;
        for (int x = 0; x < w; x++) row[x] = 0xFF000000u;
        if (!p || y < y0 || y >= y0 + bh) continue;
        float fy = ((y - y0) + 0.5f) * ph / bh - 0.5f;
        int sy = fy < 0 ? 0 : (int)fy, sy1 = sy + 1 < ph ? sy + 1 : sy;
        float ly = fy < 0 ? 0 : fy - sy;
        for (int x = x0; x < x0 + bw; x++) {
            float fx = ((x - x0) + 0.5f) * pw / bw - 0.5f;
            int sx = fx < 0 ? 0 : (int)fx, sx1 = sx + 1 < pw ? sx + 1 : sx;
            float lx = fx < 0 ? 0 : fx - sx;
            uint32_t a = p[sy * pw + sx], b = p[sy * pw + sx1], c = p[sy1 * pw + sx], d = p[sy1 * pw + sx1], o = 0xFF000000u;
            for (int k = 0; k < 24; k += 8) {
                float v = ((a >> k & 255) * (1 - lx) + (b >> k & 255) * lx) * (1 - ly) + ((c >> k & 255) * (1 - lx) + (d >> k & 255) * lx) * ly;
                o |= (uint32_t)(v + 0.5f) << k;
            }
            row[x] = o;
        }
    }
}

int ob_screen_draw(uint32_t *px, int w, int h, int pitch)
{
    if (!up || !px || w < 1 || h < 1) return 0;
    if (cur.picture) plain_picture(px, w, h, pitch);
    if (cur.draw) cur.draw(cur.ud, px, w, h, pitch);
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
