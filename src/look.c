/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The CRT looks and the MiSTer filters in software, for the hosts that draw with SDL_Renderer
 * (Genesis, SNES). It is the NHL95PC sdl2-port fragment shader (port/video_gl.c) worked out on
 * the CPU for every pixel of the picture as the window shows it:
 *
 *   CRT light, medium, strong   sharp columns (cubic over a 3x nearest grid), a Gaussian
 *                               scanline per line, its gain keeping the brightness
 *   CRT soft                    after Timothy Lottes' CRT shader: a Gaussian beam over the
 *                               three nearest lines and pixels, in linear light
 *   CRT soft + aperture grille  the same with an RGB grille every three window columns
 *   CRT custom                  the c_ fields of the look: gap darkness, gamma, mask, sharpness
 *   MiSTer                      the Filters, Gamma and Shadow_Masks files (look_tables.h)
 *
 * Every filter is split in two: a horizontal pass per source line at the output width, then the
 * vertical weights per output row. The weights that depend only on a column or a row are worked
 * out once a frame.
 */
#include "overbar/overbar.h"
#include "look_tables.h"

#include <math.h>
#ifdef OB_SDL_BASE
#include <SDL.h>
#endif
#include <stdlib.h>
#include <string.h>

#define MAX_PX (1920L * 1100L)  /* above this, a whole fraction of the picture */

/* Fixed point throughout, so the loops stay integer: a colour channel is 1.0 = ONE (10 bits),
 * a linear-light one 1.0 = LONE (14 bits), a weight 1.0 = WONE (12 bits). */
#define ONE  1024
#define LONE 16384
#define WONE 4096

static uint32_t *o_px;          /* the output */
static long o_cap;
static int16_t *o_rows;         /* horizontal pass: out_w x 3 per source line */
static long o_rows_cap;
static int16_t o_lin[256];      /* sRGB byte to linear, LONE */
static uint8_t o_srgb[4096];    /* linear (LONE >> 2) to an sRGB byte */
static int o_pow_lut[ONE * 2 + 1];   /* the custom look's gamma: ONE in, 4096 out */
static float o_pow_for = -1;
static int o_luts;

typedef struct { int x[4]; int w[4]; } col_taps;   /* weights WONE */
static col_taps *o_cols;
static long o_cols_cap;

static int grow(void **p, long *cap, long need, size_t size)
{
    void *n;
    if (need <= *cap) return 1;
    n = realloc(*p, (size_t)need * size);
    if (!n) return 0;
    *p = n;
    *cap = need;
    return 1;
}

