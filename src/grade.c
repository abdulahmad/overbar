/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Movie colour (include/overbar/grade.h): shots found by their colours, each one's correction worked out from
 * its own pictures, applied through three small tables and a saturation step.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/grade.h"

#define TW 64                    /* the thumbnail a cut is found on */
#define TH 56
#define CUT 0.6f                 /* L1 distance of two thumbnails' 8 x 8 x 8 colour histograms */
#define BINS 1024                /* brightness, for the percentiles */
#define GREY 31                  /* "nearly grey": its channels within this (of 255) */

typedef struct {
    double count[BINS], grey[BINS][4];   /* pixels by brightness; the nearly grey ones' R, G, B sums and count */
    double chroma, n;
} stats;

struct ob_grade_maker {
    int w, h, frames, start;
    float prev[512], cur[512];
    int have_prev;
    stats s;
    ob_grade_scene *out;
    int nout, cap;
    int bad;
};

ob_grade_maker *ob_grade_make(int w, int h)
{
    if (w < TW || h < TH) return NULL;
    ob_grade_maker *m = calloc(1, sizeof *m);
    if (m) { m->w = w; m->h = h; }
    return m;
}

static float percentile(const stats *s, double q)
{
    double want = s->n * q / 100, at = 0;
    for (int i = 0; i < BINS; i++) { at += s->count[i]; if (at >= want) return (float)i / (BINS - 1); }
    return 1;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static ob_grade_params params(const stats *s)
{
    ob_grade_params p = { 0, 1, { 1, 1, 1 }, 1 };
    if (s->n < 1) return p;
    float lo = percentile(s, 0.5), hi = percentile(s, 99.8);
    if (hi - lo < 0.08f) return p;                         /* a flat shot (black, a fade): left alone */
    p.lo = lo < 0.12f ? lo : 0.12f;
    p.hi = hi > 0.70f ? hi : 0.70f;
    int from = (int)(percentile(s, 80) * (BINS - 1)) + 1;
    double sum[4] = { 0, 0, 0, 0 };
    for (int i = from; i < BINS; i++) for (int k = 0; k < 4; k++) sum[k] += s->grey[i][k];
    if (sum[3] > 500) {
        double avg[3] = { sum[0] / sum[3], sum[1] / sum[3], sum[2] / sum[3] }, mean = (avg[0] + avg[1] + avg[2]) / 3;
        for (int k = 0; k < 3; k++) p.gain[k] = avg[k] > 0 ? clampf((float)(mean / avg[k]), 0.88f, 1.12f) : 1;
    }
    p.sat = clampf(1 + (0.16f - (float)(s->chroma / s->n)) * 2.5f, 1.0f, 1.35f);
    return p;
}

static int close_scene(ob_grade_maker *m, int end)
{
    if (end <= m->start) return 1;
    if (m->nout == m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        ob_grade_scene *o = realloc(m->out, (size_t)cap * sizeof *o);
        if (!o) return 0;
        m->out = o; m->cap = cap;
    }
    m->out[m->nout++] = (ob_grade_scene){ m->start, end, params(&m->s) };
    m->start = end;
    memset(&m->s, 0, sizeof m->s);
    return 1;
}

int ob_grade_add(ob_grade_maker *m, const uint32_t *f)
{
    if (!m || m->bad) return 0;
    /* the thumbnail's histogram: each cell's average colour */
    memset(m->cur, 0, sizeof m->cur);
    for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) {
        int y0 = ty * m->h / TH, y1 = (ty + 1) * m->h / TH, x0 = tx * m->w / TW, x1 = (tx + 1) * m->w / TW;
        unsigned r = 0, g = 0, b = 0, n = 0;
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            uint32_t p = f[y * m->w + x];
            r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; n++;
        }
        r /= n; g /= n; b /= n;
        m->cur[(r >> 5) * 64 + (g >> 5) * 8 + (b >> 5)] += 1.0f / (TW * TH);
    }
    int cut = 0;
    if (m->have_prev) {
        float d = 0;
        for (int i = 0; i < 512; i++) d += fabsf(m->cur[i] - m->prev[i]);
        cut = d > CUT;
    }
    memcpy(m->prev, m->cur, sizeof m->prev);
    m->have_prev = 1;
    if (cut && !close_scene(m, m->frames)) { m->bad = 1; return 0; }
    /* this frame's pixels (every other one each way) into its shot's numbers */
    stats *s = &m->s;
    for (int y = 0; y < m->h; y += 2) for (int x = 0; x < m->w; x += 2) {
        uint32_t p = f[y * m->w + x];
        int r = p >> 16 & 255, g = p >> 8 & 255, b = p & 255;
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        float Y = (0.299f * r + 0.587f * g + 0.114f * b) / 255;
        int bin = (int)(Y * (BINS - 1) + 0.5f);
        s->count[bin]++;
        s->chroma += (mx - mn) / 255.0;
        s->n++;
        if (mx - mn < GREY) { s->grey[bin][0] += r; s->grey[bin][1] += g; s->grey[bin][2] += b; s->grey[bin][3]++; }
    }
    m->frames++;
    return cut;
}

int ob_grade_end(ob_grade_maker *m, ob_grade_scene **scenes)
{
    int n = -1;
    *scenes = NULL;
    if (m && !m->bad && close_scene(m, m->frames) && m->nout) { *scenes = m->out; n = m->nout; m->out = NULL; }
    if (m) free(m->out);
    free(m);
    return n;
}

