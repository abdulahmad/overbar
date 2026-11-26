/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The frame post-process (include/overbar/postfx.h): a movie's console dither evened out, live or from a
 * cache made once. Integers only: the same pixels on every machine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/postfx.h"

const char *const ob_postfx_names[OB_POSTFX_MODES] = { "off", "live", "cached" };

#define DITHER_MAX 96     /* the most two dithered colours differ by (a channel), and the most a pixel takes from a neighbour */
#define R(c) ((int)((c) >> 16 & 255))
#define G(c) ((int)((c) >> 8 & 255))
#define B(c) ((int)((c) & 255))

static int near_rgb(uint32_t a, uint32_t b)
{
    int d0 = abs(R(a) - R(b)), d1 = abs(G(a) - G(b)), d2 = abs(B(a) - B(b));
    return d0 <= DITHER_MAX && d1 <= DITHER_MAX && d2 <= DITHER_MAX;
}

/* 1 where the pixel is part of a dither's pattern: a dot whose four sides are another colour (a checkerboard,
 * or one in four), or a line in a period-two stripe; the colours close enough to stand for one between them */
static int dither_dot(const uint32_t *in, int w, int h, int x, int y)
{
    if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1) return 0;
    const uint32_t *p = in + (long)y * w + x;
    uint32_t c = *p & 0xFFFFFF, l = p[-1] & 0xFFFFFF, r = p[1] & 0xFFFFFF, u = p[-w] & 0xFFFFFF, d = p[w] & 0xFFFFFF;
    if (l != c && r != c && u != c && d != c)                               /* a dot */
        return near_rgb(c, l) && near_rgb(c, r) && near_rgb(c, u) && near_rgb(c, d);
    if (l == r && l != c && u == c && d == c && x >= 2 && x < w - 2)        /* a column of a vertical stripe */
        return (p[-2] & 0xFFFFFF) == c && (p[2] & 0xFFFFFF) == c && near_rgb(c, l);
    if (u == d && u != c && l == c && r == c && y >= 2 && y < h - 2)        /* a row of a horizontal stripe */
        return (p[-2 * w] & 0xFFFFFF) == c && (p[2 * w] & 0xFFFFFF) == c && near_rgb(c, u);
    return 0;
}

/* scratch: 1 where a dither's pattern is within a pixel */
static void dither_zone(const uint32_t *in, uint8_t *zone, int w, int h)
{
    memset(zone, 0, (size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (dither_dot(in, w, h, x, y))
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int yy = y + dy, xx = x + dx;
                        if (yy >= 0 && yy < h && xx >= 0 && xx < w) zone[(long)yy * w + xx] = 1;
                    }
}

/* the 3 x 3 binomial (1 2 1 / 2 4 2 / 1 2 1, /16) at (x, y), a neighbour far from the pixel counting as the pixel:
 * it evens out any period-two pattern exactly and leaves an edge where it is */
static uint32_t binomial(const uint32_t *in, int w, int h, int x, int y)
{
    static const int k[3][3] = { { 1, 2, 1 }, { 2, 4, 2 }, { 1, 2, 1 } };
    uint32_t c = in[(long)y * w + x];
    int sr = 0, sg = 0, sb = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy, xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
            uint32_t n = in[(long)yy * w + xx];
            if (!near_rgb(c, n)) n = c;
            int kk = k[dy + 1][dx + 1];
            sr += kk * R(n); sg += kk * G(n); sb += kk * B(n);
        }
    return (c & 0xFF000000u) | (uint32_t)((sr + 8) >> 4) << 16 | (uint32_t)((sg + 8) >> 4) << 8 | (uint32_t)((sb + 8) >> 4);
}

void ob_dedither_live(const uint32_t *in, uint32_t *out, uint8_t *scratch, int w, int h)
{
    dither_zone(in, scratch, w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            long i = (long)y * w + x;
            out[i] = scratch[i] ? binomial(in, w, h, x, y) : in[i];
        }
}

/* ---- the cached quality ---- */
struct ob_dedither_hq {
    int w, h, W, H;
    int have_prev;
    uint8_t *zone;              /* w x h */
    uint8_t *a[3], *b[3];       /* W x H planes (r, g, b): the steps go a -> b -> a ... */
    uint8_t *prev[3];           /* the last frame's out */
    uint32_t *dist, *box;       /* W x H: a shift's squared difference, and its 3 x 3 sum */
    uint32_t *wsum, *acc[3];    /* non-local means' weights and sums */
    uint32_t *out;
};

