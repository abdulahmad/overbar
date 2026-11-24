/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Vector text and shapes for the overlay (include/overbar/vfont.h). */
#include "overbar/vfont.h"
#include "vfont_data.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo fonts[OB_VF_FACES];
static int fonts_ok;

static int fonts_init(void)
{
    const unsigned char *d[OB_VF_FACES] = { ob_vf_light, ob_vf_regular, ob_vf_medium, ob_vf_semibold };
    int i;
    if (fonts_ok) return fonts_ok > 0;
    fonts_ok = 1;
    for (i = 0; i < OB_VF_FACES; i++)
        if (!stbtt_InitFont(&fonts[i], d[i], stbtt_GetFontOffsetForIndex(d[i], 0))) { fonts_ok = -1; break; }
    return fonts_ok > 0;
}

typedef struct {
    int face, px4, cp;              /* the size in quarter pixels */
    int w, h, xo, yo;               /* the glyph's bitmap: its size and its top left from the origin on the baseline */
    int sw, sh, sxo, syo, sblur;    /* the shadow's, blurred */
    float adv;
    uint8_t *cov, *sh_cov;
    int used;
} gly;

#define CACHE 2048
static gly cache[CACHE];
static int cache_n;

void ob_vf_free(void)
{
    int i;
    for (i = 0; i < CACHE; i++) { free(cache[i].cov); free(cache[i].sh_cov); }
    memset(cache, 0, sizeof cache);
    cache_n = 0;
}

static void blur(uint8_t *s, int w, int h, int r)
{
    uint8_t *t = (uint8_t *)malloc((size_t)w * h);
    int p, x, y, k;
    if (!t) return;
    for (p = 0; p < 3; p++) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int a = 0, n = 0;
                for (k = -r; k <= r; k++) if (x + k >= 0 && x + k < w) { a += s[y * w + x + k]; n++; }
                t[y * w + x] = (uint8_t)(a / n);
            }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int a = 0, n = 0;
                for (k = -r; k <= r; k++) if (y + k >= 0 && y + k < h) { a += t[(y + k) * w + x]; n++; }
                s[y * w + x] = (uint8_t)(a / n);
            }
    }
    free(t);
}

/* the glyph for a codepoint at a size, made when it is not kept; shadow blur r pixels (0: no shadow bitmap) */
static gly *glyph(int face, float px, int cp, int r)
{
    int px4 = (int)(px * 4 + 0.5f), i, k;
    unsigned hsh = ((unsigned)face * 73856093u) ^ ((unsigned)px4 * 19349663u) ^ ((unsigned)cp * 83492791u);
    gly *g;
    float sc;
    int x0, y0, x1, y1, adv, lsb;
    for (k = 0; k < CACHE; k++) {
        i = (int)((hsh + (unsigned)k) % CACHE);
        g = &cache[i];
        if (!g->used) break;
        if (g->face == face && g->px4 == px4 && g->cp == cp && (g->sblur == r || r == 0 || g->sblur == 0)) {
            if (r > 0 && g->sblur == 0) break;           /* kept without its shadow: made again below */
            return g;
        }
    }
    if (k == CACHE || cache_n > CACHE * 3 / 4) {       /* full: start again (the next frame refills what it draws) */
        ob_vf_free();
        i = (int)(hsh % CACHE);
        g = &cache[i];
    }
    if (!g->used) cache_n++;
    free(g->cov);
    free(g->sh_cov);
    memset(g, 0, sizeof *g);
    g->used = 1;
    g->face = face;
    g->px4 = px4;
    g->cp = cp;
    sc = stbtt_ScaleForMappingEmToPixels(&fonts[face], px4 / 4.0f);
    stbtt_GetCodepointHMetrics(&fonts[face], cp, &adv, &lsb);
    g->adv = adv * sc;
    stbtt_GetCodepointBitmapBox(&fonts[face], cp, sc, sc, &x0, &y0, &x1, &y1);
    g->w = x1 - x0;
    g->h = y1 - y0;
    g->xo = x0;
    g->yo = y0;
    if (g->w > 0 && g->h > 0) {
        g->cov = (uint8_t *)calloc((size_t)g->w, (size_t)g->h);
        if (g->cov) stbtt_MakeCodepointBitmap(&fonts[face], g->cov, g->w, g->h, g->w, sc, sc, cp);
        if (g->cov && r > 0) {
            int pad = r * 3 + 1, y;
            g->sw = g->w + 2 * pad;
            g->sh = g->h + 2 * pad;
            g->sxo = g->xo - pad;
            g->syo = g->yo - pad;
            g->sblur = r;
            g->sh_cov = (uint8_t *)calloc((size_t)g->sw, (size_t)g->sh);
            if (g->sh_cov) {
                for (y = 0; y < g->h; y++) memcpy(g->sh_cov + (size_t)(y + pad) * g->sw + pad, g->cov + (size_t)y * g->w, (size_t)g->w);
                blur(g->sh_cov, g->sw, g->sh, r);
            }
        }
    }
    if (r > 0 && !g->sblur) g->sblur = r;
    return g;
}