/* ---- the file ---- */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

#define GR_VERSION 1
#define GR_REC 32

int ob_grade_save(const char *path, uint32_t source, const ob_grade_scene *s, int n)
{
    char tmp[1100];
    if (!path || n <= 0 || snprintf(tmp, sizeof tmp, "%s.part", path) >= (int)sizeof tmp) return 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    uint8_t hd[16], r[GR_REC];
    memcpy(hd, "OBGR", 4); put32(hd + 4, GR_VERSION); put32(hd + 8, source); put32(hd + 12, (uint32_t)n);
    int ok = fwrite(hd, 1, 16, f) == 16;
    for (int i = 0; ok && i < n; i++) {
        put32(r, (uint32_t)s[i].start); put32(r + 4, (uint32_t)s[i].end);
        put32(r + 8, fbits(s[i].p.lo)); put32(r + 12, fbits(s[i].p.hi));
        for (int k = 0; k < 3; k++) put32(r + 16 + 4 * k, fbits(s[i].p.gain[k]));
        put32(r + 28, fbits(s[i].p.sat));
        ok = fwrite(r, 1, GR_REC, f) == GR_REC;
    }
    ok = !fclose(f) && ok;
    if (ok) { remove(path); ok = !rename(tmp, path); }
    if (!ok) remove(tmp);
    return ok;
}

int ob_grade_load(const char *path, uint32_t source, ob_grade_scene **scenes)
{
    *scenes = NULL;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return -1;
    uint8_t hd[16], r[GR_REC];
    int n = -1;
    if (fread(hd, 1, 16, f) == 16 && !memcmp(hd, "OBGR", 4) && get32(hd + 4) == GR_VERSION && get32(hd + 8) == source
        && get32(hd + 12) > 0 && get32(hd + 12) < 100000) {
        int want = (int)get32(hd + 12);
        ob_grade_scene *s = malloc((size_t)want * sizeof *s);
        int i = 0;
        for (; s && i < want && fread(r, 1, GR_REC, f) == GR_REC; i++) {
            s[i].start = (int)get32(r); s[i].end = (int)get32(r + 4);
            s[i].p.lo = bitsf(get32(r + 8)); s[i].p.hi = bitsf(get32(r + 12));
            for (int k = 0; k < 3; k++) s[i].p.gain[k] = bitsf(get32(r + 16 + 4 * k));
            s[i].p.sat = bitsf(get32(r + 28));
            ob_grade_params *p = &s[i].p;
            if (s[i].end <= s[i].start || !(p->hi - p->lo > 0.01f) || !(p->sat >= 0.5f && p->sat <= 2) || !(p->gain[0] > 0.5f && p->gain[0] < 2)
                || !(p->gain[1] > 0.5f && p->gain[1] < 2) || !(p->gain[2] > 0.5f && p->gain[2] < 2)) break;
        }
        if (s && i == want) { *scenes = s; n = want; }
        else free(s);
    }
    fclose(f);
    return n;
}

/* ---- applied ---- */
void ob_grade_prepare(const ob_grade_params *p, ob_grade_lut *l)
{
    for (int c = 0; c < 3; c++) for (int v = 0; v < 256; v++) {
        float x = (v / 255.0f - p->lo) / (p->hi - p->lo) * p->gain[c];
        if (x < 0) x = 0;
        if (x > 0.8f) x = 0.8f + 0.2f * (1 - expf(-(x - 0.8f) / 0.2f));   /* the soft shoulder */
        l->lut[c][v] = (uint16_t)(x * 4095 + 0.5f);
    }
    l->sat = (int)(p->sat * 256 + 0.5f);
}

void ob_grade_apply(const ob_grade_lut *l, const uint32_t *in, uint32_t *out, long n)
{
    for (long i = 0; i < n; i++) {
        uint32_t p = in[i];
        int r = l->lut[0][p >> 16 & 255], g = l->lut[1][p >> 8 & 255], b = l->lut[2][p & 255];
        int Y = (306 * r + 601 * g + 117 * b) >> 10;            /* (0.299, 0.587, 0.114) */
        r = Y + (((r - Y) * l->sat) >> 8); g = Y + (((g - Y) * l->sat) >> 8); b = Y + (((b - Y) * l->sat) >> 8);
        r = r < 0 ? 0 : r > 4095 ? 4095 : r; g = g < 0 ? 0 : g > 4095 ? 4095 : g; b = b < 0 ? 0 : b > 4095 ? 4095 : b;
        out[i] = (p & 0xFF000000u) | (uint32_t)((r + 8) >> 4 > 255 ? 255 : (r + 8) >> 4) << 16
                 | (uint32_t)((g + 8) >> 4 > 255 ? 255 : (g + 8) >> 4) << 8 | (uint32_t)((b + 8) >> 4 > 255 ? 255 : (b + 8) >> 4);
    }
}

const ob_grade_scene *ob_grade_find(const ob_grade_scene *s, int n, int frame)
{
    int a = 0, b = n - 1;
    while (a <= b) {
        int m = (a + b) / 2;
        if (frame < s[m].start) b = m - 1;
        else if (frame >= s[m].end) a = m + 1;
        else return &s[m];
    }
    return NULL;
}