#define NLM_SEARCH 2            /* 5 x 5 */
#define NLM_H2 (3 * 9 * 14 * 14)   /* a patch's squared difference (3 channels x 9 pixels) of "the same" */
#define SHARPEN 2               /* (c - blur) / SHARPEN added */
#define TIME_NEAR 12            /* a channel that moved this little since the last frame is smoothed with it */

ob_dedither_hq *ob_dedither_hq_new(int w, int h)
{
    ob_dedither_hq *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    q->w = w; q->h = h; q->W = 2 * w; q->H = 2 * h;
    size_t n = (size_t)q->W * q->H;
    int ok = (q->zone = malloc((size_t)w * h)) && (q->dist = malloc(n * 4)) && (q->box = malloc(n * 4)) && (q->wsum = malloc(n * 4))
             && (q->out = malloc(n * 4));
    for (int c = 0; c < 3; c++)
        ok = ok && (q->a[c] = malloc(n)) && (q->b[c] = malloc(n)) && (q->prev[c] = malloc(n)) && (q->acc[c] = malloc(n * 4));
    if (!ok) { ob_dedither_hq_free(q); return NULL; }
    return q;
}

void ob_dedither_hq_free(ob_dedither_hq *q)
{
    if (!q) return;
    free(q->zone); free(q->dist); free(q->box); free(q->wsum); free(q->out);
    for (int c = 0; c < 3; c++) { free(q->a[c]); free(q->b[c]); free(q->prev[c]); free(q->acc[c]); }
    free(q);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 1. up 2x: in a dither's zone each pixel four times then (2) the 1 2 2 2 1 low-pass, which has nothing left
 * of a period two or four pattern at 2x; elsewhere bilinear, edges and all */
static void hq_up(ob_dedither_hq *q, const uint32_t *in)
{
    int w = q->w, h = q->h, W = q->W, H = q->H;
    dither_zone(in, q->zone, w, h);
    for (int Y = 0; Y < H; Y++)
        for (int X = 0; X < W; X++) {
            uint32_t c = in[(long)(Y >> 1) * w + (X >> 1)];
            q->a[0][(long)Y * W + X] = (uint8_t)R(c); q->a[1][(long)Y * W + X] = (uint8_t)G(c); q->a[2][(long)Y * W + X] = (uint8_t)B(c);
        }
    static const int k[5] = { 1, 2, 2, 2, 1 };
    for (int ch = 0; ch < 3; ch++) {
        uint8_t *s = q->a[ch], *t = q->b[ch];
        for (int Y = 0; Y < H; Y++)                                 /* across, near neighbours only */
            for (int X = 0; X < W; X++) {
                int c = s[(long)Y * W + X], sum = 0;
                for (int i = -2; i <= 2; i++) {
                    int v = s[(long)Y * W + clampi(X + i, 0, W - 1)];
                    sum += k[i + 2] * (abs(v - c) <= DITHER_MAX ? v : c);
                }
                t[(long)Y * W + X] = (uint8_t)((sum + 4) >> 3);
            }
        for (int Y = 0; Y < H; Y++)                                 /* down, back into a where the zone is */
            for (int X = 0; X < W; X++) {
                long i = (long)Y * W + X;
                if (!q->zone[(long)(Y >> 1) * w + (X >> 1)]) continue;
                int c = t[i], sum = 0;
                for (int j = -2; j <= 2; j++) {
                    int v = t[(long)clampi(Y + j, 0, H - 1) * W + X];
                    sum += k[j + 2] * (abs(v - c) <= DITHER_MAX ? v : c);
                }
                s[i] = (uint8_t)((sum + 4) >> 3);
            }
    }
    for (int Y = 0; Y < H; Y++)                                     /* the rest: bilinear from the source's centres */
        for (int X = 0; X < W; X++) {
            if (q->zone[(long)(Y >> 1) * w + (X >> 1)]) continue;
            int x0 = (X - 1) >> 1, y0 = (Y - 1) >> 1, fx = (X & 1) ? 1 : 3, fy = (Y & 1) ? 1 : 3;   /* (quarters) */
            int x1 = clampi(x0 + 1, 0, w - 1), y1 = clampi(y0 + 1, 0, h - 1);
            x0 = clampi(x0, 0, w - 1); y0 = clampi(y0, 0, h - 1);
            uint32_t p00 = in[(long)y0 * w + x0], p01 = in[(long)y0 * w + x1], p10 = in[(long)y1 * w + x0], p11 = in[(long)y1 * w + x1];
            int w00 = (4 - fx) * (4 - fy), w01 = fx * (4 - fy), w10 = (4 - fx) * fy, w11 = fx * fy;
            long i = (long)Y * W + X;
            q->a[0][i] = (uint8_t)((w00 * R(p00) + w01 * R(p01) + w10 * R(p10) + w11 * R(p11) + 8) >> 4);
            q->a[1][i] = (uint8_t)((w00 * G(p00) + w01 * G(p01) + w10 * G(p10) + w11 * G(p11) + 8) >> 4);
            q->a[2][i] = (uint8_t)((w00 * B(p00) + w01 * B(p01) + w10 * B(p10) + w11 * B(p11) + 8) >> 4);
        }
}

/* 3. non-local means over a: 5 x 5 shifts, 3 x 3 patches, all three channels; into b */
static void hq_nlm(ob_dedither_hq *q)
{
    int W = q->W, H = q->H;
    long n = (long)W * H;
    memset(q->wsum, 0, (size_t)n * 4);
    for (int c = 0; c < 3; c++) memset(q->acc[c], 0, (size_t)n * 4);
    for (int dy = -NLM_SEARCH; dy <= NLM_SEARCH; dy++)
        for (int dx = -NLM_SEARCH; dx <= NLM_SEARCH; dx++) {
            for (int Y = 0; Y < H; Y++) {
                int Ys = clampi(Y + dy, 0, H - 1);
                for (int X = 0; X < W; X++) {
                    long i = (long)Y * W + X, j = (long)Ys * W + clampi(X + dx, 0, W - 1);
                    int d0 = q->a[0][i] - q->a[0][j], d1 = q->a[1][i] - q->a[1][j], d2 = q->a[2][i] - q->a[2][j];
                    q->dist[i] = (uint32_t)(d0 * d0 + d1 * d1 + d2 * d2);
                }
            }
            for (int Y = 0; Y < H; Y++)                             /* 3 x 3 sums: across, then down */
                for (int X = 0; X < W; X++) {
                    long i = (long)Y * W;
                    q->box[i + X] = q->dist[i + clampi(X - 1, 0, W - 1)] + q->dist[i + X] + q->dist[i + clampi(X + 1, 0, W - 1)];
                }
            for (int Y = 0; Y < H; Y++) {
                int Ys = clampi(Y + dy, 0, H - 1);
                for (int X = 0; X < W; X++) {
                    long i = (long)Y * W + X;
                    uint32_t ssd = q->box[(long)clampi(Y - 1, 0, H - 1) * W + X] + q->box[i] + q->box[(long)clampi(Y + 1, 0, H - 1) * W + X];
                    if (ssd >= NLM_H2) continue;
                    uint32_t f = NLM_H2 - ssd, wt = (uint32_t)(((uint64_t)f * f * 256) / ((uint64_t)NLM_H2 * NLM_H2));   /* (1 - d/h)^2, 0-256 */
                    if (!wt) continue;
                    long j = (long)Ys * W + clampi(X + dx, 0, W - 1);
                    q->wsum[i] += wt;
                    for (int c = 0; c < 3; c++) q->acc[c][i] += wt * q->a[c][j];
                }
            }
        }
    for (long i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) q->b[c][i] = q->wsum[i] ? (uint8_t)((q->acc[c][i] + q->wsum[i] / 2) / q->wsum[i]) : q->a[c][i];
}

/* 4. a light sharpen of b into a: c + (c - binomial) / SHARPEN */
static void hq_sharpen(ob_dedither_hq *q)
{
    int W = q->W, H = q->H;
    for (int c = 0; c < 3; c++)
        for (int Y = 0; Y < H; Y++)
            for (int X = 0; X < W; X++) {
                const uint8_t *s = q->b[c];
                int ym = clampi(Y - 1, 0, H - 1) * W, y0 = Y * W, yp = clampi(Y + 1, 0, H - 1) * W, xm = clampi(X - 1, 0, W - 1), xp = clampi(X + 1, 0, W - 1);
                int blur = (s[ym + xm] + 2 * s[ym + X] + s[ym + xp] + 2 * s[y0 + xm] + 4 * s[y0 + X] + 2 * s[y0 + xp] + s[yp + xm] + 2 * s[yp + X] + s[yp + xp] + 8) >> 4;
                int v = s[y0 + X];
                q->a[c][y0 + X] = (uint8_t)clampi(v + (v - blur) / SHARPEN, 0, 255);
            }
}

const uint32_t *ob_dedither_hq_frame(ob_dedither_hq *q, const uint32_t *in)
{
    long n = (long)q->W * q->H;
    hq_up(q, in);
    hq_nlm(q);
    hq_sharpen(q);
    for (long i = 0; i < n; i++) {                                  /* 5. over time: what hardly moved, a quarter of the last frame */
        uint32_t px = 0xFF000000u;
        for (int c = 0; c < 3; c++) {
            int v = q->a[c][i];
            if (q->have_prev && abs(v - q->prev[c][i]) <= TIME_NEAR) v = (3 * v + q->prev[c][i] + 2) >> 2;
            q->prev[c][i] = (uint8_t)v;
            px |= (uint32_t)v << (16 - 8 * c);
        }
        q->out[i] = px;
    }
    q->have_prev = 1;
    return q->out;
}

/* ---- the cache file: "OBDD", version, w, h, source, frames, the index's place, then the frames, then the
 * index (each frame's place). Little-endian. A frame is YCoCg (luma at full size, the two chroma planes at
 * half), each sample guessed from its left, upper and upper-left neighbours (LOCO-I's median), the miss
 * rounded to a step of 2 * NEAR + 1 (so no sample is off by more than NEAR) and written as an adaptive Rice
 * code: about a twentieth of the frame's bytes for these smooth pictures. ---- */
#define DD_VERSION 4                   /* (4: back to version 2's pass after 3's wide sharpen; an older file is made again) */
#define DD_HEAD 28
#define NEAR 1
#define STEP (2 * NEAR + 1)

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { uint8_t *p; long n, cap; uint32_t acc; int bits; int bad; } bits_t;

static void put_bits(bits_t *b, uint32_t v, int n)        /* n <= 24 */
{
    b->acc = b->acc << n | (v & ((1u << n) - 1));
    b->bits += n;
    while (b->bits >= 8) {
        b->bits -= 8;
        if (b->n < b->cap) b->p[b->n++] = (uint8_t)(b->acc >> b->bits);
        else b->bad = 1;
    }
}

static uint32_t get_bits(bits_t *b, int n)
{
    while (b->bits < n) {
        b->acc = b->acc << 8 | (b->n < b->cap ? b->p[b->n] : 0);
        if (b->n++ >= b->cap) b->bad = 1;
        b->bits += 8;
    }
    b->bits -= n;
    return b->acc >> b->bits & ((1u << n) - 1);
}

/* the planes of a 2w x 2h frame: y (W x H), co and cg (W/2 x H/2), all 0-511 */
typedef struct { int W, H; int16_t *pl[3]; } planes_t;

static int plane_w(const planes_t *p, int k) { return k ? p->W / 2 : p->W; }
static int plane_h(const planes_t *p, int k) { return k ? p->H / 2 : p->H; }

static int planes_new(planes_t *p, int W, int H)
{
    p->W = W; p->H = H;
    p->pl[0] = malloc((size_t)W * H * 2);
    p->pl[1] = malloc((size_t)(W / 2) * (H / 2) * 2);
    p->pl[2] = malloc((size_t)(W / 2) * (H / 2) * 2);
    return p->pl[0] && p->pl[1] && p->pl[2];
}

static void planes_free(planes_t *p) { for (int k = 0; k < 3; k++) free(p->pl[k]); }

static void to_planes(const uint32_t *px, planes_t *p)
{
    int W = p->W, H = p->H;
    for (int y = 0; y < H; y += 2)
        for (int x = 0; x < W; x += 2) {
            int sco = 0, scg = 0;
            for (int k = 0; k < 4; k++) {
                long i = (long)(y + (k >> 1)) * W + x + (k & 1);
                int r = R(px[i]), g = G(px[i]), b = B(px[i]);
                int co = r - b, t = b + (co >> 1), cg = g - t;
                p->pl[0][i] = (int16_t)(t + (cg >> 1));
                sco += co; scg += cg;
            }
            long c = (long)(y / 2) * (W / 2) + x / 2;
            p->pl[1][c] = (int16_t)(((sco + 2) >> 2) + 256);
            p->pl[2][c] = (int16_t)(((scg + 2) >> 2) + 256);
        }
}

static void from_planes(const planes_t *p, uint32_t *px)
{
    int W = p->W, H = p->H;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            long c = (long)(y / 2) * (W / 2) + x / 2;
            int co = p->pl[1][c] - 256, cg = p->pl[2][c] - 256, t = p->pl[0][(long)y * W + x] - (cg >> 1);
            int g = clampi(cg + t, 0, 255), b = t - (co >> 1), r = clampi(b + co, 0, 255);
            px[(long)y * W + x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)clampi(b, 0, 255);
        }
}

