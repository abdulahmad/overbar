/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The box module: the screen, time, tweens, 2D drawing, hot spots, the page reader, sound, settings. */
#include "overbar/box.h"
#ifdef OB_SDL_BASE                 /* (the SDL client build only: its own audio device and timer) */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


bx_state BX;
r3_scene *BX_S;
const bx_scene *BX_SC;
bx_canvas BX_C;
float bx_look[3];
static float zoom = 1, cam_real[3];       /* the wheel's or a pinch's zoom: the camera moved toward what it looks at (a new view starts at 1) */
static float zoom_to = 1;                 /* where the zoom is going: it eases there over a few frames */
static float pan[2];                      /* the camera (and what it looks at) moved sideways / up, in view heights at what it looks at */
static void view_reset(void) { zoom = zoom_to = 1; pan[0] = pan[1] = 0; }
static void cam_unzoom(void) { if (cam_real[2] != 0) { memcpy(BX_S->cam, cam_real, sizeof cam_real); cam_real[2] = 0; } }

static struct {
    ob_host *host;
    char game[16], base[1024], data[1100], art[1100], art_ini[1100], ini[1100];
    int show_pref;                 /* 0 first launch only, 1 never, 2 every launch */
    int seen, loaded, up;
    r3_scene scene;
    r3_target tgt;
    uint32_t *bg; int bg_w, bg_h;
    uint32_t *rpx; int rcap;
    uint32_t *c3; long c3cap; int c3w, c3h; unsigned c3sig; int c3full, c3ok;   /* the last 3D picture at window size, and what it shows */
    float dyn; float r3ms; int r3w, r3h;   /* while things move: the share of the pixels drawn (to keep up), the last 3D picture's cost and size */
    int gpu;                 /* the last 3D picture came from the GPU (r3gl) */
    int mpan;                /* the middle button is held: the mouse pans */
    int fps, fps_n; double fps_t;
    double last;
    /* input */
    int mx, my, mouse_in, down, dragging, down_x, down_y, last_x, last_y, down_node, kb;
    float fade, fade_dir; void (*fade_fn)(int); int fade_arg;
    char pref_label[64];
    int focus_first;               /* highlight the first hot spot once the scene has some (after a key or pad pick) */
} B;

double bx_now(void) { return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

/* ---------------- tweens and timers ---------------- */
typedef struct { float *v; int n; float from[4], to[4]; float ms, t; int started; } tween;
typedef struct { float t; void (*fn)(int); int arg; } timer;
static tween TW[256]; static int ntw;
static timer TM[64]; static int ntm;
float bx_ease(float t) { return t < .5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2; }
void bx_tw(float *v, int n, const float *to, float ms, float delay)
{
    if (!v || n < 1 || n > 4) return;
    for (int i = 0; i < ntw; i++)                  /* a newer tween of the same values replaces the older one */
        if (TW[i].v == v && TW[i].n == n && TW[i].started) { TW[i] = TW[--ntw]; break; }
    if (ntw >= 256) return;
    tween *w = &TW[ntw++];
    memset(w, 0, sizeof *w);
    w->v = v; w->n = n; memcpy(w->to, to, (size_t)n * sizeof(float));
    w->ms = ms < 1 ? 1 : ms; w->t = -delay;
}
void bx_tw3(float *v, float x, float y, float z, float ms, float delay) { float t[3] = { x, y, z }; bx_tw(v, 3, t, ms, delay); }
void bx_tw1(float *v, float to, float ms, float delay) { bx_tw(v, 1, &to, ms, delay); }
void bx_after(float ms, void (*fn)(int), int arg) { if (ntm < 64) { TM[ntm].t = ms; TM[ntm].fn = fn; TM[ntm].arg = arg; ntm++; } }
void bx_time_clear(void) { ntw = 0; ntm = 0; }
static void time_step(float dt)
{
    for (int i = 0; i < ntw; i++) {
        tween *w = &TW[i];
        w->t += dt;
        if (w->t < 0) continue;
        if (!w->started) { memcpy(w->from, w->v, (size_t)w->n * sizeof(float)); w->started = 1; }
        float p = w->t / w->ms; if (p > 1) p = 1;
        float e = bx_ease(p);
        for (int k = 0; k < w->n; k++) w->v[k] = w->from[k] + (w->to[k] - w->from[k]) * e;
        if (p >= 1) { TW[i] = TW[--ntw]; i--; }
    }
    for (int i = 0; i < ntm; i++) {
        TM[i].t -= dt;
        if (TM[i].t <= 0) { timer t = TM[i]; TM[i] = TM[--ntm]; i--; t.fn(t.arg); }
    }
}

/* ---------------- scene helpers ---------------- */
r3_node *bx_n(int i) { static r3_node dummy; return i >= 0 && BX_S && i < BX_S->nnodes ? &BX_S->nodes[i] : &dummy; }
int bx_node(const char *name)
{
    int i = BX_S ? r3_find(BX_S, name) : -1;
    if (i < 0) fprintf(stderr, "box: no node %s\n", name);
    return i;
}
void bx_parent(int node, int parent) { if (node >= 0 && node != parent) { BX_S->nodes[node].parent = parent; BX_S->stamp++; } }
void bx_attach(int node, int parent) { r3_attach(BX_S, node, parent); }
void bx_set3(float *v, float x, float y, float z) { v[0] = x; v[1] = y; v[2] = z; }
void bx_cam(float x, float y, float z) { view_reset(); cam_real[2] = 0; BX_S->cam[0] = x; BX_S->cam[1] = y; BX_S->cam[2] = z; }
void bx_cam_to(float x, float y, float z, float ms) { cam_unzoom(); view_reset(); bx_tw3(BX_S->cam, x, y, z, ms, 0); }

/* ---------------- 2D ---------------- */
static uint32_t mix(uint32_t d, uint32_t s, int a)
{
    if (a >= 255) return s | 0xff000000u;
    if (a <= 0) return d;
    uint32_t r = (((d >> 16) & 255) * (255 - a) + ((s >> 16) & 255) * a) / 255;
    uint32_t g = (((d >> 8) & 255) * (255 - a) + ((s >> 8) & 255) * a) / 255;
    uint32_t b = ((d & 255) * (255 - a) + (s & 255) * a) / 255;
    return 0xff000000u | r << 16 | g << 8 | b;
}
void bx_rect(float x, float y, float w, float h, uint32_t argb)
{
    int x0 = (int)(x + .5f), y0 = (int)(y + .5f), x1 = (int)(x + w + .5f), y1 = (int)(y + h + .5f), a = argb >> 24;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BX_C.w) x1 = BX_C.w;
    if (y1 > BX_C.h) y1 = BX_C.h;
    for (int yy = y0; yy < y1; yy++) { uint32_t *r = BX_C.px + (size_t)yy * BX_C.pitch; for (int xx = x0; xx < x1; xx++) r[xx] = mix(r[xx], argb, a); }
}
void bx_rrect(float x, float y, float w, float h, float r, uint32_t fill, uint32_t line, float lw)
{
    ob_vf_rrect(BX_C.px, BX_C.w, BX_C.h, BX_C.pitch, x, y, w, h, r, fill, line, lw);
}
static ob_vf_face face_of(int f) { return f == 2 ? OB_VF_SEMIBOLD : f == 1 ? OB_VF_MEDIUM : OB_VF_REGULAR; }
float bx_text_w(float size, const char *s, int face) { return ob_vf_width(face_of(face), size, s, 0); }
void bx_text(float x, float y, float size, uint32_t rgb, int a, const char *s, int face, int align)
{
    if (align >= 0) { float w = bx_text_w(size, s, face); x -= align == 0 ? w / 2 : w; }
    ob_vf_draw(BX_C.px, BX_C.w, BX_C.h, BX_C.pitch, x, y, face_of(face), size, rgb, a, s, 0, size > 14 ? 2 : 1);
}
/* "<b>bold</b> rest" centred at cx */
void bx_rich(float cx, float y, float size, const char *s, uint32_t plain, uint32_t bold)
{
    char seg[8][160]; int segb[8], n = 0;
    const char *p = s;
    while (*p && n < 8) {
        int b = 0;
        if (!strncmp(p, "<b>", 3)) { b = 1; p += 3; }
        const char *e = strstr(p, b ? "</b>" : "<b>");
        size_t L = e ? (size_t)(e - p) : strlen(p);
        if (L > 159) L = 159;
        memcpy(seg[n], p, L); seg[n][L] = 0; segb[n] = b; n++;
        p += L; if (b && e) p += 4;
    }
    float tw = 0;
    for (int i = 0; i < n; i++) tw += bx_text_w(size, seg[i], segb[i] ? 2 : 0);
    float x = cx - tw / 2;
    for (int i = 0; i < n; i++) {
        bx_text(x, y, size, segb[i] ? bold : plain, 255, seg[i], segb[i] ? 2 : 0, -1);
        x += bx_text_w(size, seg[i], segb[i] ? 2 : 0);
    }
}
/* a picture into a rect, bilinear from the mip level nearest its size */
void bx_image(const r3_tex *t, float x, float y, float w, float h, float alpha)
{
    if (!t || !t->loaded || w < 1 || h < 1) { bx_rect(x, y, w, h, 0xff2a2a2a); return; }
    int lv = 0;
    while (lv + 1 < t->nlv && t->lw[lv + 1] >= w && t->lh[lv + 1] >= h) lv++;
    const uint32_t *src = t->lv[lv]; int sw = t->lw[lv], sh = t->lh[lv];
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h), a8 = (int)(alpha * 255);
    for (int yy = y0 < 0 ? 0 : y0; yy < y1 && yy < BX_C.h; yy++) {
        float fy = (yy + .5f - y) / h * sh - .5f; int iy = (int)floorf(fy); float ay = fy - iy;
        int ya = iy < 0 ? 0 : iy >= sh ? sh - 1 : iy, yb = iy + 1 < 0 ? 0 : iy + 1 >= sh ? sh - 1 : iy + 1;
        uint32_t *row = BX_C.px + (size_t)yy * BX_C.pitch;
        for (int xx = x0 < 0 ? 0 : x0; xx < x1 && xx < BX_C.w; xx++) {
            float fx = (xx + .5f - x) / w * sw - .5f; int ix = (int)floorf(fx); float ax = fx - ix;
            int xa = ix < 0 ? 0 : ix >= sw ? sw - 1 : ix, xb = ix + 1 < 0 ? 0 : ix + 1 >= sw ? sw - 1 : ix + 1;
            uint32_t p00 = src[ya * sw + xa], p10 = src[ya * sw + xb], p01 = src[yb * sw + xa], p11 = src[yb * sw + xb];
            uint32_t o = 0;
            for (int sh2 = 0; sh2 < 32; sh2 += 8) {
                float v = ((p00 >> sh2) & 255) * (1 - ax) * (1 - ay) + ((p10 >> sh2) & 255) * ax * (1 - ay) + ((p01 >> sh2) & 255) * (1 - ax) * ay + ((p11 >> sh2) & 255) * ax * ay;
                o |= (uint32_t)(v + .5f) << sh2;
            }
            row[xx] = mix(row[xx], o & 0xffffff, ((o >> 24) * a8) / 255);
        }
    }
}
void bx_pill(float cx, float y, const char *s)
{
    float sz = 1.4f * BX_C.u, w = bx_text_w(sz, s, 0) + 2.2f * BX_C.u, h = sz * 1.7f;
    if (cx - w / 2 < BX_C.sx + BX_C.u) cx = BX_C.sx + BX_C.u + w / 2;
    if (cx + w / 2 > BX_C.sx + BX_C.sw - BX_C.u) cx = BX_C.sx + BX_C.sw - BX_C.u - w / 2;
    bx_rrect(cx - w / 2, y, w, h, h / 2, 0xdd000000, 0, 0);
    bx_text(cx, y + h * 0.68f, sz, 0xf4ecdf, 255, s, 0, 0);
}