void ob_vf_blend(uint32_t *p, uint32_t rgb, int a)
{
    uint32_t d = *p;
    int da = (int)(d >> 24), sr = (int)(rgb >> 16 & 255), sg = (int)(rgb >> 8 & 255), sb = (int)(rgb & 255), oa, r, g, b;
    if (a <= 0) return;
    if (a > 255) a = 255;
    oa = a + da * (255 - a) / 255;
    if (oa <= 0) return;
    r = (sr * a * 255 + (int)(d >> 16 & 255) * da * (255 - a)) / (oa * 255);
    g = (sg * a * 255 + (int)(d >> 8 & 255) * da * (255 - a)) / (oa * 255);
    b = (sb * a * 255 + (int)(d & 255) * da * (255 - a)) / (oa * 255);
    *p = (uint32_t)oa << 24 | (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}

static int next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p;
    if (c < 0x80) { (*s)++; return c; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { *s += 2; return (c & 0x1F) << 6 | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { *s += 3; return (c & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); }
    (*s)++;
    return '?';
}

static int have(int face, int cp) { return stbtt_FindGlyphIndex(&fonts[face], cp) != 0; }

float ob_vf_width(ob_vf_face face, float px, const char *s, float tracking)
{
    float x = 0, sc;
    int prev = 0;
    if (!s || !fonts_init()) return 0;
    sc = stbtt_ScaleForMappingEmToPixels(&fonts[face], px);
    while (*s) {
        int cp = next_cp(&s), adv, lsb;
        if (!have(face, cp)) cp = '?';
        stbtt_GetCodepointHMetrics(&fonts[face], cp, &adv, &lsb);
        if (prev) x += stbtt_GetCodepointKernAdvance(&fonts[face], prev, cp) * sc;
        x += adv * sc + tracking;
        prev = cp;
    }
    return x - (x > 0 ? tracking : 0);
}

float ob_vf_draw(uint32_t *buf, int w, int h, int pitch, float x, float y, ob_vf_face face, float px, uint32_t rgb, int a,
                 const char *s, float tracking, int shadow)
{
    int pass, prev;
    float sc, x_end = x;
    const char *q;
    if (!s || !fonts_init()) return x;
    sc = stbtt_ScaleForMappingEmToPixels(&fonts[face], px);
    for (pass = shadow > 0 ? 0 : 1; pass < 2; pass++) {
        float cx = x;
        prev = 0;
        for (q = s; *q;) {
            int cp = next_cp(&q);
            gly *g;
            int gx, gy, xx, yy;
            if (!have(face, cp)) cp = '?';
            g = glyph(face, px, cp, shadow > 0 ? shadow : 0);
            if (prev) cx += stbtt_GetCodepointKernAdvance(&fonts[face], prev, cp) * sc;
            if (pass == 0 && g->sh_cov) {
                gx = (int)floorf(cx + shadow + 0.5f) + g->sxo;
                gy = (int)floorf(y + shadow + 0.5f) + g->syo;
                for (yy = 0; yy < g->sh; yy++) {
                    if (gy + yy < 0 || gy + yy >= h) continue;
                    for (xx = 0; xx < g->sw; xx++) {
                        int c = g->sh_cov[yy * g->sw + xx] * 2;
                        if (gx + xx < 0 || gx + xx >= w || !c) continue;
                        ob_vf_blend(&buf[(gy + yy) * pitch + gx + xx], 0x000000, (c > 255 ? 255 : c) * a / 255 * 3 / 4);
                    }
                }
            } else if (pass == 1 && g->cov) {
                gx = (int)floorf(cx + 0.5f) + g->xo;
                gy = (int)floorf(y + 0.5f) + g->yo;
                for (yy = 0; yy < g->h; yy++) {
                    if (gy + yy < 0 || gy + yy >= h) continue;
                    for (xx = 0; xx < g->w; xx++) {
                        int c = g->cov[yy * g->w + xx];
                        if (gx + xx < 0 || gx + xx >= w || !c) continue;
                        ob_vf_blend(&buf[(gy + yy) * pitch + gx + xx], rgb, c * a / 255);
                    }
                }
            }
            cx += g->adv + tracking;
            prev = cp;
        }
        x_end = cx;
    }
    return x_end;
}

/* the distance from a point to a rounded rectangle's edge (negative inside) */
static float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

static void shape(uint32_t *buf, int w, int h, int pitch, int x0, int y0, int x1, int y1, int disc,
                  float cx, float cy, float hw, float hh, float r, uint32_t fill, uint32_t line, float lw)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            float d = disc ? sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - r
                           : sd_rrect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r);
            float cover = 0.5f - d;
            uint32_t *p = &buf[y * pitch + x];
            if (cover <= 0) continue;
            if (cover > 1) cover = 1;
            if (lw > 0 && (line >> 24)) {
                float inner = 0.5f + d + lw;                /* 1 well inside the outline, 0 on it */
                float lc = 1.f - (inner > 1 ? 1 : inner < 0 ? 0 : inner);
                ob_vf_blend(p, fill & 0xFFFFFF, (int)(cover * (fill >> 24)));
                ob_vf_blend(p, line & 0xFFFFFF, (int)(cover * lc * (line >> 24)));
            } else ob_vf_blend(p, fill & 0xFFFFFF, (int)(cover * (fill >> 24)));
        }
}