static int med(int a, int b, int c)
{
    int mx = a > b ? a : b, mn = a < b ? a : b;
    return c >= mx ? mn : c <= mn ? mx : a + b - c;
}

/* one plane, coded (enc 1: the samples are rounded to what the reader will get) or read back (enc 0) */
static void code_plane(bits_t *b, int16_t *s, int w, int h, int top, int enc)
{
    uint32_t A = 4, N = 1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            long i = (long)y * w + x;
            int a = x ? s[i - 1] : y ? s[i - w] : top / 2, bb = y ? s[i - w] : a, c = x && y ? s[i - w - 1] : bb;
            int pred = med(a, bb, c), k = 0;
            while ((N << k) < A && k < 16) k++;
            int q;
            if (enc) {
                int e = s[i] - pred;
                q = e >= 0 ? (e + NEAR) / STEP : -((NEAR - e) / STEP);
                uint32_t m = q >= 0 ? (uint32_t)q * 2 : (uint32_t)(-q) * 2 - 1;
                uint32_t hi = m >> k;
                if (hi < 24) { put_bits(b, 0, (int)hi); put_bits(b, 1, 1); }
                else { put_bits(b, 0, 24); put_bits(b, 1, 1); put_bits(b, m, 16); k = -1; }   /* (an escape: the number itself) */
                if (k > 0) put_bits(b, m, k);
            } else {
                uint32_t hi = 0, m;
                while (!get_bits(b, 1) && hi < 25 && !b->bad) hi++;
                if (hi >= 24) m = get_bits(b, 16);
                else m = hi << k | (k ? get_bits(b, k) : 0);
                q = m & 1 ? -(int)((m + 1) / 2) : (int)(m / 2);
            }
            s[i] = (int16_t)clampi(pred + q * STEP, 0, top - 1);
            A += (uint32_t)abs(q);
            if (++N >= 64) { A = (A + 1) / 2; N /= 2; }
        }
}