static void make_bg(int w, int h);
/* ---------------- hot spots ---------------- */
#define HOT_MAX 48
static bx_hot HOT[HOT_MAX]; static int nhot;
static int hover = -1;          /* index into HOT under the mouse */
void bx_hot_clear(void) { nhot = 0; }
void bx_hot_node(int node, const char *label)
{
    if (node < 0 || nhot >= HOT_MAX) return;
    memset(&HOT[nhot], 0, sizeof HOT[0]);
    HOT[nhot].node = node; HOT[nhot].label = label ? label : bx_n(node)->label; nhot++;
}
static int hot_is_kb(int i) { return B.kb >= 0 && B.kb == i; }
void bx_button(float x, float y, const char *label, void (*fn)(int), int arg, int align)
{
    float sz = 1.5f * BX_C.u, w = bx_text_w(sz, label, 0) + 2.6f * BX_C.u, h = sz * 2.0f;
    if (align > 0) x -= w; else if (align == 0) x -= w / 2;
    int i = nhot < HOT_MAX ? nhot++ : -1;
    int lit = 0;
    if (i >= 0) {
        memset(&HOT[i], 0, sizeof HOT[0]);
        HOT[i].node = -1; HOT[i].x = x; HOT[i].y = y; HOT[i].w = w; HOT[i].h = h; HOT[i].label = label; HOT[i].fn = fn; HOT[i].arg = arg; HOT[i].btn = 1;
        HOT[i].noauto = label[0] == '<' || !strncmp(label, "Skip", 4) || !strcmp(label, "Close");
        lit = hot_is_kb(i) || (B.mouse_in && B.mx >= x && B.mx < x + w && B.my >= y && B.my < y + h);
    }
    bx_rrect(x, y, w, h, .4f * BX_C.u, lit ? 0xff4a3b2e : 0xff3a2e24, lit ? 0xffffd25a : 0xff5a4a3b, lit ? .25f * BX_C.u : 1);
    bx_text(x + w / 2, y + h * 0.66f, sz, 0xf4ecdf, 255, label, 0, 0);
}
int bx_hot_rect(float x, float y, float w, float h, const char *label, void (*fn)(int), int arg)
{
    int i = nhot < HOT_MAX ? nhot++ : -1;
    if (i < 0) return 0;
    memset(&HOT[i], 0, sizeof HOT[0]);
    HOT[i].node = -1; HOT[i].x = x; HOT[i].y = y; HOT[i].w = w; HOT[i].h = h; HOT[i].label = label; HOT[i].fn = fn; HOT[i].arg = arg; HOT[i].btn = 1;
    int lit = hot_is_kb(i) || (B.mouse_in && B.mx >= x && B.mx < x + w && B.my >= y && B.my < y + h);
    if (lit && label) bx_pill(x + w / 2, y + h + .4f * BX_C.u, label);
    return lit;
}
void bx_desk(void)
{
    make_bg(BX_C.w, BX_C.h);
    if (B.bg) for (int y = 0; y < BX_C.h; y++) memcpy(BX_C.px + (size_t)y * BX_C.pitch, B.bg + (size_t)y * BX_C.w, (size_t)BX_C.w * 4);
}
int bx_hovered_node(void)
{
    if (B.kb >= 0 && B.kb < nhot && HOT[B.kb].node >= 0) return HOT[B.kb].node;
    if (hover >= 0 && hover < nhot && HOT[hover].node >= 0) return HOT[hover].node;
    return -1;
}

/* which hot spot is under a window point */
static int hot_at(int x, int y)
{
    for (int i = nhot - 1; i >= 0; i--)
        if (HOT[i].btn && x >= HOT[i].x && x < HOT[i].x + HOT[i].w && y >= HOT[i].y && y < HOT[i].y + HOT[i].h) return i;
    if ((!B.tgt.pick && !B.gpu) || !B.tgt.w) return -1;
    int tx = (int)((float)x * B.tgt.w / BX_C.w), ty = (int)((float)y * B.tgt.h / BX_C.h);
    if (tx < 0 || ty < 0 || tx >= B.tgt.w || ty >= B.tgt.h) return -1;
    int nd = B.gpu ? r3gl_pick(BX_S, B.tgt.w, B.tgt.h, tx, ty) : B.tgt.pick[(size_t)ty * B.tgt.w + tx];
    if (nd < 0) return -1;
    /* the deepest hot node that owns the picked mesh */
    int best = -1, depth = -1;
    for (int i = 0; i < nhot; i++) {
        if (HOT[i].node < 0 || !r3_is_under(BX_S, nd, HOT[i].node)) continue;
        int d = 0; for (int k = HOT[i].node; k >= 0; k = BX_S->nodes[k].parent) d++;
        if (d > depth) { depth = d; best = i; }
    }
    return best;
}

/* ---------------- fade ---------------- */
void bx_fade(void (*fn)(int), int arg) { B.fade_fn = fn; B.fade_arg = arg; B.fade_dir = 1; }
static void fade_step(float dt)
{
    if (B.fade_dir > 0) {
        B.fade += dt / 700.0f;
        if (B.fade >= 1) {
            B.fade = 1; B.fade_dir = -1;
            void (*fn)(int) = B.fade_fn; B.fade_fn = NULL;
            int had_kb = B.kb >= 0 || B.focus_first;
            if (fn) fn(B.fade_arg);
            B.kb = -1; B.focus_first = had_kb;           /* a new scene: the keys start from its first spot */
            B.fade_dir = -0.0001f;                     /* hold dark one beat */
        }
    } else if (B.fade_dir < 0) {
        if (B.fade_dir > -0.5f) { B.fade_dir -= dt / 150.0f; if (B.fade_dir <= -0.5f) B.fade_dir = -1; return; }
        B.fade -= dt / 700.0f;
        if (B.fade <= 0) { B.fade = 0; B.fade_dir = 0; }
    }
}

/* ---------------- art ---------------- */
typedef struct { char key[64]; r3_tex t; int tried; } art_ent;
static art_ent ART[160]; static int nart;
static uint32_t *art_load(const char *key, int *w, int *h, void *ud)
{
    (void)ud;
    static const char *ext[] = { ".jpg", ".png", ".jpeg" };
    char p[1300];
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s%s", B.art, key, ext[i]);
        uint32_t *o = r3_load_image(p, w, h);
        if (o) return o;
    }
    return NULL;
}
r3_tex *bx_art(const char *key)
{
    for (int i = 0; i < nart; i++) if (!strcmp(ART[i].key, key)) return ART[i].t.loaded ? &ART[i].t : NULL;
    if (nart >= 160) return NULL;
    art_ent *a = &ART[nart++];
    memset(a, 0, sizeof *a);
    snprintf(a->key, sizeof a->key, "%s", key);
    a->t.rep[0] = a->t.rep[1] = 1;
    int w, h; uint32_t *px = art_load(key, &w, &h, NULL);
    if (px) r3_tex_set(&a->t, px, w, h);
    return a->t.loaded ? &a->t : NULL;
}

