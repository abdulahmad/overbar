/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The one rendering plan every port uses: what a look means for one frame at one size on screen. Both renderers read it, the OpenGL
 * shader (src/gl.c, the desktop PC ports) and the software one (src/look.c, the SDL_Renderer hosts and any host without GL), so a look
 * draws the same on every port and every device.
 *
 * Sharp is "integer prescale, then bicubic": the picture is taken as if enlarged by a whole number with nearest neighbour (every pixel a
 * block of equal size), and that is resized to the window with a cubic (Catmull-Rom) filter. The prescale never goes above what the view
 * can show (the whole multiple that fits on each axis), because a bicubic filter that shrinks skips and doubles pixels, which is the
 * uneven look the prescale is there to avoid. So 3x on a 1080p screen is 3x, and 3x of a 320x240 picture on a 640x480 handheld is 2x.
 */
#include "overbar/overbar.h"

#include <math.h>
#include <string.h>

static int r_filter = OB_FILTER_CUBIC;

void ob_render_set_filter(const char *name)
{
    if (name && !strcmp(name, "nearest")) r_filter = OB_FILTER_NEAREST;
    else if (name && !strcmp(name, "linear")) r_filter = OB_FILTER_LINEAR;
    else r_filter = OB_FILTER_CUBIC;
}

int ob_render_filter(void) { return r_filter; }

int ob_render_prescale(int want, int src, int view)
{
    int fit;
    if (want < 1) want = 1;
    if (src < 1) return 1;
    fit = view / src;                   /* the biggest whole multiple the view shows without shrinking */
    if (fit < 1) fit = 1;
    return want < fit ? want : fit;
}

/* where a scanline's centre sits in a line drawn over `rows` window rows, and the gain that keeps the line's average brightness */
void ob_render_scan_rows(float sigma, int rows, float *center, float *gain)
{
    float sum = 0;
    int r;
    if (rows < 1) rows = 1;
    *center = ((rows - 1) / 2 + 0.5f) / rows;
    *gain = 1;
    if (sigma <= 0) return;
    for (r = 0; r < rows; r++) {
        float d = ((r + 0.5f) / rows - *center) / sigma;
        sum += expf(-d * d);
    }
    *gain = rows / sum;
}

void ob_render_plan_get(const ob_look *look, ob_class cls, int sw, int sh, int view_w, int view_h, ob_render_plan *p)
{
    static const float scan_strength[3] = { 0.80f, 0.60f, 0.45f };   /* CRT light, medium, strong: the scanline's sigma */
    int lores = cls != OB_CLASS_HIRES, want = 1;
    memset(p, 0, sizeof *p);
    p->period = 1;
    p->gain = 1;
    p->center = 0.5f;
    p->filter = r_filter;
    if (look && look->mode == OB_MODE_CRT) {
        int crt = (int)look->crt;
        if (lores && look->khz31) p->period = 2;            /* 31 kHz: every line twice */
        if (crt <= OB_CRT_STRONG) { p->style = OB_STYLE_SHARP; p->scan = scan_strength[crt < 0 ? 0 : crt]; want = lores ? 3 : 1; }
        else if (crt == OB_CRT_SOFT) p->style = OB_STYLE_SOFT;
        else if (crt == OB_CRT_SOFT_APERTURE) p->style = OB_STYLE_GRILLE;
        else { p->style = OB_STYLE_CUSTOM; want = lores ? 3 : 1; }
        p->filter = OB_FILTER_CUBIC;
    } else if (look && look->mode == OB_MODE_MISTER) {
        if (lores && look->khz31) p->period = 2;
        p->style = OB_STYLE_MISTER;
    } else {                                                /* Sharp (and the modes a host draws as Sharp) */
        p->style = OB_STYLE_SHARP;
        want = look ? ob_sharp_mul(look->sharp) : (lores ? 3 : 1);
    }
    p->pre_x = ob_render_prescale(want, sw, view_w);
    p->pre_y = ob_render_prescale(want, sh, view_h);
    if (p->style == OB_STYLE_SHARP && p->scan > 0) {        /* scanlines: the vertical is per source line, never blended */
        int unit = sh * p->period;
        ob_render_scan_rows(p->scan, unit > 0 ? (view_h + unit / 2) / unit : 1, &p->center, &p->gain);
    } else if (p->style != OB_STYLE_SHARP) {
        int unit = sh * p->period;
        ob_render_scan_rows(0, unit > 0 ? (view_h + unit / 2) / unit : 1, &p->center, &p->gain);
    }
}

void ob_render_weights(int filter, float f, float w[4])
{
    if (filter == OB_FILTER_NEAREST) { w[0] = 0; w[1] = f < 0.5f ? 1.f : 0.f; w[2] = 1.f - w[1]; w[3] = 0; }
    else if (filter == OB_FILTER_LINEAR) { w[0] = 0; w[1] = 1.f - f; w[2] = f; w[3] = 0; }
    else {
        float f2 = f * f, f3 = f2 * f;
        w[0] = -0.5f * f3 + f2 - 0.5f * f;
        w[1] = 1.5f * f3 - 2.5f * f2 + 1.f;
        w[2] = -1.5f * f3 + 2.f * f2 + 0.5f * f;
        w[3] = 0.5f * f3 - 0.5f * f2;
    }
}