static long encode_frame(planes_t *p, uint8_t *out, long cap)
{
    bits_t b = { out, 0, cap, 0, 0, 0 };
    for (int k = 0; k < 3; k++) code_plane(&b, p->pl[k], plane_w(p, k), plane_h(p, k), k ? 512 : 256, 1);
    if (b.bits) put_bits(&b, 0, 8 - b.bits);
    return b.bad ? -1 : b.n;
}

static int decode_frame(planes_t *p, uint8_t *in, long len)
{
    bits_t b = { in, 0, len, 0, 0, 0 };
    for (int k = 0; k < 3; k++) code_plane(&b, p->pl[k], plane_w(p, k), plane_h(p, k), k ? 512 : 256, 0);
    return !b.bad;
}

struct ob_ddcache {
    FILE *f;
    int w, h, frames;
    uint32_t *index;            /* frames + 1 places */
    planes_t pl;
    uint8_t *buf;
    long cap;
    uint32_t *out;
};

/* a file of frames fw x fh: "OBDD" a de-dither cache (frames 2x its movie's), "OBFR" frames as they were given */
static ob_ddcache *open_file(const char *path, const char *magic, int fw, int fh, uint32_t source)
{
    uint8_t hd[DD_HEAD];
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return NULL;
    ob_ddcache *c = calloc(1, sizeof *c);
    if (!c || fread(hd, 1, DD_HEAD, f) != DD_HEAD || memcmp(hd, magic, 4) || get32(hd + 4) != DD_VERSION || get32(hd + 8) != (uint32_t)fw
        || get32(hd + 12) != (uint32_t)fh || get32(hd + 16) != source || !get32(hd + 20) || get32(hd + 20) > 100000) goto bad;
    c->f = f;
    c->w = fw; c->h = fh;
    c->frames = (int)get32(hd + 20);
    c->cap = (long)c->w * c->h * 8;
    uint8_t *ix = malloc((size_t)c->frames * 4);
    c->index = malloc(((size_t)c->frames + 1) * 4);
    c->buf = malloc((size_t)c->cap);
    c->out = malloc((size_t)c->w * c->h * 4);
    int ok = ix && c->index && c->buf && c->out && planes_new(&c->pl, c->w, c->h) && !fseek(f, (long)get32(hd + 24), SEEK_SET)
             && fread(ix, 4, (size_t)c->frames, f) == (size_t)c->frames;
    for (int i = 0; ok && i < c->frames; i++) c->index[i] = get32(ix + 4 * i);
    if (ok) c->index[c->frames] = get32(hd + 24);
    for (int i = 0; ok && i < c->frames; i++) ok = c->index[i] >= DD_HEAD && c->index[i] <= c->index[i + 1] && (long)(c->index[i + 1] - c->index[i]) <= c->cap;
    free(ix);
    if (!ok) goto bad;
    return c;
bad:
    if (c) { c->f = NULL; ob_ddcache_close(c); }
    fclose(f);
    return NULL;
}