/* ---------------- the page reader ---------------- */
#define VIEW_MAX 200
static struct {
    int up, i, n, spread, zoom; float scroll; r3_tex *pg[VIEW_MAX]; char title[96]; void (*close)(int);
    int keyed; char key[VIEW_MAX][32];                    /* pages by art key, read when shown (bx_view_open_keys) */
    struct { int page; unsigned used; r3_tex t; } slot[4]; unsigned tick;
} V;
static void view_slots_free(void) { for (int k = 0; k < 4; k++) { r3_tex_set(&V.slot[k].t, NULL, 0, 0); V.slot[k].page = -1; } }
/* page i: from the list, or read from the art folder into one of four slots (the least recently shown one goes) */
static r3_tex *view_page(int i)
{
    if (i < 0 || i >= V.n) return NULL;
    if (!V.keyed) return V.pg[i];
    int k, old = 0;
    V.tick++;
    for (k = 0; k < 4; k++) if (V.slot[k].page == i) { V.slot[k].used = V.tick; return V.slot[k].t.loaded ? &V.slot[k].t : NULL; }
    for (k = 1; k < 4; k++) if (V.slot[k].used < V.slot[old].used) old = k;
    int w, h; uint32_t *px = art_load(V.key[i], &w, &h, NULL);
    r3_tex_set(&V.slot[old].t, px, w, h);
    V.slot[old].t.rep[0] = V.slot[old].t.rep[1] = 1;
    V.slot[old].page = i; V.slot[old].used = V.tick;
    return V.slot[old].t.loaded ? &V.slot[old].t : NULL;
}
int bx_art_exists(const char *key)
{
    static const char *ext[] = { ".jpg", ".png", ".jpeg" };
    char p[1300];
    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/%s%s", B.art, key, ext[i]); FILE *f = fopen(p, "rb"); if (f) { fclose(f); return 1; } }
    return 0;
}
int bx_view_up(void) { return V.up; }
static void view_start(const char *title, int n, int spread, void (*on_close)(int))
{
    view_slots_free();
    V.n = n; V.i = 0; V.spread = spread; V.close = on_close; V.up = 1; V.zoom = 0; V.scroll = 0;
    snprintf(V.title, sizeof V.title, "%s", title);
    B.kb = -1;
}
void bx_view_open(const char *title, r3_tex **pages, int n, int spread, void (*on_close)(int))
{
    if (n > VIEW_MAX) n = VIEW_MAX;
    V.keyed = 0;
    for (int i = 0; i < n; i++) V.pg[i] = pages[i];
    view_start(title, n, spread, on_close);
}
void bx_view_open_keys(const char *title, const char *const *keys, int n, int spread, void (*on_close)(int))
{
    if (n > VIEW_MAX) n = VIEW_MAX;
    V.keyed = 1;
    for (int i = 0; i < n; i++) snprintf(V.key[i], sizeof V.key[i], "%s", keys[i]);
    view_start(title, n, spread, on_close);
}
static void view_next(int a) { (void)a; int nx = V.i == 0 ? 1 : V.i + (V.spread ? 2 : 1); if (nx < V.n) { V.i = nx; V.scroll = 0; bx_sfx(SFX_PAGE, 0); } }
static void view_prev(int a) { (void)a; int nx = V.spread ? (V.i <= 2 ? 0 : V.i - 2) : V.i - 1; if (nx >= 0 && nx != V.i) { V.i = nx; V.scroll = 0; bx_sfx(SFX_PAGE, 0); } }
static void view_zoom(int a) { (void)a; V.zoom = !V.zoom; V.scroll = 0; }
static void view_close(int a) { (void)a; V.up = 0; B.kb = -1; view_slots_free(); if (V.close) V.close(0); }
static void view_draw(void)
{
    bx_rect(0, 0, BX_C.w, BX_C.h, 0xdd000000);
    float u = BX_C.u;
    int two = V.spread && V.i > 0 && V.i + 1 < V.n;
    r3_tex *a = view_page(V.i), *b = two ? view_page(V.i + 1) : NULL;
    float ph = BX_C.sh * 0.74f, top = BX_C.sy + BX_C.sh * 0.08f;
    float ar = a && a->loaded ? (float)a->w / a->h : 0.77f, br = b && b->loaded ? (float)b->w / b->h : ar;
    if (V.zoom) {
        float w = fminf(BX_C.sw * 0.9f, 120 * u), h = w / ar;
        float maxs = h - ph; if (maxs < 0) maxs = 0;
        if (V.scroll > maxs) V.scroll = maxs;
        if (V.scroll < 0) V.scroll = 0;
        /* clip by drawing into the window, then cover outside the frame */
        bx_image(a, BX_C.sx + (BX_C.sw - w) / 2, top - V.scroll, w, h, 1);
        bx_rect(0, 0, BX_C.w, top, 0xff0a0a0a); bx_rect(0, top + ph, BX_C.w, BX_C.h - top - ph, 0xff0a0a0a);
    } else {
        float wa = ph * ar, wb = b ? ph * br : 0, gap = b ? .2f * u : 0, tot = wa + wb + gap;
        float sc = tot > BX_C.sw * 0.92f ? BX_C.sw * 0.92f / tot : 1;
        float x = BX_C.sx + (BX_C.sw - tot * sc) / 2, h = ph * sc, y = top + (ph - h) / 2;
        bx_image(a, x, y, wa * sc, h, 1);
        if (b) bx_image(b, x + (wa + gap) * sc, y, wb * sc, h, 1);
    }
    bx_text(BX_C.sx + BX_C.sw / 2, BX_C.sy + BX_C.sh * 0.055f, 1.6f * u, 0xf4ecdf, 255, V.title, 2, 0);
    char cnt[64] = "";
    if (V.n > 1) { if (two) snprintf(cnt, sizeof cnt, "Pages %d-%d of %d", V.i + 1, V.i + 2, V.n); else snprintf(cnt, sizeof cnt, "Page %d of %d", V.i + 1, V.n); }
    float by = BX_C.sy + BX_C.sh * 0.85f, cx = BX_C.sx + BX_C.sw / 2;
    float cw = bx_text_w(1.4f * u, cnt, 0) + 2 * u;
    if (V.n > 1) {
        bx_button(cx - cw / 2, by, "< Prev", view_prev, 0, 1);
        bx_text(cx, by + 2.0f * u, 1.4f * u, 0xb5a58f, 255, cnt, 0, 0);
        bx_button(cx + cw / 2, by, "Next >", view_next, 0, -1);
    }
    float rx = cx + cw / 2 + bx_text_w(1.5f * u, "Next >", 0) + 4.2f * u;
    if (V.n <= 1) rx = cx - 6 * u;
    bx_button(rx, by, V.zoom ? "Fit" : "Zoom", view_zoom, 0, -1);
    bx_button(rx + bx_text_w(1.5f * u, "Zoom", 0) + 4 * u, by, "Close", view_close, 0, -1);
}
static void view_key(int k)
{
    if (k == OB_SCREEN_RIGHT) view_next(0);
    else if (k == OB_SCREEN_LEFT) view_prev(0);
    else if (k == OB_SCREEN_UP) { if (V.zoom) V.scroll -= BX_C.sh * 0.12f; }
    else if (k == OB_SCREEN_DOWN) { if (V.zoom) V.scroll += BX_C.sh * 0.12f; }
    else if (k == OB_SCREEN_BACK) view_close(0);
    else if (k == OB_SCREEN_OK) { if (V.i + 1 < V.n) view_next(0); else view_close(0); }
}

/* ---------------- the book: a manual read in 3D ----------------
 * The pages as a booklet in front of the camera: two pages open, a leaf that turns over in 3D, the cover alone on the right
 * at first. Pages are art keys read when shown (a few kept at a time), so a long manual costs little. Pick the right page (or
 * Right) for the next, the left (or Left) for the one before; Full size opens the flat reader on the same pages. */
