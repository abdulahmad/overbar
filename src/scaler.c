/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Destination rectangle. Reuses the nhl95pc sdl2-port video_view approach
 * (largest picture in the window; integer k of a line unit; columns never cut).
 *
 * Console frames (dar_h 240, src_h 224): the pixel shape is the one a 4:3
 * screen of 240 lines gives (PAR 1.0 at 320 wide, 5:4 at 256), but only the
 * 224 real lines are drawn. No 8+8 border: the picture is 4*dar_h : 3*src_h
 * (10:7 at 320x224) and fills the window's height or width. VGA
 * (dar_h == src_h) is plain 4:3.
 */
#include "overbar/overbar.h"

static int gcd_pos(int a, int b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int t = a % b; a = b; b = t; }
    return a ? a : 1;
}

void ob_view(const ob_view_in *in, ob_view_out *out)
{
    int ww = in->window_w > 0 ? in->window_w : 1;
    int wh = in->window_h > 0 ? in->window_h : 1;
    int src_h = in->src_h > 0 ? in->src_h : 1;
    int dar_h = in->dar_h > 0 ? in->dar_h : src_h;
    int vs = in->height;
    int period = in->line_double ? 2 : 1;
    int unit = src_h * period;              /* a whole number of dest rows per source line */
    long an = 4L * dar_h, ad = 3L * src_h;  /* the picture's width : height */
    int w, h;

    /* Fit: the largest picture of that shape in the window. */
    w = ww;
    h = (int)(ww * ad / an);
    if (h > wh) {
        h = wh;
        w = (int)(wh * an / ad);
    }
    if (vs != OB_HEIGHT_FIT) {
        /* Integer / overscan / auto-crop: k dest rows per source line, so
         * scanlines land on whole rows; the width follows the shape. */
        int kf = wh / unit;
        int kc, k, n;
        if (kf < 1) kf = 1;
        while (kf > 1 && (long)kf * unit * an / ad > ww) kf--;
        kc = (wh + unit - 1) / unit;
        if ((long)kc * unit * an / ad > ww) kc = kf;   /* columns never cut */
        if (kc < kf) kc = kf;
        if (vs == OB_HEIGHT_INTEGER) k = kf;
        else if (vs == OB_HEIGHT_OVERSCAN) k = kc;
        else {
            n = in->auto_crop_src_rows;
            if (n <= 0) n = (in->src_w == 320 || dar_h == 240) ? 6 : 10;
            k = (kc * unit - wh <= n * kc * period) ? kc : kf;
        }
        h = k * unit;
        w = (int)(h * an / ad);
    }
    /* The canvas is the picture: nothing is drawn above or below it. */
    out->canvas_w = w;
    out->canvas_h = h;
    out->canvas_x = (ww - w) / 2;
    out->canvas_y = (wh - h) / 2;
    out->src_w = in->src_w;
    out->src_h = src_h;
    out->blit_w = w;
    out->blit_h = h;
    out->blit_x = out->canvas_x;
    out->blit_y = out->canvas_y;
}

void ob_view_par(const ob_view_out *v, int *num, int *den)
{
    /* (blit_w / src_w) / (blit_h / src_h) = blit_w * src_h : blit_h * src_w */
    int n = v->blit_w * v->src_h;
    int d = v->blit_h * v->src_w;
    int g;
    if (d == 0) { *num = 0; *den = 1; return; }
    g = gcd_pos(n, d);
    *num = n / g;
    *den = d / g;
}

void ob_view_from_host(const ob_host *h, ob_class cls, int src_w, int src_h,
                       int window_w, int window_h, ob_view_in *in)
{
    const ob_look *look = (cls == OB_CLASS_HIRES) ? &h->hires : &h->lores;
    in->src_w = src_w;
    in->src_h = src_h;
    in->dar_h = h->console_dar ? 240 : src_h;
    in->window_w = window_w;
    in->window_h = window_h;
    in->height = look->height;
    in->line_double = (cls == OB_CLASS_LORES && ob_mode_has_khz(look->mode) && look->khz31);
    in->auto_crop_src_rows = 0;
}