ob_ddcache *ob_ddcache_open(const char *path, int w, int h, uint32_t source) { return open_file(path, "OBDD", 2 * w, 2 * h, source); }
ob_ddcache *ob_frames_open(const char *path, int w, int h, uint32_t source) { return open_file(path, "OBFR", w, h, source); }

int ob_ddcache_frames(const ob_ddcache *c) { return c ? c->frames : 0; }

const uint32_t *ob_ddcache_read(ob_ddcache *c, int index)
{
    if (!c || index < 0 || index >= c->frames) return NULL;
    long len = (long)(c->index[index + 1] - c->index[index]);
    if (fseek(c->f, (long)c->index[index], SEEK_SET) || fread(c->buf, 1, (size_t)len, c->f) != (size_t)len || !decode_frame(&c->pl, c->buf, len)) return NULL;
    from_planes(&c->pl, c->out);
    return c->out;
}

void ob_ddcache_close(ob_ddcache *c)
{
    if (!c) return;
    if (c->f) fclose(c->f);
    planes_free(&c->pl);
    free(c->index); free(c->buf); free(c->out);
    free(c);
}

struct ob_ddcache_maker {
    FILE *f;
    char path[1024], tmp[1040];
    int frames, cap;
    uint32_t *index, pos;
    ob_dedither_hq *q;
    planes_t pl;
    uint8_t *buf;
    long bufcap;
    uint8_t head[DD_HEAD];
};