#define BK_SLOTS 8
static struct {
    int up, built, n, s, dir; float t;            /* spread s (0: the cover alone); a turn: dir +1 / -1, t 0..1 */
    char key[VIEW_MAX][32], title[96]; void (*close)(int);
    int root, nSh, nL, nR, nT, nBL, nBR, nDim, mL, mR, mTf, mTb;
    int slot_tex[BK_SLOTS], slot_page[BK_SLOTS]; unsigned slot_used[BK_SLOTS], tick;
    float W, H;
} K;
int bx_book_up(void) { return K.up; }
static int bk_tex(int page)                       /* the texture holding a page, read if it is not there */
{
    if (page < 0 || page >= K.n) return -1;
    int lru = 0;
    for (int i = 0; i < BK_SLOTS; i++) {
        if (K.slot_page[i] == page) { K.slot_used[i] = ++K.tick; return K.slot_tex[i]; }
        if (K.slot_used[i] < K.slot_used[lru]) lru = i;
    }
    int w = 0, h = 0; uint32_t *px = art_load(K.key[page], &w, &h, NULL);
    r3_tex *t = &BX_S->texs[K.slot_tex[lru]];
    t->flip_y = 1; t->rep[0] = t->rep[1] = 1;
    r3_tex_set(t, px, w, h);
    K.slot_page[lru] = page; K.slot_used[lru] = ++K.tick;
    return K.slot_tex[lru];
}
static int bk_left(int s) { return s <= 0 ? -1 : 2 * s - 1 < K.n ? 2 * s - 1 : -1; }
static int bk_right(int s) { return 2 * s < K.n ? 2 * s : -1; }
static void bk_face(int node, int mat, int page)
{
    int tx = bk_tex(page);
    bx_n(node)->vis = tx >= 0;
    if (tx >= 0) BX_S->mats[mat].map = tx;
}
static void bk_layout(void)
{
    int l, r;
    if (K.dir > 0) { l = bk_left(K.s); r = bk_right(K.s + 1); bk_face(K.nT, K.mTf, bk_right(K.s)); bk_face(K.nT, K.mTb, bk_left(K.s + 1)); bx_n(K.nT)->vis = 1; }
    else if (K.dir < 0) { l = bk_left(K.s - 1); r = bk_right(K.s); bk_face(K.nT, K.mTf, bk_right(K.s - 1)); bk_face(K.nT, K.mTb, bk_left(K.s)); bx_n(K.nT)->vis = 1; }
    else { l = bk_left(K.s); r = bk_right(K.s); bx_n(K.nT)->vis = 0; }
    bk_face(K.nL, K.mL, l); bk_face(K.nR, K.mR, r);
    bx_n(K.nBL)->vis = l >= 0 || (K.dir != 0); bx_n(K.nBR)->vis = r >= 0 || (K.dir != 0);
    if (K.dir == 0) { bx_n(K.nBL)->vis = l >= 0; bx_n(K.nBR)->vis = r >= 0; }
}
static float bk_shift(int s) { return bk_left(s) < 0 ? -K.W / 2 : bk_right(s) < 0 ? K.W / 2 : 0; }
static float bk_ease(float t) { return t < .5f ? 2 * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) / 2; }
static void bk_frame(float dt)
{
    if (K.dir) {
        K.t += dt / 520.0f;
        if (K.t >= 1) { K.s += K.dir; K.dir = 0; K.t = 0; bk_layout(); }
    }
    float a = K.dir > 0 ? bk_ease(K.t) : K.dir < 0 ? 1 - bk_ease(K.t) : 0;
    r3_node *T = bx_n(K.nT);
    T->rot[1] = -PI_F * a;
    {   /* the cover alone sits in the middle, so does the back cover alone; two pages open sit either side of it */
        float o0 = bk_shift(K.s), o1 = K.dir ? bk_shift(K.s + K.dir) : o0, e = K.dir ? bk_ease(K.t) : 0;
        bx_n(K.nSh)->pos[0] = o0 + (o1 - o0) * e;
    }
    T->pos[2] = .02f + sinf(PI_F * a) * .25f;     /* the leaf lifts as it turns */
    if (!K.dir) {
        if (bk_right(K.s) >= 0 && bk_left(K.s + 1) >= 0) bx_hot_node(K.nR, "Next page");
        if (bk_left(K.s) >= 0) bx_hot_node(K.nL, "Previous page");
    }
}
static void bk_next(int a) { (void)a; if (K.dir || bk_left(K.s + 1) < 0) return; K.dir = 1; K.t = 0; bk_layout(); bx_sfx(SFX_PAGE, 0); }
static void bk_prev(int a) { (void)a; if (K.dir || K.s <= 0) return; K.dir = -1; K.t = 0; bk_layout(); bx_sfx(SFX_PAGE, 0); }
static void bk_close(int a)
{
    (void)a;
    K.up = 0; K.dir = 0;
    if (K.built && K.root >= 0 && K.root < BX_S->nnodes) bx_n(K.root)->vis = 0;
    for (int i = 0; BX_S && i < BK_SLOTS; i++) if (K.slot_tex[i] >= 0 && K.slot_tex[i] < BX_S->ntexs) { r3_tex_set(&BX_S->texs[K.slot_tex[i]], NULL, 0, 0); K.slot_page[i] = -1; }
    B.kb = -1;
    if (K.close) K.close(0);
}
static void bk_full(int a)
{
    (void)a;
    const char *k[VIEW_MAX];
    for (int i = 0; i < K.n; i++) k[i] = K.key[i];
    bx_view_open_keys(K.title, k, K.n, 1, NULL);
    V.i = K.s > 0 ? bk_left(K.s) : 0;
}
static int bk_build(void)
{
    if (K.built && K.root >= 0 && K.root < BX_S->nnodes && !strcmp(BX_S->nodes[K.root].name, "book.root")) return 1;
    int n0 = BX_S->nnodes;
    K.root = r3_node_add(BX_S, -1, "book.root");
    K.nSh = r3_node_add(BX_S, K.root, "book.shift");                 /* the booklet slides so what is open sits in the middle */
    K.nL = r3_node_add(BX_S, K.nSh, "book.left"); K.nR = r3_node_add(BX_S, K.nSh, "book.right"); K.nT = r3_node_add(BX_S, K.nSh, "book.leaf");
    K.nBL = r3_node_add(BX_S, K.nSh, "book.boardL"); K.nBR = r3_node_add(BX_S, K.nSh, "book.boardR"); K.nDim = r3_node_add(BX_S, K.root, "book.dim");
    if (K.root < 0 || K.nDim < 0) return 0;
    for (int i = n0; i < BX_S->nnodes; i++) { snprintf(BX_S->nodes[i].label, sizeof BX_S->nodes[i].label, "%s", K.title); }
    r3_mat pg; memset(&pg, 0, sizeof pg);
    pg.type = 'L'; pg.col[0] = pg.col[1] = pg.col[2] = 1; pg.map = -1; pg.opacity = 1; pg.depth_write = 1; pg.visible = 1; pg.bright = 1;
    pg.emis[0] = pg.emis[1] = pg.emis[2] = .18f;
    K.mL = r3_mat_add(BX_S, &pg); K.mR = r3_mat_add(BX_S, &pg); K.mTf = r3_mat_add(BX_S, &pg); K.mTb = r3_mat_add(BX_S, &pg);
    r3_mat bd = pg; bd.map = -1; bd.col[0] = .045f; bd.col[1] = .04f; bd.col[2] = .036f; bd.emis[0] = bd.emis[1] = bd.emis[2] = 0;
    int mBd = r3_mat_add(BX_S, &bd);
    r3_mat dm = pg; dm.type = 'B'; dm.map = -1; dm.col[0] = dm.col[1] = dm.col[2] = 0; dm.opacity = .72f; dm.transparent = 1; dm.depth_write = 0; dm.emis[0] = dm.emis[1] = dm.emis[2] = 0;
    int mDim = r3_mat_add(BX_S, &dm);
    for (int i = 0; i < BK_SLOTS; i++) { K.slot_tex[i] = r3_tex_add(BX_S, NULL, 0, 0); K.slot_page[i] = -1; K.slot_used[i] = 0; }
    K.H = 5.0f; K.W = 5.0f * .6875f;
    float W = K.W, H = K.H, e = .05f;
    r3_mesh_quad(BX_S, K.nL, K.mL, -W, 0, -H / 2, H / 2, .01f, 1, 0);
    r3_mesh_quad(BX_S, K.nR, K.mR, 0, W, -H / 2, H / 2, .01f, 1, 0);
    r3_mesh_quad(BX_S, K.nT, K.mTf, 0, W, -H / 2, H / 2, .004f, 1, 0);
    r3_mesh_quad(BX_S, K.nT, K.mTb, 0, W, -H / 2, H / 2, -.004f, -1, 1);
    r3_mesh_quad(BX_S, K.nBL, mBd, -W - e, 0, -H / 2 - e, H / 2 + e, -.04f, 1, 0);
    r3_mesh_quad(BX_S, K.nBR, mBd, 0, W + e, -H / 2 - e, H / 2 + e, -.04f, 1, 0);
    r3_mesh_quad(BX_S, K.nDim, mDim, -80, 80, -60, 60, -.6f, 1, 0);
    bx_n(K.root)->vis = 0;
    K.built = 1;
    return 1;
}
/* open the book in front of the camera */
void bx_book_open(const char *title, const char *const *keys, int n, void (*on_close)(int))
{
    if (!BX_S || n <= 0) return;
    if (n > VIEW_MAX) n = VIEW_MAX;
    snprintf(K.title, sizeof K.title, "%s", title ? title : "");
    if (!bk_build()) { bx_view_open_keys(title, keys, n, 1, on_close); return; }
    for (int i = 0; i < n; i++) snprintf(K.key[i], sizeof K.key[0], "%s", keys[i]);
    for (int i = 0; i < BK_SLOTS; i++) { r3_tex_set(&BX_S->texs[K.slot_tex[i]], NULL, 0, 0); K.slot_page[i] = -1; }
    K.n = n; K.s = 0; K.dir = 0; K.t = 0; K.close = on_close; K.up = 1;
    cam_unzoom(); view_reset();
    /* in front of the camera, facing it, the open pages filling about three quarters of the height */
    float *c = BX_S->cam, d[3] = { bx_look[0] - c[0], bx_look[1] - c[1], bx_look[2] - c[2] };
    float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (L < 1e-3f) { d[0] = 0; d[1] = 0; d[2] = -1; L = 1; }
    for (int k = 0; k < 3; k++) d[k] /= L;
    float D = K.H / (.74f * 2 * tanf(BX_S->fov * PI_F / 360));
    if (D > L * .8f) D = L * .8f;
    r3_node *r = bx_n(K.root);
    for (int k = 0; k < 3; k++) r->pos[k] = c[k] + d[k] * D;
    r->rot[0] = asinf(d[1]); r->rot[1] = atan2f(-d[0], -d[2]); r->rot[2] = 0;
    r->vis = 1;
    bk_layout();
    bx_sfx(SFX_PAGE, 0);
}
static void bk_draw2d(void)
{
    float u = BX_C.u;
    bx_text(BX_C.sx + BX_C.sw / 2, BX_C.sy + BX_C.sh * 0.055f, 1.6f * u, 0xf4ecdf, 255, K.title, 2, 0);
    char cnt[64];
    int l = bk_left(K.s), r = bk_right(K.s);
    if (l >= 0 && r >= 0) snprintf(cnt, sizeof cnt, "Pages %d-%d of %d", l + 1, r + 1, K.n);
    else snprintf(cnt, sizeof cnt, "Page %d of %d", (r >= 0 ? r : l) + 1, K.n);
    float by = BX_C.sy + BX_C.sh * 0.88f, cx = BX_C.sx + BX_C.sw / 2, cw = bx_text_w(1.4f * u, cnt, 0) + 2 * u;
    bx_button(cx - cw / 2, by, "< Prev", bk_prev, 0, 1);
    bx_text(cx, by + 2.0f * u, 1.4f * u, 0xb5a58f, 255, cnt, 0, 0);
    bx_button(cx + cw / 2, by, "Next >", bk_next, 0, -1);
    float rx = cx + cw / 2 + bx_text_w(1.5f * u, "Next >", 0) + 4.2f * u;
    bx_button(rx, by, "Full size", bk_full, 0, -1);
    bx_button(rx + bx_text_w(1.5f * u, "Full size", 0) + 4 * u, by, "Close", bk_close, 0, -1);
    bx_text(BX_C.sx + BX_C.sw / 2, BX_C.sy + BX_C.sh * 0.97f, 1.3f * u, 0xb5a58f, 255, "Pick the right page to turn it, the left to go back", 0, 0);
}
static int bk_key(int k)
{
    if (k == OB_SCREEN_RIGHT || k == OB_SCREEN_DOWN) bk_next(0);
    else if (k == OB_SCREEN_LEFT || k == OB_SCREEN_UP) bk_prev(0);
    else if (k == OB_SCREEN_BACK) bk_close(0);
    else if (k == OB_SCREEN_OK) { if (bk_left(K.s + 1) >= 0) bk_next(0); else bk_close(0); }
    else return 0;
    return 1;
}