void ob_vf_rrect(uint32_t *buf, int w, int h, int pitch, float x, float y, float rw, float rh, float r, uint32_t fill, uint32_t line, float lw)
{
    shape(buf, w, h, pitch, (int)floorf(x) - 1, (int)floorf(y) - 1, (int)ceilf(x + rw) + 1, (int)ceilf(y + rh) + 1, 0,
          x + rw / 2, y + rh / 2, rw / 2, rh / 2, r, fill, line, lw);
}

void ob_vf_disc(uint32_t *buf, int w, int h, int pitch, float cx, float cy, float r, uint32_t fill, uint32_t line, float lw)
{
    shape(buf, w, h, pitch, (int)floorf(cx - r) - 1, (int)floorf(cy - r) - 1, (int)ceilf(cx + r) + 1, (int)ceilf(cy + r) + 1, 1,
          cx, cy, r, r, r, fill, line, lw);
}

static float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

void ob_vf_tri(uint32_t *buf, int w, int h, int pitch, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba)
{
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    int x, y, sx, sy;
    float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (y = miny; y < maxy; y++)
        for (x = minx; x < maxx; x++) {
            int in = 0;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    float e0 = edge(x0, y0, x1, y1, px, py) / area, e1 = edge(x1, y1, x2, y2, px, py) / area, e2 = edge(x2, y2, x0, y0, px, py) / area;
                    if (e0 >= 0 && e1 >= 0 && e2 >= 0) in++;
                }
            if (in) ob_vf_blend(&buf[y * pitch + x], rgba & 0xFFFFFF, in * (int)(rgba >> 24) / 16);
        }
}