/* frames fw x fh into a file; hq: each comes in at half that and goes through ob_dedither_hq first */
static ob_ddcache_maker *make_file(const char *path, const char *magic, int fw, int fh, uint32_t source, int hq)
{
    ob_ddcache_maker *m = calloc(1, sizeof *m);
    if (!m || !path || strlen(path) >= sizeof m->path) { free(m); return NULL; }
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->tmp, sizeof m->tmp, "%s.part", path);
    memcpy(m->head, magic, 4);
    put32(m->head + 4, DD_VERSION); put32(m->head + 8, (uint32_t)fw); put32(m->head + 12, (uint32_t)fh); put32(m->head + 16, source);
    m->bufcap = (long)fw * fh * 8;
    m->q = hq ? ob_dedither_hq_new(fw / 2, fh / 2) : NULL;
    m->buf = malloc((size_t)m->bufcap);
    int ok = planes_new(&m->pl, fw, fh);
    m->f = fopen(m->tmp, "wb");
    m->pos = DD_HEAD;
    if (!ok || (hq && !m->q) || !m->buf || !m->f || fwrite(m->head, 1, DD_HEAD, m->f) != DD_HEAD) { ob_ddcache_end(m, 0); return NULL; }
    return m;
}

ob_ddcache_maker *ob_ddcache_make(const char *path, int w, int h, uint32_t source) { return make_file(path, "OBDD", 2 * w, 2 * h, source, 1); }
ob_ddcache_maker *ob_ddcache_make_done(const char *path, int w, int h, uint32_t source) { return make_file(path, "OBDD", 2 * w, 2 * h, source, 0); }
ob_ddcache_maker *ob_frames_make(const char *path, int w, int h, uint32_t source) { return make_file(path, "OBFR", w, h, source, 0); }