/* ---------------- sound ---------------- */
#define MIXN 88200
static SDL_AudioDeviceID adev;
static float mixbuf[MIXN];
static int mixpos;                  /* the callback's read position */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *o = (int16_t *)stream; int n = len / 2;
    for (int i = 0; i < n; i++) {
        float v = mixbuf[mixpos]; mixbuf[mixpos] = 0; mixpos = (mixpos + 1) % MIXN;
        v = v > 1 ? 1 : v < -1 ? -1 : v;
        o[i] = (int16_t)(v * 32000);
    }
}
static uint32_t rng = 22222;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
/* white noise with a squared fade through a band-pass (RBJ), as the mockup's noise() */
static void add_noise(float dur, float freq, float q, float gain, float when)
{
    int n = (int)(44100 * dur), at = (mixpos + 1024 + (int)(when * 44100)) % MIXN;
    float w0 = 2 * PI_F * freq / 44100, al = sinf(w0) / (2 * q), b0 = al, b2 = -al, a0 = 1 + al, a1 = -2 * cosf(w0), a2 = 1 - al;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < n && i < MIXN - 2048; i++) {
        float x = (frand() * 2 - 1) * (1 - (float)i / n) * (1 - (float)i / n);
        float y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        mixbuf[(at + i) % MIXN] += y * gain * 0.5f;
    }
}
static void add_tone(float freq, float dur, int type, float gain, float when, float sweep)
{
    int n = (int)(44100 * dur), at = (mixpos + 1024 + (int)(when * 44100)) % MIXN;
    float ph = 0;
    for (int i = 0; i < n && i < MIXN - 2048; i++) {
        float t = (float)i / n, f = sweep > 0 ? freq * powf(sweep / freq, t) : freq;
        ph += f / 44100; ph -= floorf(ph);
        float s = type == 0 ? sinf(2 * PI_F * ph) : type == 1 ? (ph < .5f ? 1.f : -1.f) : type == 2 ? 2 * ph - 1 : (ph < .5f ? 4 * ph - 1 : 3 - 4 * ph);
        float env = gain > 0 ? gain * powf(0.0001f / gain, t) : 0;
        mixbuf[(at + i) % MIXN] += s * env * 0.5f;
    }
}
void bx_sfx(int kind, int n)
{
    if (!adev) {
        SDL_AudioSpec want, got;
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
        memset(&want, 0, sizeof want);
        want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        if (!adev) return;
        SDL_PauseAudioDevice(adev, 0);
    }
    SDL_LockAudioDevice(adev);
    switch (kind) {
    case SFX_DISK_IN: add_noise(.05f, 1800, 2, .5f, 0); add_noise(.09f, 300, 1, .9f, .06f); add_tone(90, .12f, 0, .6f, .06f, 50); add_noise(.04f, 3000, 3, .4f, .2f); break;
    case SFX_DISK_OUT: add_noise(.04f, 3500, 3, .5f, 0); add_tone(140, .07f, 1, .12f, 0, 70); add_noise(.12f, 900, 1.5f, .5f, .03f); add_noise(.05f, 1200, 2, .4f, .16f); break;
    case SFX_BEEP: add_tone(1000, .14f, 1, .12f, 0, 0); break;
    case SFX_MOTOR: add_noise(.7f, 160, .6f, .5f, 0); break;
    case SFX_STEP: add_tone(180, .05f, 2, .12f, 0, 60); add_noise(.04f, 500, 4, .3f, 0); break;
    case SFX_SEEK: for (int i = 0; i < (n > 0 ? n : 3); i++) { float t = i * .045f; add_tone(120 + frand() * 80, .04f, 2, .1f, t, 60); add_noise(.03f, 700, 5, .25f, t); } break;
    case SFX_HDD: add_noise(.012f, 2600, 3, .35f, 0); if (frand() < .3f) add_tone(90 + frand() * 30, .05f, 3, .05f, 0, 50); break;
    case SFX_KEY: add_noise(.02f, 2200, 2, .25f, 0); break;
    case SFX_SLIDE: add_noise(.5f, 700, .8f, .25f, 0); break;
    case SFX_PAGE: add_noise(.18f, 2200, .7f, .28f, 0); break;
    case SFX_CLICK: add_noise(.3f, 800, .7f, .2f, 0); break;
    }
    SDL_UnlockAudioDevice(adev);
}

/* ---------------- settings ---------------- */
static void ini_write(void)
{
    FILE *f = fopen(B.ini, "w");
    if (!f) return;
    fprintf(f, "; the box (experimental)\nshow=%s\nseen=%d\n", B.show_pref == 1 ? "never" : B.show_pref == 2 ? "always" : "first", B.seen);
    if (B.art_ini[0]) fprintf(f, "art=%s\n", B.art_ini);        /* (only one the player set) */
    fclose(f);
}
static void ini_read(void)
{
    char line[1200];
    FILE *f = fopen(B.ini, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        size_t L = strlen(line); while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        if (!strncmp(line, "show=", 5)) B.show_pref = !strcmp(line + 5, "never") ? 1 : !strcmp(line + 5, "always") ? 2 : 0;
        else if (!strncmp(line, "seen=", 5)) B.seen = atoi(line + 5);
        else if (!strncmp(line, "art=", 4) && line[4]) { size_t n = strlen(line + 4); if (n >= sizeof B.art_ini) n = sizeof B.art_ini - 1; memcpy(B.art_ini, line + 4, n); B.art_ini[n] = 0; memcpy(B.art, B.art_ini, n + 1); }
    }
    fclose(f);
}

/* ---------------- the screen ---------------- */
void bx_say(const char *html) { snprintf(BX.cap, sizeof BX.cap, "%s", html); }

static void finish(int a)
{
    (void)a;
    if (BX.first) { B.seen = 1; ini_write(); }
    BX.first = 0;
    B.up = 0;
    bx_time_clear();
    V.up = 0;
    ob_screen_show(NULL);
    r3gl_close();
    B.gpu = 0;
}
void bx_done(void) { bx_fade(finish, 0); }
static void skip(int a) { (void)a; if (BX_SC->skip) BX_SC->skip(); bx_done(); }
/* opened from View Manual & Materials: play the whole opening again at the next start, or not */
static void replay_toggle(int a)
{
    (void)a;
    if (B.seen) { B.seen = 0; if (B.show_pref == 1) B.show_pref = 0; }
    else B.seen = 1;
    ini_write();
}