static void luts(void)
{
    int i;
    if (o_luts) return;
    for (i = 0; i < 256; i++) o_lin[i] = (int16_t)(powf(i / 255.f, 2.2f) * LONE + 0.5f);
    for (i = 0; i < 4096; i++) o_srgb[i] = (uint8_t)(powf((i + 0.5f) / 4096.f, 1.f / 2.2f) * 255.f + 0.5f);
    o_luts = 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float gaus(float p, float s) { return exp2f(s * p * p); }
static int wfix(float w) { return (int)floorf(w * WONE + 0.5f); }

/* the horizontal pass: for source line y, each output column's taps of the line */
static void hpass(const uint32_t *line, int ow, int lin, int16_t *out)
{
    int ox, k, t;
    for (ox = 0; ox < ow; ox++) {
        const col_taps *c = &o_cols[ox];
        int a[3] = { 0, 0, 0 };
        for (k = 0; k < 4; k++) {
            uint32_t p = line[c->x[k]];
            int w = c->w[k];
            if (!w) continue;
            if (lin) {
                a[0] += o_lin[(p >> 16) & 255] * w;
                a[1] += o_lin[(p >> 8) & 255] * w;
                a[2] += o_lin[p & 255] * w;
            } else {                                /* (a byte to ONE: x 4) */
                a[0] += (int)((p >> 16) & 255) * 4 * w;
                a[1] += (int)((p >> 8) & 255) * 4 * w;
                a[2] += (int)(p & 255) * 4 * w;
            }
        }
        for (t = 0; t < 3; t++) out[ox * 3 + t] = (int16_t)clampi((a[t] + WONE / 2) >> 12, -32768, 32767);
    }
}

/* the shader's cubic (Catmull-Rom) weights mixed with linear ones by sharp (1: all cubic) over a
 * grid of pre output-texels per source pixel: the taps i-1..i+2 of that grid, as source columns */
static void cols_sharp(int sw, int ow, int pre, float sharp)
{
    int ox, k;
    for (ox = 0; ox < ow; ox++) {
        float p = (ox + 0.5f) / ow * sw * pre - 0.5f, i = floorf(p), f = p - i;
        float f2 = f * f, f3 = f2 * f;
        float cub[4] = { -0.5f * f3 + f2 - 0.5f * f, 1.5f * f3 - 2.5f * f2 + 1.f,
                         -1.5f * f3 + 2.f * f2 + 0.5f * f, 0.5f * f3 - 0.5f * f2 };
        float lin[4] = { 0.f, 1.f - f, f, 0.f };
        for (k = 0; k < 4; k++) {
            int g = (int)i - 1 + k;
            o_cols[ox].x[k] = clampi((int)floorf((float)g / pre), 0, sw - 1);
            o_cols[ox].w[k] = wfix(lin[k] + (cub[k] - lin[k]) * sharp);
        }
    }
}

/* Lottes' beam across: the three nearest pixels, Gaussian, normalised */
static void cols_soft(int sw, int ow)
{
    int ox, k;
    for (ox = 0; ox < ow; ox++) {
        float pos = (ox + 0.5f) / ow * sw, q = floorf(pos), dst = 0.5f - (pos - q), w[3], sum = 0;
        for (k = 0; k < 3; k++) {
            o_cols[ox].x[k] = clampi((int)q - 1 + k, 0, sw - 1);
            w[k] = gaus(dst - 1.f + k, -3.f);
            sum += w[k];
        }
        for (k = 0; k < 3; k++) o_cols[ox].w[k] = wfix(w[k] / sum);
        o_cols[ox].x[3] = o_cols[ox].x[2];
        o_cols[ox].w[3] = 0;
    }
}

/* the MiSTer horizontal filter: 64 phases of 4 taps */
static void cols_mister(int sw, int ow, const float (*htap)[4])
{
    int ox, k;
    for (ox = 0; ox < ow; ox++) {
        float p = (ox + 0.5f) / ow * sw - 0.5f, i = floorf(p), f = p - i;
        int ph = clampi((int)(f * 64.f), 0, 63);
        for (k = 0; k < 4; k++) {
            o_cols[ox].x[k] = clampi((int)i - 1 + k, 0, sw - 1);
            o_cols[ox].w[k] = wfix(htap[ph][k]);
        }
    }
}

/* 95 PC's scan_rows: where the scanline's centre sits in a line of rows output rows, and the gain
 * that keeps the line as bright as it was */
static void scan_rows(float sigma, int rows, float *center, float *gain)
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

static const int16_t *row_of(int line, int sh, int ow)
{
    return o_rows + (long)clampi(line, 0, sh - 1) * ow * 3;
}

static uint32_t pack(int r, int g, int b)        /* bytes, clamped */
{
    return 0xFF000000u | (uint32_t)clampi(r, 0, 255) << 16 | (uint32_t)clampi(g, 0, 255) << 8 | (uint32_t)clampi(b, 0, 255);
}

int ob_look_renders(const ob_look *look)
{
    return look && (look->mode == OB_MODE_CRT || look->mode == OB_MODE_MISTER);
}

/* one frame's work, shared by the threads that draw bands of it */
typedef struct {
    const ob_look *look;
    const uint32_t *src;
    int sw, sh, spitch, ow, oh, d, period, style, lin;
    float center, gain, sigma, bright;
    const int *fx;                  /* each output column's window column (gl_FragCoord.x) */
    const ob_mask_tile *mask;
    const ob_mister_tables *mt;
    int gam[256];
} job;

static int *o_fx;
static long o_fx_cap;

static void hrows(const job *j, int y0, int y1)
{
    int y;
    for (y = y0; y < y1; y++)
        hpass(j->src + (long)y * j->spitch, j->ow, j->lin, o_rows + (long)y * j->ow * 3);
}

static void vrows(const job *j, int oy0, int oy1)
{
    static const int grille[3][3] = {           /* (1.4 or 0.6) x 1.15, 1/256 */
        { 412, 177, 177 }, { 177, 412, 177 }, { 177, 177, 412 }
    };
    const ob_look *look = j->look;
    int sh = j->sh, ow = j->ow, period = j->period, oy, ox, t, k;
    for (oy = oy0; oy < oy1; oy++) {
        uint32_t *out = o_px + (long)oy * ow;
        float v = (oy + 0.5f) / j->oh;                /* the shader's uv.y */
        int fy = (int)((oy + 0.5f) * j->d);           /* the window row (gl_FragCoord.y), for the masks */
        if (j->style == 0) {
            float sy = v * sh * period, dd = (sy - floorf(sy) - j->center) / j->sigma;
            const int16_t *r = row_of((int)floorf(sy / period), sh, ow);
            int K = (int)(expf(-dd * dd) * j->gain * 255.f * 256.f + 0.5f), i;
            for (ox = 0, i = 0; ox < ow; ox++, i += 3)  /* (ONE x 255*256 >> 18: a byte) */
                out[ox] = pack((r[i] * K + (1 << 17)) >> 18, (r[i + 1] * K + (1 << 17)) >> 18, (r[i + 2] * K + (1 << 17)) >> 18);
        } else if (j->style == 4) {
            float sy = v * sh * period, dd = fabsf(sy - floorf(sy) - j->center) * 2.f, cm = look->c_mask;
            const int16_t *r = row_of((int)floorf(sy / period), sh, ow);
            int K, lv[2], par = fy & 1;
            lv[0] = (int)((1.f - cm) * 256.f + 0.5f);
            lv[1] = (int)((1.f + 2.f * cm) * 256.f + 0.5f);
            if (dd > 1.f) dd = 1.f;
            K = (int)((1.f - look->c_dark * powf(dd, 1.5f)) * WONE + 0.5f);
            for (ox = 0; ox < ow; ox++) {
                int m = (j->fx[ox] + par) % 3, c[3], i = ox * 3;
                for (t = 0; t < 3; t++)
                    c[t] = ((o_pow_lut[clampi((r[i + t] * K) >> 12, 0, 2 * ONE)] * lv[t == m]) >> 8) * 255 >> 12;
                out[ox] = pack(c[0], c[1], c[2]);
            }
        } else if (j->style == 1 || j->style == 2) {
            float py = v * sh * period, q = floorf(py), dy = j->center - (py - q);
            const int16_t *r[3];
            int w[3];
            for (k = 0; k < 3; k++) {
                r[k] = row_of((int)floorf((q - 1.f + k) / period), sh, ow);
                w[k] = wfix(gaus(dy - 1.f + k, -7.f));
            }
            for (ox = 0; ox < ow; ox++) {
                int c[3], i = ox * 3;
                for (t = 0; t < 3; t++) c[t] = (r[0][i + t] * w[0] + r[1][i + t] * w[1] + r[2][i + t] * w[2]) >> 12;
                if (j->style == 2) {
                    const int *g = grille[j->fx[ox] % 3];
                    for (t = 0; t < 3; t++) c[t] = (c[t] * g[t]) >> 8;
                }
                out[ox] = 0xFF000000u | (uint32_t)o_srgb[clampi(c[0] >> 2, 0, 4095)] << 16
                        | (uint32_t)o_srgb[clampi(c[1] >> 2, 0, 4095)] << 8 | o_srgb[clampi(c[2] >> 2, 0, 4095)];
            }
        } else {
            const ob_mask_tile *mask = j->mask;
            float py = v * sh * period - 0.5f + (0.5f - j->center), iy = floorf(py), f = py - iy;
            int ph = clampi((int)(f * 256.f), 0, 255), my, wd[4], wb[4], fi = (int)(f * 256.f + 0.5f), ms, cell;
            int F[16][3];
            const int16_t *r[4];
            ms = look->mask_scale > 0 ? look->mask_scale : 1;
            for (k = 0; k < 4; k++) {
                r[k] = row_of((int)floorf((iy - 1.f + k) / period), sh, ow);
                wd[k] = wfix(j->mt->vtap[0][ph][k]);
                wb[k] = wfix(j->mt->vtap[1][ph][k]);
            }
            my = (fy / ms) % mask->h;
            for (cell = 0; cell < mask->w; cell++)      /* this row's mask cells x the brightness, 1/256 */
                for (t = 0; t < 3; t++) F[cell][t] = (int)(mask->rgb[(my * mask->w + cell) * 3 + t] * j->bright * 256.f + 0.5f);
            for (ox = 0; ox < ow; ox++) {
                int i = ox * 3, lum, w[4], c[3];
                const int *m = F[(j->fx[ox] / ms) % mask->w];
                lum = ((r[1][i] * (256 - fi) + r[2][i] * fi) * 77 + (r[1][i + 1] * (256 - fi) + r[2][i + 1] * fi) * 150
                       + (r[1][i + 2] * (256 - fi) + r[2][i + 2] * fi) * 29) >> 18;   /* (ONE x 256 x 256 >> 18: 0..256) */
                lum = clampi(lum, 0, 256);
                for (k = 0; k < 4; k++) w[k] = wd[k] + (((wb[k] - wd[k]) * lum) >> 8);
                for (t = 0; t < 3; t++) {
                    int sm = (r[0][i + t] * w[0] + r[1][i + t] * w[1] + r[2][i + t] * w[2] + r[3][i + t] * w[3]) >> 12;
                    c[t] = ((j->gam[clampi(sm * 255 >> 10, 0, 255)] * m[t]) >> 8) * 255 >> 12;
                }
                out[ox] = pack(c[0], c[1], c[2]);
            }
        }
    }
}

/* Bands of a frame on SDL threads (the hosts that link SDL), else all here. */
#ifdef OB_SDL_BASE
#define MAX_THREADS 8
static int o_nthreads = -1;
static SDL_sem *o_go[MAX_THREADS], *o_done;
static const job *o_job;
static int o_phase;

static void band(int which, int n)
{
    const job *j = o_job;
    if (o_phase == 0) hrows(j, j->sh * which / n, j->sh * (which + 1) / n);
    else vrows(j, j->oh * which / n, j->oh * (which + 1) / n);
}

static int worker(void *arg)
{
    int which = (int)(intptr_t)arg;
    for (;;) {
        SDL_SemWait(o_go[which]);
        band(which, o_nthreads);
        SDL_SemPost(o_done);
    }
    return 0;
}

static void threads_start(void)
{
    int n = SDL_GetCPUCount(), i;
    if (n > MAX_THREADS) n = MAX_THREADS;
    o_nthreads = 1;
    if (n < 2 || !(o_done = SDL_CreateSemaphore(0))) return;
    for (i = 1; i < n; i++) {
        SDL_Thread *t;
        if (!(o_go[i] = SDL_CreateSemaphore(0))) break;
        t = SDL_CreateThread(worker, "overbar look", (void *)(intptr_t)i);
        if (!t) { SDL_DestroySemaphore(o_go[i]); break; }
        SDL_DetachThread(t);
        o_nthreads = i + 1;
    }
}

static void run(const job *j, int phase)
{
    int i;
    if (o_nthreads < 0) threads_start();
    o_job = j;
    o_phase = phase;
    for (i = 1; i < o_nthreads; i++) SDL_SemPost(o_go[i]);
    band(0, o_nthreads);
    for (i = 1; i < o_nthreads; i++) SDL_SemWait(o_done);
}
#else
static void run(const job *j, int phase)
{
    if (phase == 0) hrows(j, 0, j->sh);
    else vrows(j, 0, j->oh);
}
#endif

int ob_look_render(const ob_look *look, const uint32_t *src, int sw, int sh, int spitch,
                   int out_w, int out_h, uint32_t **dst, int *dw, int *dh)
{
    static const float scan_strength[3] = { 0.80f, 0.60f, 0.45f };   /* light, medium, strong */
    int ow = out_w, oh = out_h, d = 1, crt, ox, k;
    ob_mister_tables mt;
    job j;
    if (!ob_look_renders(look) || !src || sw < 1 || sh < 1 || out_w < 1 || out_h < 1) return 0;
    if (spitch < sw) spitch = sw;
    while ((long)(ow = out_w / d) * (oh = out_h / d) > MAX_PX) d++;
    if (ow < 1 || oh < 1) return 0;
    luts();
    memset(&j, 0, sizeof j);
    j.look = look;
    j.src = src;
    j.sw = sw;
    j.sh = sh;
    j.spitch = spitch;
    j.ow = ow;
    j.oh = oh;
    j.d = d;
    j.period = look->khz31 ? 2 : 1;
    crt = look->mode == OB_MODE_CRT ? (int)look->crt : -1;
    /* 0 scanlines, 1 soft, 2 soft + grille, 3 MiSTer, 4 custom (video_gl.c's style) */
    j.style = crt < 0 ? 3 : crt <= OB_CRT_STRONG ? 0 : crt == OB_CRT_SOFT ? 1 : crt == OB_CRT_SOFT_APERTURE ? 2 : 4;
    j.lin = j.style == 1 || j.style == 2;
    if (j.style == 0) j.sigma = scan_strength[crt];
    scan_rows(j.sigma, (oh + sh * j.period / 2) / (sh * j.period), &j.center, &j.gain);
    if (!grow((void **)&o_px, &o_cap, (long)ow * oh, sizeof *o_px)
        || !grow((void **)&o_cols, &o_cols_cap, ow, sizeof *o_cols)
        || !grow((void **)&o_fx, &o_fx_cap, ow, sizeof *o_fx)
        || !grow((void **)&o_rows, &o_rows_cap, (long)sh * ow * 3, sizeof *o_rows))
        return 0;
    for (ox = 0; ox < ow; ox++) o_fx[ox] = (int)((ox + 0.5f) * d);
    j.fx = o_fx;
    if (j.style == 3) {
        int i, best = 0;
        ob_mister_tables_get(look, &mt);
        for (i = 0; i < mt.nmasks; i++)        /* the largest pattern not above the output height */
            if (mt.masks[i].res <= out_h && mt.masks[i].res >= mt.masks[best].res) best = i;
        j.mask = &mt.masks[best];
        j.mt = &mt;
        j.bright = look->bright > 0 ? look->bright / 100.f : 1.f;
        for (k = 0; k < 256; k++) j.gam[k] = (int)(mt.gamma[k] * 4096.f + 0.5f);
        cols_mister(sw, ow, mt.htap);
    } else if (j.lin) cols_soft(sw, ow);
    else cols_sharp(sw, ow, 3, j.style == 4 ? look->c_sharp : 1.f);
    if (j.style == 4 && look->c_gamma != o_pow_for) {
        int i;
        float g = look->c_gamma > 0.05f ? look->c_gamma : 1.f;
        for (i = 0; i <= 2 * ONE; i++) o_pow_lut[i] = (int)(powf(i > ONE ? 1.f : i / (float)ONE, 1.f / g) * 4096.f + 0.5f);
        o_pow_for = look->c_gamma;
    }
    run(&j, 0);
    run(&j, 1);
    *dst = o_px;
    *dw = ow;
    *dh = oh;
    return 1;
}