int ob_ddcache_add(ob_ddcache_maker *m, const uint32_t *frame)
{
    if (!m || !m->f) return 0;
    if (m->frames == m->cap) {
        int cap = m->cap ? m->cap * 2 : 1024;
        uint32_t *ix = realloc(m->index, (size_t)cap * 4);
        if (!ix) return 0;
        m->index = ix;
        m->cap = cap;
    }
    to_planes(m->q ? ob_dedither_hq_frame(m->q, frame) : frame, &m->pl);
    long len = encode_frame(&m->pl, m->buf, m->bufcap);
    if (len < 0 || fwrite(m->buf, 1, (size_t)len, m->f) != (size_t)len) return 0;
    m->index[m->frames++] = m->pos;
    m->pos += (uint32_t)len;
    return 1;
}

int ob_ddcache_end(ob_ddcache_maker *m, int ok)
{
    if (!m) return 0;
    if (m->f) {
        uint8_t b4[4];
        ok = ok && m->frames > 0;
        for (int i = 0; ok && i < m->frames; i++) { put32(b4, m->index[i]); ok = fwrite(b4, 1, 4, m->f) == 4; }
        put32(m->head + 20, (uint32_t)m->frames);
        put32(m->head + 24, m->pos);
        ok = ok && !fseek(m->f, 0, SEEK_SET) && fwrite(m->head, 1, DD_HEAD, m->f) == DD_HEAD;
        ok = !fclose(m->f) && ok;
        if (ok) { remove(m->path); ok = !rename(m->tmp, m->path); }
        if (!ok) remove(m->tmp);
    } else ok = 0;
    ob_dedither_hq_free(m->q);
    planes_free(&m->pl);
    free(m->index); free(m->buf);
    free(m);
    return ok;
}

/* ---- the stage ---- */
struct ob_postfx {
    int w, h;
    uint32_t source;
    char *cache_path;
    ob_ddcache *cache;
    int looked;                 /* frames since it last looked for the cache */
    uint32_t *live;
    uint8_t *scratch;
};

ob_postfx *ob_postfx_new(int w, int h, const char *cache_path, uint32_t source)
{
    ob_postfx *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->w = w; p->h = h; p->source = source;
    p->live = malloc((size_t)w * h * 4);
    p->scratch = malloc((size_t)w * h);
    if (cache_path && (p->cache_path = malloc(strlen(cache_path) + 1))) strcpy(p->cache_path, cache_path);
    if (!p->live || !p->scratch) { ob_postfx_free(p); return NULL; }
    return p;
}

int ob_postfx_has_cache(ob_postfx *p)
{
    if (!p) return 0;
    if (!p->cache && p->cache_path) p->cache = ob_ddcache_open(p->cache_path, p->w, p->h, p->source);
    p->looked = 0;
    return p->cache != NULL;
}

const uint32_t *ob_postfx_run(ob_postfx *p, int mode, int index, const uint32_t *in, int *ow, int *oh)
{
    *ow = p ? p->w : 0;
    *oh = p ? p->h : 0;
    if (!p || mode == OB_POSTFX_OFF) return in;
    if (mode == OB_POSTFX_CACHED) {
        if (!p->cache && p->cache_path && ++p->looked >= 30) ob_postfx_has_cache(p);   /* (one being made now) */
        const uint32_t *c = p->cache ? ob_ddcache_read(p->cache, index) : NULL;
        if (c) { *ow = 2 * p->w; *oh = 2 * p->h; return c; }
    }
    ob_dedither_live(in, p->live, p->scratch, p->w, p->h);
    return p->live;
}

void ob_postfx_free(ob_postfx *p)
{
    if (!p) return;
    ob_ddcache_close(p->cache);
    free(p->cache_path); free(p->live); free(p->scratch);
    free(p);
}

uint32_t ob_postfx_hash(const uint8_t *data, long n)
{
    uint32_t h = 2166136261u;
    for (long i = 0; i < n; i++) h = (h ^ data[i]) * 16777619u;
    return h;
}