void (*bx_bg_paint)(uint32_t *px, int w, int h);
static void (*bg_painted)(uint32_t *px, int w, int h);
static void make_bg(int w, int h)
{
    if (B.bg && B.bg_w == w && B.bg_h == h && bg_painted == bx_bg_paint) return;
    free(B.bg);
    B.bg = (uint32_t *)malloc((size_t)w * h * 4);
    B.bg_w = w; B.bg_h = h; bg_painted = bx_bg_paint;
    if (!B.bg) return;
    if (bx_bg_paint) { bx_bg_paint(B.bg, w, h); return; }
    /* radial-gradient(ellipse at 50% 20%, #4a3829 0%, #2a211b 55%, #1a1410 100%) */
    float cx = w * .5f, cy = h * .2f, rx = w * .5f * 1.4142f, ry = h * .8f * 1.4142f;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry, d = sqrtf(dx * dx + dy * dy);
            float r, g, b;
            if (d < .55f) { float t = d / .55f; r = 0x4a + (0x2a - 0x4a) * t; g = 0x38 + (0x21 - 0x38) * t; b = 0x29 + (0x1b - 0x29) * t; }
            else { float t = (d - .55f) / .45f; if (t > 1) t = 1; r = 0x2a + (0x1a - 0x2a) * t; g = 0x21 + (0x14 - 0x21) * t; b = 0x1b + (0x10 - 0x1b) * t; }
            B.bg[(size_t)y * w + x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
}

/* the 3D picture in bands of rows on every core (SDL threads), each band picking the next free one */
#define BX_THREADS 16
#define BX_BAND 24
static struct { int n; SDL_sem *go[BX_THREADS], *done; SDL_atomic_t next; int nb; int up; } TP = { .n = -1 };
static void up_rows(int y0, int y1);
static void pool_start(void);
static void bands(void)
{
    for (;;) {
        int b = SDL_AtomicAdd(&TP.next, 1);
        if (b >= TP.nb) return;
        if (TP.up) up_rows(b * BX_BAND, (b + 1) * BX_BAND);
        else r3_render_rows(BX_S, &B.tgt, B.bg, b * BX_BAND, (b + 1) * BX_BAND);
    }
}
static void run_bands(int nb, int up)
{
    if (TP.n < 0) pool_start();
    TP.nb = nb; TP.up = up;
    SDL_AtomicSet(&TP.next, 0);
    for (int i = 1; i < TP.n; i++) SDL_SemPost(TP.go[i]);
    bands();
    for (int i = 1; i < TP.n; i++) SDL_SemWait(TP.done);
}
static int band_worker(void *arg)
{
    int i = (int)(intptr_t)arg;
    for (;;) { SDL_SemWait(TP.go[i]); bands(); SDL_SemPost(TP.done); }
    return 0;
}
static void pool_start(void)
{
    int n = SDL_GetCPUCount();
    if (n > BX_THREADS) n = BX_THREADS;
    TP.n = 1;
    if (n < 2 || !(TP.done = SDL_CreateSemaphore(0))) return;
    for (int i = 1; i < n; i++) {
        if (!(TP.go[i] = SDL_CreateSemaphore(0))) break;
        SDL_Thread *t = SDL_CreateThread(band_worker, "overbar box", (void *)(intptr_t)i);
        if (!t) { SDL_DestroySemaphore(TP.go[i]); break; }
        SDL_DetachThread(t);
        TP.n = i + 1;
    }
}

/* the scene's 3D picture at the window's own size (up to 4K; bigger is drawn smaller and stretched). A still scene is not drawn
   again: the last picture is kept (its signature says what it shows). While things move it is drawn with fewer pixels if the
   whole size would not keep up, and drawn at full size again as soon as it stops. */
float box_fixed_dt;                /* tests: each frame is this many ms (0: the clock) */
static struct { const uint32_t *src; int tw, th; uint32_t *px; int w, h, pitch; } UP;
static void up_rows(int ya, int yb)
{
    const uint32_t *src = UP.src; int tw = UP.tw, th = UP.th, w = UP.w, h = UP.h, pitch = UP.pitch; uint32_t *px = UP.px;
    uint32_t sx = (uint32_t)(((uint64_t)tw << 16) / w), sy = (uint32_t)(((uint64_t)th << 16) / h);
    if (yb > h) yb = h;
    for (int y = ya; y < yb; y++) {
        int fy = (int)(y * sy + sy / 2) - 32768; if (fy < 0) fy = 0;
        int y0 = fy >> 16, y1 = y0 + 1 < th ? y0 + 1 : th - 1; uint32_t ay = (fy >> 8) & 255;
        const uint32_t *r0 = src + (size_t)y0 * tw, *r1 = src + (size_t)y1 * tw;
        uint32_t *o = px + (size_t)y * pitch;
        for (int x = 0; x < w; x++) {
            int fx = (int)(x * sx + sx / 2) - 32768; if (fx < 0) fx = 0;
            int x0 = fx >> 16, x1 = x0 + 1 < tw ? x0 + 1 : tw - 1; uint32_t ax = (fx >> 8) & 255;
            uint32_t a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
            uint32_t rb = (((a & 0xff00ff) * (256 - ax) + (b & 0xff00ff) * ax) >> 8) & 0xff00ff;
            uint32_t gg = (((a & 0x00ff00) * (256 - ax) + (b & 0x00ff00) * ax) >> 8) & 0x00ff00;
            uint32_t rb2 = (((c & 0xff00ff) * (256 - ax) + (d & 0xff00ff) * ax) >> 8) & 0xff00ff;
            uint32_t gg2 = (((c & 0x00ff00) * (256 - ax) + (d & 0x00ff00) * ax) >> 8) & 0x00ff00;
            uint32_t RB = ((rb * (256 - ay) + rb2 * ay) >> 8) & 0xff00ff, GG = ((gg * (256 - ay) + gg2 * ay) >> 8) & 0x00ff00;
            o[x] = 0xff000000u | RB | GG;
        }
    }
}
static void render3d(uint32_t *px, int w, int h, int pitch)
{
    if (B.c3cap < (long)w * h) {
        free(B.c3); B.c3 = (uint32_t *)malloc((size_t)w * h * 4); B.c3cap = B.c3 ? (long)w * h : 0; B.c3ok = 0;
        if (!B.c3) return;
    }
    if (B.c3w != w || B.c3h != h) B.c3ok = 0;
    unsigned sig = r3_signature(BX_S);
    int still = B.c3ok && sig == B.c3sig;
    if (still && B.c3full) {                     /* nothing moved: the same picture */
        for (int y = 0; y < h; y++) memcpy(px + (size_t)y * pitch, B.c3 + (size_t)y * w, (size_t)w * 4);
        return;
    }
    /* the GPU: the whole picture at the window's size, every frame */
    make_bg(w, h);
    if (B.bg && r3gl_render(BX_S, B.c3, w, h, w, B.bg)) {
        B.gpu = 1;
        B.tgt.px = B.c3; B.tgt.w = w; B.tgt.h = h; B.tgt.pitch = w;
        B.r3ms = r3gl_ms(); B.r3w = w; B.r3h = h;
        B.c3w = w; B.c3h = h; B.c3sig = sig; B.c3full = 1; B.c3ok = 1;
        for (int y = 0; y < h; y++) memcpy(px + (size_t)y * pitch, B.c3 + (size_t)y * w, (size_t)w * 4);
        return;
    }
    B.gpu = 0;
    if (B.dyn <= 0) B.dyn = 1;
    float k = sqrtf(8.3e6f / ((float)w * h));
    if (k > 1) k = 1;
    int full = still || box_fixed_dt > 0 || getenv("OB_BOX_DYN_OFF") != NULL;
    if (!full) k *= sqrtf(B.dyn);
    int tw = (int)(w * k), th = (int)(h * k);
    if (tw < 16) tw = 16;
    if (th < 16) th = 16;
    int direct = tw == w && th == h;
    if (direct) { B.tgt.px = B.c3; B.tgt.w = w; B.tgt.h = h; B.tgt.pitch = w; }
    else {
        if (B.rcap < tw * th) { free(B.rpx); B.rpx = (uint32_t *)malloc((size_t)tw * th * 4); B.rcap = B.rpx ? tw * th : 0; if (!B.rpx) return; }
        B.tgt.px = B.rpx; B.tgt.w = tw; B.tgt.h = th; B.tgt.pitch = tw;
    }
    double t0 = bx_now();
    make_bg(tw, th);
    if (!r3_render_begin(BX_S, &B.tgt)) return;
    run_bands((th + BX_BAND - 1) / BX_BAND, 0);
    if (!direct) {                               /* bilinear up, 16.16 fixed, in bands too */
        UP.src = B.rpx; UP.tw = tw; UP.th = th; UP.px = B.c3; UP.w = w; UP.h = h; UP.pitch = w;
        run_bands((h + BX_BAND - 1) / BX_BAND, 1);
    }
    float ms = (float)((bx_now() - t0) * 1000.0);
    B.r3ms = ms; B.r3w = tw; B.r3h = th;
    if (!still) {                                /* moving: aim for about 9 ms (a 60 Hz frame with room for the rest) */
        float f = 9.0f / (ms > .5f ? ms : .5f);
        if (f < .7f) f = .7f;
        if (f > 1.25f) f = 1.25f;
        if (direct || f < 1) B.dyn *= f;
        if (B.dyn > 1) B.dyn = 1;
        if (B.dyn < .3f) B.dyn = .3f;
    }
    B.c3w = w; B.c3h = h; B.c3sig = sig; B.c3full = k >= 1 || direct || full; B.c3ok = 1;
    for (int y = 0; y < h; y++) memcpy(px + (size_t)y * pitch, B.c3 + (size_t)y * w, (size_t)w * 4);
}

/* the stats (F12 / the overlay's Stats): the box's own pictures a second and what its 3D picture costs */
static void stats_draw(uint32_t *px, int w, int h, int pitch)
{
    double now = bx_now();
    B.fps_n++;
    if (B.fps_t <= 0) B.fps_t = now;
    if (now - B.fps_t >= 1) { B.fps = (int)(B.fps_n / (now - B.fps_t) + .5); B.fps_n = 0; B.fps_t = now; }
    if (!B.host || !B.host->stats) return;
    char l1[40], l2[48], l3[40];
    snprintf(l1, sizeof l1, "BOX FPS %d", B.fps);
    snprintf(l2, sizeof l2, "3D %.1f MS %dX%d", B.r3ms, B.r3w, B.r3h);
    if (B.gpu) snprintf(l3, sizeof l3, "GPU  %dX%d", w, h);
    else snprintf(l3, sizeof l3, "CPU %d CORES  %dX%d", TP.n > 0 ? TP.n : 1, w, h);
    const char *const lines[3] = { l1, l2, l3 };
    static uint32_t sp[200 * 28];
    int bw = 0, bh = 0, x, y, ww, hh;
    ob_stats_draw(lines, sp, 200, 200, 28, &bw, &bh);
    ob_stats_place(w, h, bw, bh, &x, &y, &ww, &hh);
    for (int j = 0; j < hh && y + j < h; j++) {
        uint32_t *o = px + (size_t)(y + j) * pitch;
        const uint32_t *r = sp + (size_t)(j * bh / hh) * 200;
        for (int i = 0; i < ww && x + i < w; i++) {
            if (x + i < 0) continue;
            uint32_t c = r[i * bw / ww], a = c >> 24;
            if (!a) continue;
            uint32_t d = o[x + i];
            uint32_t rb = ((c & 0xff00ff) * a + (d & 0xff00ff) * (255 - a)) >> 8 & 0xff00ff;
            uint32_t g = ((c & 0xff00) * a + (d & 0xff00) * (255 - a)) >> 8 & 0xff00;
            o[x + i] = 0xff000000u | rb | g;
        }
    }
}

static void draw(void *ud, uint32_t *px, int w, int h, int pitch)
{
    (void)ud;
    double now = bx_now();
    float dt = box_fixed_dt > 0 ? box_fixed_dt : (float)((now - B.last) * 1000.0);
    B.last = now;
    if (dt < 0 || dt > 50) dt = dt < 0 ? 0 : 50;
    if (B.host && B.host->overlay_open) dt = 0;
    if (B.up && BX_S && cam_real[2] != 0) { memcpy(BX_S->cam, cam_real, sizeof cam_real); cam_real[2] = 0; }   /* the scene's own camera while it moves */
    time_step(dt);
    fade_step(dt);
    BX_C.px = px; BX_C.w = w; BX_C.h = h; BX_C.pitch = pitch;
    if ((float)w / h > 16.0f / 9) { BX_C.sh = (float)h; BX_C.sw = h * 16.0f / 9; } else { BX_C.sw = (float)w; BX_C.sh = w * 9.0f / 16; }
    BX_C.sx = (w - BX_C.sw) / 2; BX_C.sy = (h - BX_C.sh) / 2; BX_C.u = BX_C.sw / 100;
    if (!B.up) { for (int y = 0; y < h; y++) memset(px + (size_t)y * pitch, 0, (size_t)w * 4); return; }
    int kb_node = B.kb >= 0 && B.kb < nhot ? HOT[B.kb].node : -2;
    bx_hot_clear();
    if (BX_SC->draws_3d && !BX_SC->draws_3d()) {
        BX_SC->draw_full();
    } else {
        BX_SC->frame(dt);
        if (K.up) { bx_hot_clear(); bk_frame(dt); }
        /* highlight */
        for (int i = 0; i < BX_S->nnodes; i++) BX_S->nodes[i].hl = 1;
        for (int i = 0; i < BX_S->nmats; i++) BX_S->mats[i].bright = 1;
        int hn = -1;
        if (!V.up) {
            if (kb_node >= 0) hn = kb_node;
            else if (B.mouse_in && hover >= 0 && hover < nhot && HOT[hover].node >= 0) hn = HOT[hover].node;
            for (int i = 0; i < nhot; i++) if (HOT[i].node == hn) { BX_S->nodes[hn].hl = 1.12f; break; }
        }
        BX_S->look[0] = bx_look[0]; BX_S->look[1] = bx_look[1]; BX_S->look[2] = bx_look[2];
        memcpy(cam_real, BX_S->cam, sizeof cam_real);
        if (cam_real[2] == 0) cam_real[2] = 1e-6f;
        /* the zoom eases toward where the wheel or the pinch sent it (evenly in its logarithm, so in and out feel the same) */
        if (zoom != zoom_to) {
            float a = 1 - expf(-dt / 70.0f);
            zoom = expf(logf(zoom) + (logf(zoom_to) - logf(zoom)) * a);
            if (fabsf(zoom / zoom_to - 1) < 1e-3f) zoom = zoom_to;
        }
        for (int k = 0; k < 3; k++) BX_S->cam[k] = BX_S->look[k] + (BX_S->cam[k] - BX_S->look[k]) * zoom;   /* kept till the next frame: picks see this picture */
        if (pan[0] != 0 || pan[1] != 0) {          /* the pan: camera and target together, across the view */
            float f[3] = { BX_S->look[0] - BX_S->cam[0], BX_S->look[1] - BX_S->cam[1], BX_S->look[2] - BX_S->cam[2] };
            float L = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (L > 1e-4f) {
                for (int k = 0; k < 3; k++) f[k] /= L;
                float r[3] = { f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0 };   /* f x up */
                float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                if (rl > 1e-4f) {
                    for (int k = 0; k < 3; k++) r[k] /= rl;
                    float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
                    float vh = 2 * L * tanf(BX_S->fov * PI_F / 360);
                    if ((float)w / h < 16.0f / 9) vh *= (16.0f / 9) / ((float)w / h);   /* (a narrow window shows more height: r3d keeps the width) */
                    for (int k = 0; k < 3; k++) { float o = (r[k] * pan[0] + u[k] * pan[1]) * vh; BX_S->cam[k] += o; BX_S->look[k] += o; }
                }
            }
        }
        render3d(px, w, h, pitch);
        /* the label under the highlighted object */
        if (hn >= 0 && !V.up && !B.dragging) {
            float x0, y0, x1, y1;
            const char *lab = NULL;
            for (int i = 0; i < nhot; i++) if (HOT[i].node == hn) lab = HOT[i].label;
            if (lab && lab[0] && r3_screen_box(BX_S, &B.tgt, hn, &x0, &y0, &x1, &y1)) {
                float k = (float)w / B.tgt.w;
                float y = y1 * k + BX_C.u;
                if (y > BX_C.sy + BX_C.sh - 6 * BX_C.u) y = BX_C.sy + BX_C.sh - 6 * BX_C.u;
                bx_pill((x0 + x1) / 2 * k, y, lab);
            }
        }
        if (K.up) bk_draw2d(); else BX_SC->draw2d();
    }
    /* first launch: the way out */
    if (BX.first && !V.up) bx_button(BX_C.sx + BX_C.sw - 2 * BX_C.u, BX_C.sy + BX_C.sh - 4.6f * BX_C.u, "Skip to the game", skip, 0, 1);
    /* from View Manual & Materials (the way back is the scene's own top-left button): the reset that plays the opening again at the next start */
    if (BX.from_menu && !BX.manual_only && !V.up) {
        bx_button(BX_C.sx + BX_C.sw - 2 * BX_C.u, BX_C.sy + BX_C.sh - 4.6f * BX_C.u,
                  B.seen ? "Replay the opening at next start" : "Opening plays at next start (undo)", replay_toggle, 0, 1);
    }
    {
        float u = BX_C.u;
        const char *tag = "EXPERIMENTAL";
        float tw2 = bx_text_w(1.0f * u, tag, 2) + 1.4f * u;
        bx_rrect(BX_C.sx + 2 * u, BX_C.sy + BX_C.sh - 3.2f * u, tw2, 1.8f * u, .9f * u, 0x66e0533d, 0, 0);
        bx_text(BX_C.sx + 2 * u + tw2 / 2, BX_C.sy + BX_C.sh - 1.95f * u, 1.0f * u, 0xffffff, 230, tag, 2, 0);
    }
    if (V.up) { nhot = 0; view_draw(); }
    if (B.focus_first) for (int i = 0; i < nhot; i++) if (!HOT[i].noauto) { B.kb = i; B.focus_first = 0; break; }
    if (B.kb >= nhot) B.kb = nhot ? nhot - 1 : -1;
    if (B.mouse_in) hover = hot_at(B.mx, B.my);
    /* a hot spot with no label and no action is only there to be dragged: the arrow stays */
    ob_pointer_hand_set(1, B.mouse_in && hover >= 0 && hover < nhot && !B.dragging && (HOT[hover].fn || (HOT[hover].label && HOT[hover].label[0])));
    if (B.fade > 0) bx_rect(0, 0, w, h, (uint32_t)(B.fade * 255) << 24);
    stats_draw(px, w, h, pitch);
}

static void activate(int i)
{
    if (i < 0 || i >= nhot) return;
    if (HOT[i].btn) { if (HOT[i].fn) { bx_sfx(SFX_KEY, 0); HOT[i].fn(HOT[i].arg); } return; }
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, sx = B.mx, sy = B.my;
    if (B.kb == i && r3_screen_box(BX_S, &B.tgt, HOT[i].node, &x0, &y0, &x1, &y1)) {   /* a key: the object's right half (next page) */
        float k = (float)BX_C.w / B.tgt.w; sx = (x0 + x1) / 2 * k + 1; sy = (y0 + y1) / 2 * k;
    }
    if (K.up) { if (HOT[i].node == K.nR) bk_next(0); else if (HOT[i].node == K.nL) bk_prev(0); return; }
    BX_SC->activate(HOT[i].node, sx, sy);
}

static void zoom_clamp(void)
{
    float lo = K.up ? .55f : .35f;
    if (zoom_to < lo) zoom_to = lo;
    if (zoom_to > 2.2f) zoom_to = 2.2f;
}
static void pan_by(float x, float y)
{
    pan[0] += x; pan[1] += y;
    if (pan[0] < -1) pan[0] = -1;
    if (pan[0] > 1) pan[0] = 1;
    if (pan[1] < -.8f) pan[1] = -.8f;
    if (pan[1] > .8f) pan[1] = .8f;
}

static void input(void *ud, const ob_screen_ev *e)
{
    (void)ud;
    if (!B.up || B.fade_dir != 0) return;
    if (e->kind == OB_SCREEN_MPRESS || e->kind == OB_SCREEN_MRELEASE) {   /* the middle button held: drag to pan */
        B.mpan = e->kind == OB_SCREEN_MPRESS && !V.up;
        B.mx = B.last_x = e->x; B.my = B.last_y = e->y;
        return;
    }
    if (e->kind == OB_SCREEN_MOVE && B.mpan) {
        if (BX_C.h > 0) pan_by(-(float)(e->x - B.last_x) / BX_C.h, (float)(e->y - B.last_y) / BX_C.h);   /* the scene follows the pointer */
        B.mx = B.last_x = e->x; B.my = B.last_y = e->y;
        return;
    }
    if (e->kind == OB_SCREEN_PAN) {
        if (V.up) { if (V.zoom) V.scroll -= e->dy * BX_C.sh * 0.08f; return; }
        pan_by(e->dx * 0.05f, e->dy * 0.05f);                  /* as the system scrolls: down moves the view down */
        return;
    }
    if (e->kind == OB_SCREEN_MOVE || e->kind == OB_SCREEN_PRESS || e->kind == OB_SCREEN_RELEASE) {
        B.mx = e->x; B.my = e->y; B.mouse_in = 1;
        if (e->kind == OB_SCREEN_MOVE) {
            if (B.down && !B.dragging && (abs(B.mx - B.down_x) + abs(B.my - B.down_y)) > 6) B.dragging = 1;
            if (B.dragging && BX_SC->drag && !V.up && !K.up) BX_SC->drag(B.down_node, (float)(B.mx - B.last_x), (float)(B.my - B.last_y));
            B.last_x = B.mx; B.last_y = B.my;
            if (!B.down) { hover = hot_at(B.mx, B.my); if (hover >= 0) B.kb = -1; }
            return;
        }
        if (e->kind == OB_SCREEN_PRESS) {
            B.down = 1; B.dragging = 0; B.down_x = B.last_x = B.mx; B.down_y = B.last_y = B.my;
            int hh = hot_at(B.mx, B.my);
            B.down_node = hh >= 0 ? HOT[hh].node : -1;
            return;
        }
        int was_drag = B.dragging;
        B.down = 0; B.dragging = 0;
        if (was_drag) return;
        hover = hot_at(B.mx, B.my);
        B.kb = -1;
        if (BX_SC->draws_3d && !BX_SC->draws_3d() && BX_SC->key && hover < 0) { BX_SC->key(OB_SCREEN_OK + 100); return; }
        activate(hover);
        return;
    }
    if (e->kind == OB_SCREEN_WHEEL) {
        if (V.up) { if (V.zoom) V.scroll -= e->y * BX_C.sh * 0.08f; return; }
        zoom_to *= e->y > 0 ? .85f : 1.0f / .85f;                 /* up: closer */
        zoom_clamp();
        return;
    }
    if (e->kind == OB_SCREEN_ZOOM) {
        if (V.up) { if ((e->x > 0) != !!V.zoom) view_zoom(0); return; }
        float a = e->amount != 0 ? e->amount : e->x / 1000.0f;
        zoom_to /= 1 + a * 2.5f;                                   /* fingers apart: closer */
        zoom_clamp();
        return;
    }
    if (V.up && e->kind != OB_SCREEN_OK) { view_key(e->kind); return; }
    if (K.up && !V.up && (e->kind != OB_SCREEN_OK || B.kb < 0)) { if (bk_key(e->kind)) { B.kb = -1; return; } }
    if (e->kind == OB_SCREEN_BACK) { if (V.up) view_close(0); else BX_SC->back(); B.kb = -1; return; }
    if (e->kind == OB_SCREEN_OK) {
        if (B.kb >= 0 && B.kb < nhot) { int k = B.kb; B.kb = -1; B.focus_first = 1; activate(k); }
        else if (V.up) view_key(OB_SCREEN_OK);
        else for (int i = 0; i < nhot; i++) if (!HOT[i].noauto) { B.focus_first = 1; activate(i); break; }   /* nothing lit: the first spot */
        return;
    }
    /* arrows: right/down to the next hot spot, left/up to the one before */
    B.mouse_in = 0;
    if (!nhot) return;
    int d = (e->kind == OB_SCREEN_RIGHT || e->kind == OB_SCREEN_DOWN) ? 1 : -1;
    if (B.kb < 0) B.kb = d > 0 ? 0 : nhot - 1;
    else B.kb = (B.kb + d + nhot) % nhot;
    bx_sfx(SFX_KEY, 0);
}

/* ---------------- start ---------------- */
static int load_game(void)
{
    if (B.loaded) return 1;
    char d[1300];
    snprintf(d, sizeof d, "%s/%s", B.data, BX_SC->dir);
    if (!r3_load(&B.scene, d, art_load, NULL)) { fprintf(stderr, "box: no scene in %s\n", d); return 0; }
    BX_S = &B.scene;
    B.loaded = 1;
    return 1;
}

int ob_box_open(int mode)
{
    if (!BX_SC || !load_game()) return 0;
    memset(&BX, 0, sizeof BX);
    BX.first = mode == BOX_SHOW_FIRST;
    BX.from_menu = mode != BOX_SHOW_FIRST;
    BX.manual_only = mode == BOX_SHOW_MANUAL;
    bx_time_clear();
    V.up = 0; B.kb = -1; hover = -1; B.fade = 0; B.fade_dir = 0; B.down = B.dragging = 0;
    B.up = 1;
    B.last = bx_now();
    ob_screen s = { draw, input, NULL };
    ob_screen_show(&s);
    K.up = 0; K.dir = 0;
    if (K.built && K.root >= 0 && K.root < BX_S->nnodes && !strcmp(BX_S->nodes[K.root].name, "book.root")) BX_S->nodes[K.root].vis = 0;
    if (mode == BOX_SHOW_MANUAL) { BX_SC->enter(mode); if (BX_SC->manual) BX_SC->manual(); }
    else BX_SC->enter(mode);
    if (B.host && B.host->overlay_open) ob_overlay_close(B.host);
    return 1;
}
/* the reader closed when it was opened by itself from the menu: back to the game */
void bx_manual_only_closed(int a) { (void)a; finish(0); }

static int menu_box(void *ctx) { (void)ctx; return ob_box_open(BOX_SHOW_REOPEN), 1; }

int ob_box_init(ob_host *h, const bx_scene *scene)
{
    const char *game = scene ? scene->dir : "";
    memset(&B, 0, sizeof B);
    B.host = h; B.kb = -1;
    snprintf(B.game, sizeof B.game, "%s", game);
    BX_SC = scene;
    if (!BX_SC) return 0;
    {
        char *base = SDL_GetBasePath();
        snprintf(B.base, sizeof B.base, "%s", base ? base : "./");
        if (base) SDL_free(base);
        size_t L = strlen(B.base); if (L && (B.base[L - 1] == '/' || B.base[L - 1] == '\\')) B.base[L - 1] = 0;
    }
    const char *dd = getenv("OB_BOX_DATA");
    snprintf(B.data, sizeof B.data, "%s", dd ? dd : B.base);
    if (!dd) { size_t L = strlen(B.data); snprintf(B.data + L, sizeof B.data - L, "/box"); }
    snprintf(B.ini, sizeof B.ini, "%s/box.ini", B.base);
    snprintf(B.art, sizeof B.art, "%s/box-art/%s", B.base, game);
    ini_read();
    if (getenv("OB_BOX_ART")) snprintf(B.art, sizeof B.art, "%s", getenv("OB_BOX_ART"));
    {
        char p[1300]; FILE *f;
        snprintf(p, sizeof p, "%s/%s/scene.txt", B.data, BX_SC->dir);
        if (!(f = fopen(p, "r"))) { fprintf(stderr, "box: %s missing, the box is off\n", p); return 0; }
        fclose(f);
    }
    /* one entry, on the Game page of the overlay and the menu bar: the box with everything in it (manual, disks, case...) */
    ob_host_add_addon(h, OB_ID_ADDON + 40, "View Manual & Materials", menu_box);
    const char *force = getenv("OB_BOX");
    int show = force ? atoi(force) == 1 : (B.show_pref == 2 || (B.show_pref == 0 && !B.seen));
    if (force && atoi(force) == 2) return ob_box_open(BOX_SHOW_REOPEN);
    return show ? ob_box_open(BOX_SHOW_FIRST) : 0;
}
#endif
