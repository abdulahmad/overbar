/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * r3d: software 3D renderer for the box module (see r3d.h). */
#include "overbar/r3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WRITE
#include "../third_party/stb/stb_image.h"

/* ---------------- colour ---------------- */
static float s2l[256];
static uint8_t l2s[4096];
static void color_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (int i = 0; i < 256; i++) {
        float c = i / 255.0f;
        s2l[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i < 4096; i++) {
        float c = i / 4095.0f;
        float s = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f;
        int v = (int)(s * 255 + 0.5f);
        l2s[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}
static inline int enc(float l) { int i = (int)(l * 4095.0f); return l2s[i < 0 ? 0 : i > 4095 ? 4095 : i]; }

/* ---------------- matrices (row major, column vectors: p' = M p) ---------------- */
void r3_m4_mul(float *o, const float *a, const float *b)
{
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    memcpy(o, t, sizeof t);
}
void r3_m4_compose(float *o, const float p[3], const float r[3], const float s[3])
{
    float a = cosf(r[0]), b = sinf(r[0]), c = cosf(r[1]), d = sinf(r[1]), e = cosf(r[2]), f = sinf(r[2]);
    /* three.js Euler XYZ: R = Rx Ry Rz */
    float ae = a * e, af = a * f, be = b * e, bf = b * f;
    float R[9] = { c * e, -c * f, d,
                   af + be * d, ae - bf * d, -b * c,
                   bf - ae * d, be + af * d, a * c };
    for (int i = 0; i < 3; i++) {
        o[i * 4 + 0] = R[i * 3 + 0] * s[0];
        o[i * 4 + 1] = R[i * 3 + 1] * s[1];
        o[i * 4 + 2] = R[i * 3 + 2] * s[2];
        o[i * 4 + 3] = p[i];
    }
    o[12] = o[13] = o[14] = 0; o[15] = 1;
}
static float det3(const float *m) /* upper 3x3 of a 4x4 */
{
    return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}
void r3_m4_decompose(const float *m, float p[3], float r[3], float s[3])
{
    float sx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    float sy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    float sz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    if (det3(m) < 0) sx = -sx;
    p[0] = m[3]; p[1] = m[7]; p[2] = m[11];
    s[0] = sx; s[1] = sy; s[2] = sz;
    float m11 = m[0] / sx, m12 = m[1] / sy, m13 = m[2] / sz;
    float m22 = m[5] / sy, m23 = m[6] / sz;
    float m32 = m[9] / sy, m33 = m[10] / sz;
    float c13 = m13 < -1 ? -1 : m13 > 1 ? 1 : m13;
    r[1] = asinf(c13);
    if (fabsf(c13) < 0.9999999f) { r[0] = atan2f(-m23, m33); r[2] = atan2f(-m12, m11); }
    else { r[0] = atan2f(m32, m22); r[2] = 0; }
}
void r3_m4_inv(float *o, const float *m)
{
    float inv[16], det;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    det = det ? 1.0f / det : 0;
    for (int i = 0; i < 16; i++) o[i] = inv[i] * det;
}

/* ---------------- textures ---------------- */
void r3_tex_set(r3_tex *t, uint32_t *argb, int w, int h)
{
    for (int i = 0; i < t->nlv; i++) free(t->lv[i]);
    t->nlv = 0;
    if (!argb || w < 1 || h < 1) { t->loaded = 0; return; }
    t->w = w; t->h = h; t->lv[0] = argb; t->lw[0] = w; t->lh[0] = h; t->nlv = 1; t->loaded = 1;
    t->alpha = 0;
    for (long i = 0; i < (long)w * h; i++) if ((argb[i] >> 24) != 255) { t->alpha = 1; break; }
    while (t->nlv < 14 && (w > 1 || h > 1)) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        uint32_t *src = t->lv[t->nlv - 1], *d = (uint32_t *)malloc((size_t)nw * nh * 4);
        if (!d) break;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                int x0 = x * 2 < w ? x * 2 : w - 1, x1 = x * 2 + 1 < w ? x * 2 + 1 : w - 1;
                int y0 = y * 2 < h ? y * 2 : h - 1, y1 = y * 2 + 1 < h ? y * 2 + 1 : h - 1;
                uint32_t p[4] = { src[y0 * w + x0], src[y0 * w + x1], src[y1 * w + x0], src[y1 * w + x1] };
                unsigned a = 0, r = 0, g = 0, b = 0;
                for (int k = 0; k < 4; k++) { a += p[k] >> 24; r += (p[k] >> 16) & 255; g += (p[k] >> 8) & 255; b += p[k] & 255; }
                d[y * nw + x] = ((a + 2) / 4) << 24 | ((r + 2) / 4) << 16 | ((g + 2) / 4) << 8 | ((b + 2) / 4);
            }
        t->lv[t->nlv] = d; t->lw[t->nlv] = nw; t->lh[t->nlv] = nh; t->nlv++;
        w = nw; h = nh;
    }
}
static uint32_t *load_file(const char *path, int *w, int *h)
{
    int n;
    unsigned char *p = stbi_load(path, w, h, &n, 4);
    if (!p) return NULL;
    uint32_t *o = (uint32_t *)malloc((size_t)*w * *h * 4);
    if (o) for (long i = 0; i < (long)*w * *h; i++) o[i] = (uint32_t)p[i * 4 + 3] << 24 | (uint32_t)p[i * 4] << 16 | (uint32_t)p[i * 4 + 1] << 8 | p[i * 4 + 2];
    stbi_image_free(p);
    return o;
}
uint32_t *r3_load_image(const char *path, int *w, int *h) { return load_file(path, w, h); }

/* the sleeve panels: the art (or cream) with a half disc cut out of the open (+x) edge */
static uint32_t *notch_tex(uint32_t *art, int aw, int ah, float w, float h, float nr, float ny, unsigned cream, int *ow, int *oh)
{
    int W = 512, H = (int)(512 * h / w + 0.5f);
    uint32_t *o = (uint32_t *)malloc((size_t)W * H * 4);
    if (!o) return NULL;
    float cx = (float)W, cy = H * (0.5f - ny / h), r = nr / w * W;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c;
            if (art) {
                float fx = (x + 0.5f) * aw / W - 0.5f, fy = (y + 0.5f) * ah / H - 0.5f;
                int ix = (int)fx, iy = (int)fy;
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (ix >= aw - 1) ix = aw - 2 < 0 ? 0 : aw - 2;
                if (iy >= ah - 1) iy = ah - 2 < 0 ? 0 : ah - 2;
                float ax = fx - ix, ay = fy - iy;
                if (ax < 0) ax = 0;
                if (ay < 0) ay = 0;
                if (ax > 1) ax = 1;
                if (ay > 1) ay = 1;
                int ix1 = ix + 1 < aw ? ix + 1 : ix, iy1 = iy + 1 < ah ? iy + 1 : iy;
                uint32_t p00 = art[iy * aw + ix], p10 = art[iy * aw + ix1], p01 = art[iy1 * aw + ix], p11 = art[iy1 * aw + ix1];
                c = 0xff000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    float v = ((p00 >> sh) & 255) * (1 - ax) * (1 - ay) + ((p10 >> sh) & 255) * ax * (1 - ay) + ((p01 >> sh) & 255) * (1 - ax) * ay + ((p11 >> sh) & 255) * ax * ay;
                    c |= (uint32_t)(v + 0.5f) << sh;
                }
            } else c = 0xff000000u | cream;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - r;           /* soft edge one pixel wide */
            int a = d <= -0.5f ? 0 : d >= 0.5f ? 255 : (int)((d + 0.5f) * 255);
            o[y * W + x] = (c & 0xffffffu) | (uint32_t)a << 24;
        }
    *ow = W; *oh = H;
    return o;
}

int r3_tex_add(r3_scene *s, uint32_t *argb, int w, int h)
{
    if (s->ntexs >= s->cap_texs) {
        int nc = s->cap_texs ? s->cap_texs * 2 : 16;
        r3_tex *n = (r3_tex *)realloc(s->texs, (size_t)nc * sizeof *n);
        if (!n) return -1;
        memset(n + s->cap_texs, 0, (size_t)(nc - s->cap_texs) * sizeof *n);
        s->texs = n; s->cap_texs = nc;
    }
    r3_tex *t = &s->texs[s->ntexs];
    memset(t, 0, sizeof *t);
    t->rep[0] = t->rep[1] = 1; t->flip_y = 1;
    r3_tex_set(t, argb, w, h);
    return s->ntexs++;
}
int r3_mat_add(r3_scene *s, const r3_mat *m)
{
    if (s->nmats >= s->cap_mats) {
        int nc = s->cap_mats ? s->cap_mats * 2 : 16;
        r3_mat *n = (r3_mat *)realloc(s->mats, (size_t)nc * sizeof *n);
        if (!n) return -1;
        s->mats = n; s->cap_mats = nc;
    }
    s->mats[s->nmats] = *m;
    return s->nmats++;
}

/* ---------------- loading ---------------- */
static void unq(char *s) { for (; *s; s++) if (*s == '_') *s = ' '; }
int r3_load(r3_scene *s, const char *dir, r3_art_fn art, void *ud)
{
    char path[1200], line[2048];
    FILE *f;
    float *bin = NULL;
    long nbin = 0;
    color_init();
    memset(s, 0, sizeof *s);
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    snprintf(path, sizeof path, "%s/scene.vtx", dir);
    if ((f = fopen(path, "rb"))) {
        fseek(f, 0, SEEK_END); nbin = ftell(f) / 4; fseek(f, 0, SEEK_SET);
        bin = (float *)malloc((size_t)nbin * 4 + 4);
        if (bin && fread(bin, 4, (size_t)nbin, f) != (size_t)nbin) nbin = 0;
        fclose(f);
    }
    uint32_t *ib = NULL; long nib = 0;
    snprintf(path, sizeof path, "%s/scene.idx", dir);
    if ((f = fopen(path, "rb"))) {
        fseek(f, 0, SEEK_END); nib = ftell(f) / 4; fseek(f, 0, SEEK_SET);
        ib = (uint32_t *)malloc((size_t)nib * 4 + 4);
        if (ib && fread(ib, 4, (size_t)nib, f) != (size_t)nib) nib = 0;
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/scene.txt", dir);
    if (!(f = fopen(path, "r")) || !bin || !ib) { if (f) fclose(f); free(bin); free(ib); return 0; }
    /* first pass: counts */
    int nn = 0, nm = 0, nt = 0, nmat = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "node ", 5)) nn++;
        else if (!strncmp(line, "mesh ", 5)) nm++;
        else if (!strncmp(line, "tex ", 4)) nt++;
        else if (!strncmp(line, "mat ", 4)) nmat++;
    }
    rewind(f);
    s->nodes = (r3_node *)calloc((size_t)nn + 1, sizeof(r3_node));
    s->meshes = (r3_mesh *)calloc((size_t)nm + 1, sizeof(r3_mesh));
    s->texs = (r3_tex *)calloc((size_t)nt + 1, sizeof(r3_tex)); s->cap_texs = nt + 1;
    s->mats = (r3_mat *)calloc((size_t)nmat + 1, sizeof(r3_mat)); s->cap_mats = nmat + 1;
    s->fov = 30; s->znear = 1; s->zfar = 300;
    while (fgets(line, sizeof line, f)) {
        char kind[16], key[256];
        if (sscanf(line, "camera %f %f %f", &s->fov, &s->znear, &s->zfar) == 3) continue;
        if (!strncmp(line, "tex ", 4)) {
            int i, ws, wt, fy; float ox, oy, rx, ry, w, h, nr, ny; unsigned cream = 0xe9e3d2;
            if (sscanf(line, "tex %d %15s %255s %d %d %f %f %f %f %d", &i, kind, key, &ws, &wt, &ox, &oy, &rx, &ry, &fy) < 10 || i < 0 || i >= nt) continue;
            r3_tex *t = &s->texs[i];
            t->wrap_s = ws; t->wrap_t = wt; t->off[0] = ox; t->off[1] = oy; t->rep[0] = rx; t->rep[1] = ry; t->flip_y = fy;
            { size_t n = strlen(key); if (n >= sizeof t->key) n = sizeof t->key - 1; memcpy(t->key, key, n); t->key[n] = 0; }
            uint32_t *px = NULL; int pw = 0, ph = 0;
            if (!strcmp(kind, "file")) { char p2[1400]; snprintf(p2, sizeof p2, "%s/%s", dir, key); px = load_file(p2, &pw, &ph); }
            else if (!strcmp(kind, "art")) { if (art) px = art(key, &pw, &ph, ud); }
            else if (!strcmp(kind, "notch")) {
                char *q = line; int sp = 0;
                while (*q && sp < 11) { if (*q == ' ') sp++; q++; }
                if (sscanf(q, "%f %f %f %f %u", &w, &h, &nr, &ny, &cream) >= 4) {
                    uint32_t *a = NULL; int aw = 0, ah = 0;
                    if (strcmp(key, "-") && art) a = art(key, &aw, &ah, ud);
                    if (strcmp(key, "-") && !a) { px = NULL; }
                    else px = notch_tex(a, aw, ah, w, h, nr, ny, cream, &pw, &ph);
                    free(a);
                }
            }
            if (px) r3_tex_set(t, px, pw, ph);
            if (i >= s->ntexs) s->ntexs = i + 1;
            continue;
        }
        if (!strncmp(line, "mat ", 4)) {
            int i; r3_mat m; memset(&m, 0, sizeof m); char ty[4];
            if (sscanf(line, "mat %d %3s %f %f %f %d %f %d %f %d %d %d %f %f %f %f %f %f %f %d", &i, ty, &m.col[0], &m.col[1], &m.col[2], &m.map, &m.opacity, &m.transparent,
                       &m.alpha_test, &m.side, &m.depth_write, &m.visible, &m.spec[0], &m.spec[1], &m.spec[2], &m.shin, &m.emis[0], &m.emis[1], &m.emis[2], &m.poff) < 20 || i < 0 || i >= nmat) continue;
            m.type = ty[0]; m.bright = 1;
            s->mats[i] = m;
            if (i >= s->nmats) s->nmats = i + 1;
            continue;
        }
        if (!strncmp(line, "node ", 5)) {
            int i, par, vis; r3_node n; memset(&n, 0, sizeof n); char nm2[R3_NAME], id[24], ord[8];
            int used = 0;
            if (sscanf(line, "node %d %d %47s %23s %f %f %f %f %f %f %7s %f %f %f %d %f %f %n", &i, &par, nm2, id, &n.pos[0], &n.pos[1], &n.pos[2], &n.rot[0], &n.rot[1], &n.rot[2], ord,
                       &n.scale[0], &n.scale[1], &n.scale[2], &vis, &n.on_x, &n.off_x, &used) < 17 || i < 0 || i >= nn) continue;
            n.parent = par; n.vis = vis; n.hl = 1; n.opacity = 1;
            snprintf(n.name, sizeof n.name, "%s", strcmp(nm2, "-") ? nm2 : "");
            snprintf(n.id, sizeof n.id, "%s", strcmp(id, "-") ? id : "");
            if (used > 0) { char *lb = line + used; size_t L = strlen(lb); while (L && (lb[L - 1] == '\n' || lb[L - 1] == '\r')) lb[--L] = 0; snprintf(n.label, sizeof n.label, "%s", lb); }
            unq(n.id);
            s->nodes[i] = n;
            if (i >= s->nnodes) s->nnodes = i + 1;
            continue;
        }
        if (!strncmp(line, "mesh ", 5)) {
            int i, node, mat, vfirst, nv, ifirst, ni, ro = 0;
            if (sscanf(line, "mesh %d %d %d %d %d %d %d %d", &i, &node, &mat, &vfirst, &nv, &ifirst, &ni, &ro) < 7 || i < 0 || i >= nm) continue;
            if (nv <= 0 || ni <= 0 || (long)(vfirst + nv) * 8 > nbin || (long)ifirst + ni > nib) continue;
            r3_mesh *m = &s->meshes[s->nmeshes];
            m->node = node; m->mat = mat; m->ro = ro; m->mat_override = -1;
            m->v = (float *)malloc((size_t)nv * 8 * sizeof(float));
            m->idx = (uint32_t *)malloc((size_t)ni * sizeof(uint32_t));
            if (!m->v || !m->idx) { free(m->v); free(m->idx); m->v = NULL; m->idx = NULL; continue; }
            memcpy(m->v, bin + (size_t)vfirst * 8, (size_t)nv * 8 * sizeof(float));
            int bad = 0;
            for (int k = 0; k < ni; k++) { uint32_t x = ib[ifirst + k]; if (x >= (uint32_t)nv) { bad = 1; x = 0; } m->idx[k] = x; }
            (void)bad;
            m->nv = nv; m->ni = ni;
            s->nmeshes++;
            continue;
        }
        if (!strncmp(line, "light ", 6) && s->nlights < 8) {
            r3_light *l = &s->lights[s->nlights]; char k[4]; float p[3], t[3];
            if (sscanf(line, "light %3s %f %f %f %f %f %f %f %f %f %f %f %f %f", k, &l->col[0], &l->col[1], &l->col[2], &l->I, &l->ground[0], &l->ground[1], &l->ground[2],
                       &p[0], &p[1], &p[2], &t[0], &t[1], &t[2]) < 14) continue;
            l->kind = k[0];
            float d[3] = { p[0] - t[0], p[1] - t[1], p[2] - t[2] }, L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (L > 0) for (int j = 0; j < 3; j++) l->dir[j] = d[j] / L;
            s->nlights++;
        }
    }
    fclose(f);
    free(bin); free(ib);
    return s->nnodes > 0;
}

void r3_free(r3_scene *s)
{
    for (int i = 0; i < s->nmeshes; i++) { free(s->meshes[i].v); free(s->meshes[i].idx); }
    for (int i = 0; i < s->ntexs; i++) for (int k = 0; k < s->texs[i].nlv; k++) free(s->texs[i].lv[k]);
    free(s->nodes); free(s->meshes); free(s->mats); free(s->texs);
    memset(s, 0, sizeof *s);
}

int r3_find(const r3_scene *s, const char *name)
{
    for (int i = 0; i < s->nnodes; i++) if (!strcmp(s->nodes[i].name, name)) return i;
    return -1;
}
int r3_find_id(const r3_scene *s, const char *id)
{
    for (int i = 0; i < s->nnodes; i++) if (!strcmp(s->nodes[i].id, id)) return i;
    return -1;
}

/* ---------------- transforms ---------------- */
static void world_of(r3_scene *s, int i)
{
    r3_node *n = &s->nodes[i];
    if (n->stamp == s->stamp) return;
    float L[16];
    r3_m4_compose(L, n->pos, n->rot, n->scale);
    if (n->parent >= 0) { world_of(s, n->parent); r3_m4_mul(n->world, s->nodes[n->parent].world, L); }
    else memcpy(n->world, L, sizeof L);
    n->stamp = s->stamp;
}
void r3_update(r3_scene *s)
{
    s->stamp++;
    if (!s->stamp) s->stamp = 1;
    for (int i = 0; i < s->nnodes; i++) world_of(s, i);
}
const float *r3_world(r3_scene *s, int node) { s->stamp++; if (!s->stamp) s->stamp = 1; world_of(s, node); return s->nodes[node].world; }
void r3_world_pos(r3_scene *s, int node, float out[3]) { const float *w = r3_world(s, node); out[0] = w[3]; out[1] = w[7]; out[2] = w[11]; }
void r3_attach(r3_scene *s, int node, int parent)
{
    float W[16], P[16], Pi[16], L[16];
    if (node < 0 || node == parent) return;
    memcpy(W, r3_world(s, node), sizeof W);
    if (parent >= 0) { memcpy(P, r3_world(s, parent), sizeof P); r3_m4_inv(Pi, P); r3_m4_mul(L, Pi, W); }
    else memcpy(L, W, sizeof L);
    r3_node *n = &s->nodes[node];
    r3_m4_decompose(L, n->pos, n->rot, n->scale);
    n->parent = parent;
    s->stamp++;
}
int r3_visible(const r3_scene *s, int i)
{
    while (i >= 0) { if (!s->nodes[i].vis) return 0; i = s->nodes[i].parent; }
    return 1;
}
int r3_is_under(const r3_scene *s, int i, int a)
{
    while (i >= 0) { if (i == a) return 1; i = s->nodes[i].parent; }
    return 0;
}

/* ---------------- camera ---------------- */
typedef struct { float v[16]; float fy, fx, cx, cy; } cam_t;
static void cam_make(r3_scene *s, const r3_target *t, cam_t *c)
{
    float f[3] = { s->cam[0] - s->look[0], s->cam[1] - s->look[1], s->cam[2] - s->look[2] }; /* camera's +z (it looks down -z) */
    float L = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (L < 1e-6f) { f[0] = 0; f[1] = 0; f[2] = 1; L = 1; }
    for (int i = 0; i < 3; i++) f[i] /= L;
    float up[3] = { 0, 1, 0 };
    float x[3] = { up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0] };
    L = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); if (L < 1e-6f) { x[0] = 1; x[1] = 0; x[2] = 0; L = 1; }
    for (int i = 0; i < 3; i++) x[i] /= L;
    float y[3] = { f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0] };
    float *v = c->v;
    v[0] = x[0]; v[1] = x[1]; v[2] = x[2]; v[3] = -(x[0] * s->cam[0] + x[1] * s->cam[1] + x[2] * s->cam[2]);
    v[4] = y[0]; v[5] = y[1]; v[6] = y[2]; v[7] = -(y[0] * s->cam[0] + y[1] * s->cam[1] + y[2] * s->cam[2]);
    v[8] = f[0]; v[9] = f[1]; v[10] = f[2]; v[11] = -(f[0] * s->cam[0] + f[1] * s->cam[1] + f[2] * s->cam[2]);
    v[12] = v[13] = v[14] = 0; v[15] = 1;
    /* the scenes were framed for a 16:9 stage: a narrower window keeps that width and shows more above and below */
    float aspect = (float)t->w / (t->h > 0 ? t->h : 1);
    float th = tanf(s->fov * 0.5f * 3.14159265f / 180.0f);
    float tw = th * (16.0f / 9.0f);
    if (aspect >= 16.0f / 9.0f) tw = th * aspect; else th = tw / aspect;
    c->fx = t->w * 0.5f / tw;
    c->fy = t->h * 0.5f / th;
    c->cx = t->w * 0.5f; c->cy = t->h * 0.5f;
}
int r3_project(r3_scene *s, const r3_target *t, const float p[3], float *x, float *y)
{
    cam_t c; cam_make(s, t, &c);
    float vx = c.v[0] * p[0] + c.v[1] * p[1] + c.v[2] * p[2] + c.v[3];
    float vy = c.v[4] * p[0] + c.v[5] * p[1] + c.v[6] * p[2] + c.v[7];
    float vz = c.v[8] * p[0] + c.v[9] * p[1] + c.v[10] * p[2] + c.v[11];
    if (vz > -1e-4f) return 0;
    *x = c.cx + vx * c.fx / -vz; *y = c.cy - vy * c.fy / -vz;
    return 1;
}
int r3_screen_box(r3_scene *s, const r3_target *t, int node, float *x0, float *y0, float *x1, float *y1)
{
    cam_t c; cam_make(s, t, &c);
    int any = 0;
    r3_update(s);
    *x0 = *y0 = 1e9f; *x1 = *y1 = -1e9f;
    for (int i = 0; i < s->nmeshes; i++) {
        r3_mesh *m = &s->meshes[i];
        if (!r3_is_under(s, m->node, node) || !r3_visible(s, m->node)) continue;
        const float *W = s->nodes[m->node].world;
        float M[16]; r3_m4_mul(M, c.v, W);
        int step = m->nv > 400 ? m->nv / 200 : 1;
        for (int k = 0; k < m->nv; k += step) {
            const float *v = m->v + (size_t)k * 8;
            float vx = M[0] * v[0] + M[1] * v[1] + M[2] * v[2] + M[3];
            float vy = M[4] * v[0] + M[5] * v[1] + M[6] * v[2] + M[7];
            float vz = M[8] * v[0] + M[9] * v[1] + M[10] * v[2] + M[11];
            if (vz > -1e-3f) continue;
            float sx = c.cx + vx * c.fx / -vz, sy = c.cy - vy * c.fy / -vz;
            if (sx < *x0) *x0 = sx;
            if (sx > *x1) *x1 = sx;
            if (sy < *y0) *y0 = sy;
            if (sy > *y1) *y1 = sy;
            any = 1;
        }
    }
    return any;
}

/* ---------------- rasterizer ---------------- */
typedef struct { float x, y, z, iw, u, v, r, g, b, sr, sg, sb; } pv;   /* screen x y, view z, 1/w, u/w, v/w, light (gouraud) */
typedef struct {
    r3_target *t; const r3_tex *tex; int lv; const r3_mat *m; float opa; int blend, zw; int16_t pick; float zbias;
    float bright;
    int y0, y1;                 /* the rows this call draws */
} rctx;

static inline uint32_t sample(const r3_tex *t, int lv, float u, float v)
{
    int w = t->lw[lv], h = t->lh[lv];
    const uint32_t *p = t->lv[lv];
    u = u * t->rep[0] + t->off[0];
    v = v * t->rep[1] + t->off[1];
    if (t->flip_y) v = 1 - v;
    float fx = u * w - 0.5f, fy = v * h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (t->wrap_s) { x0 = ((x0 % w) + w) % w; x1 = ((x1 % w) + w) % w; }
    else { x0 = x0 < 0 ? 0 : x0 >= w ? w - 1 : x0; x1 = x1 < 0 ? 0 : x1 >= w ? w - 1 : x1; }
    if (t->wrap_t) { y0 = ((y0 % h) + h) % h; y1 = ((y1 % h) + h) % h; }
    else { y0 = y0 < 0 ? 0 : y0 >= h ? h - 1 : y0; y1 = y1 < 0 ? 0 : y1 >= h ? h - 1 : y1; }
    uint32_t a = p[y0 * w + x0], b = p[y0 * w + x1], c = p[y1 * w + x0], d = p[y1 * w + x1];
    int wa = (int)((1 - ax) * (1 - ay) * 256), wb = (int)(ax * (1 - ay) * 256), wc = (int)((1 - ax) * ay * 256);
    int wd = 256 - wa - wb - wc;
    uint32_t o = 0;
    for (int sh = 0; sh < 32; sh += 8)
        o |= (uint32_t)((((a >> sh) & 255) * wa + ((b >> sh) & 255) * wb + ((c >> sh) & 255) * wc + ((d >> sh) & 255) * wd) >> 8) << sh;
    return o;
}

static void raster(rctx *R, const pv *a, const pv *b, const pv *c)
{
    r3_target *t = R->t;
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-9f) return;
    int minx = (int)floorf(fminf(a->x, fminf(b->x, c->x))), maxx = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int miny = (int)floorf(fminf(a->y, fminf(b->y, c->y))), maxy = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > t->w - 1) maxx = t->w - 1;
    if (maxy > t->h - 1) maxy = t->h - 1;
    if (miny < R->y0) miny = R->y0;
    if (maxy > R->y1 - 1) maxy = R->y1 - 1;
    if (minx > maxx || miny > maxy) return;
    float ia = 1.0f / area;
    /* barycentric planes: attr = A + Bx * x + By * y */
#define PLANE(fa, fb, fc, A0, BX, BY) do { float d1 = (fb) - (fa), d2 = (fc) - (fa); \
        BX = (d1 * (c->y - a->y) - d2 * (b->y - a->y)) * ia; BY = (d2 * (b->x - a->x) - d1 * (c->x - a->x)) * ia; A0 = (fa) - BX * a->x - BY * a->y; } while (0)
    float iwA, iwX, iwY, uA, uX, uY, vA, vX, vY, rA, rX, rY, gA, gX, gY, bA, bX, bY, srA, srX, srY, sgA, sgX, sgY, sbA, sbX, sbY;
    PLANE(a->iw, b->iw, c->iw, iwA, iwX, iwY);
    PLANE(a->u, b->u, c->u, uA, uX, uY);
    PLANE(a->v, b->v, c->v, vA, vX, vY);
    PLANE(a->r, b->r, c->r, rA, rX, rY);
    PLANE(a->g, b->g, c->g, gA, gX, gY);
    PLANE(a->b, b->b, c->b, bA, bX, bY);
    PLANE(a->sr, b->sr, c->sr, srA, srX, srY);
    PLANE(a->sg, b->sg, c->sg, sgA, sgX, sgY);
    PLANE(a->sb, b->sb, c->sb, sbA, sbX, sbY);
#undef PLANE
    /* edge functions, top-left rule by a small bias */
    float e0a = b->y - c->y, e0b = c->x - b->x, e0c = b->x * c->y - b->y * c->x;
    float e1a = c->y - a->y, e1b = a->x - c->x, e1c = c->x * a->y - c->y * a->x;
    float e2a = a->y - b->y, e2b = b->x - a->x, e2c = a->x * b->y - a->y * b->x;
    float sgn = area > 0 ? 1.0f : -1.0f;
    e0a *= sgn; e0b *= sgn; e0c *= sgn; e1a *= sgn; e1b *= sgn; e1c *= sgn; e2a *= sgn; e2b *= sgn; e2c *= sgn;
    const r3_tex *tx = R->tex; int lv = R->lv;
    /* texels per uv unit, squared, for the per-pixel footprint */
    float tw2 = 0, th2 = 0;
    if (tx) { float tu = tx->w * tx->rep[0], tv = tx->h * tx->rep[1]; tw2 = tu * tu; th2 = tv * tv; }
    const r3_mat *m = R->m;
    float opa = R->opa, at = m->alpha_test, br = R->bright;
    for (int y = miny; y <= maxy; y++) {
        float py = y + 0.5f;
        float w0 = e0a * (minx + 0.5f) + e0b * py + e0c, w1 = e1a * (minx + 0.5f) + e1b * py + e1c, w2 = e2a * (minx + 0.5f) + e2b * py + e2c;
        /* skip to where the row enters the triangle */
        int x = minx, xend = maxx;
        {
            float lo = (float)minx, hi = (float)maxx + 1;
            float ea[3] = { e0a, e1a, e2a }, ew[3] = { w0, w1, w2 };
            for (int k = 0; k < 3; k++) {
                if (ea[k] > 1e-9f) { float x0 = minx - ew[k] / ea[k]; if (x0 > lo) lo = x0; }
                else if (ea[k] < -1e-9f) { float x1 = minx - ew[k] / ea[k]; if (x1 < hi) hi = x1; }
                else if (ew[k] < 0) { lo = 1e9f; }
            }
            int xs = (int)floorf(lo) - 1, xe = (int)ceilf(hi) + 1;
            if (xs > x) x = xs;
            if (xe < xend) xend = xe;
            if (x > xend) continue;
            w0 += e0a * (x - minx); w1 += e1a * (x - minx); w2 += e2a * (x - minx);
        }
        uint32_t *row = t->px + (size_t)y * t->pitch;
        float *zr = t->z + (size_t)y * t->w;
        int16_t *pr = t->pick + (size_t)y * t->w;
        for (; x <= xend; x++, w0 += e0a, w1 += e1a, w2 += e2a) {
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float px = x + 0.5f;
            float iw = iwA + iwX * px + iwY * py;
            float zv = iw * R->zbias;
            if (zv <= zr[x]) continue;
            float wv = 1.0f / iw;
            float lr = (rA + rX * px + rY * py) * wv, lg = (gA + gX * px + gY * py) * wv, lb = (bA + bX * px + bY * py) * wv;
            float cr = 1, cg = 1, cb = 1, ca = 1;
            if (tx) {
                float u = (uA + uX * px + uY * py) * wv, v = (vA + vX * px + vY * py) * wv;
                uint32_t s;
                if (tx->nlv > 1) {
                    /* this pixel's footprint in texels: the mip level from its short side, up to 4 taps along its long side */
                    float dux = (uX - u * iwX) * wv, dvx = (vX - v * iwX) * wv, duy = (uY - u * iwY) * wv, dvy = (vY - v * iwY) * wv;
                    float ax2 = dux * dux * tw2 + dvx * dvx * th2, ay2 = duy * duy * tw2 + dvy * dvy * th2;
                    float mj2 = ax2 > ay2 ? ax2 : ay2, mn2 = ax2 > ay2 ? ay2 : ax2;
                    int taps = 1;
                    if (mn2 * 16 < mj2) taps = 4;
                    else if (mn2 * 4 < mj2) taps = (mn2 * 9 < mj2) ? 3 : 2;
                    float fp2 = mj2 / (float)(taps * taps);
                    if (fp2 < mn2) fp2 = mn2;
                    /* log2 of the footprint (half of log2 of its square), sharpened a quarter level */
                    int e; float fm = frexpf(fp2, &e);
                    float l2 = (float)(e - 1) + (fm - 0.5f) * 2.0f;        /* log2(fp2), close enough between powers of two */
                    int lvp = (int)(l2 * 0.5f + 0.25f);
                    lvp = lvp < 0 ? 0 : lvp >= tx->nlv ? tx->nlv - 1 : lvp;
                    if (taps == 1) s = sample(tx, lvp, u, v);
                    else {
                        float su = ax2 > ay2 ? dux : duy, sv = ax2 > ay2 ? dvx : dvy;
                        unsigned A = 0, Rr = 0, G = 0, Bb = 0;
                        for (int k = 0; k < taps; k++) {
                            float o = ((float)k + 0.5f) / (float)taps - 0.5f;
                            uint32_t q = sample(tx, lvp, u + su * o, v + sv * o);
                            A += q >> 24; Rr += (q >> 16) & 255; G += (q >> 8) & 255; Bb += q & 255;
                        }
                        s = (A / taps) << 24 | (Rr / taps) << 16 | (G / taps) << 8 | (Bb / taps);
                    }
                } else s = sample(tx, lv, u, v);
                ca = (s >> 24) / 255.0f;
                cr = s2l[(s >> 16) & 255]; cg = s2l[(s >> 8) & 255]; cb = s2l[s & 255];
            }
            ca *= opa;
            if (at > 0 && ca < at) continue;
            float fr = (cr * lr + (srA + srX * px + srY * py) * wv) * br;
            float fg = (cg * lg + (sgA + sgX * px + sgY * py) * wv) * br;
            float fb = (cb * lb + (sbA + sbX * px + sbY * py) * wv) * br;
            int R8 = enc(fr), G8 = enc(fg), B8 = enc(fb);
            if (R->blend && ca < 0.999f) {
                if (ca <= 0.002f) continue;
                uint32_t d = row[x];
                int ia8 = (int)(ca * 256);
                R8 = (((int)((d >> 16) & 255) * (256 - ia8)) + R8 * ia8) >> 8;
                G8 = (((int)((d >> 8) & 255) * (256 - ia8)) + G8 * ia8) >> 8;
                B8 = (((int)(d & 255) * (256 - ia8)) + B8 * ia8) >> 8;
            }
            row[x] = 0xff000000u | (uint32_t)R8 << 16 | (uint32_t)G8 << 8 | (uint32_t)B8;
            if (R->zw) { zr[x] = zv; pr[x] = R->pick; }
        }
    }
}

/* a triangle in view space: clip against the near plane, project, raster */
static void tri_view(rctx *R, const cam_t *c, float znear, pv in[3])
{
    pv poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const pv *p = &in[i], *q = &in[(i + 1) % 3];
        int pin = p->z <= -znear, qin = q->z <= -znear;
        if (pin) poly[n++] = *p;
        if (pin != qin) {
            float t = (-znear - p->z) / (q->z - p->z);
            pv r;
            float *rp = (float *)&r; const float *pp = (const float *)p, *qq = (const float *)q;
            for (int k = 0; k < (int)(sizeof(pv) / sizeof(float)); k++) rp[k] = pp[k] + (qq[k] - pp[k]) * t;
            poly[n++] = r;
        }
    }
    if (n < 3) return;
    for (int i = 0; i < n; i++) {
        pv *p = &poly[i];
        float iw = 1.0f / -p->z;
        p->x = c->cx + p->x * c->fx * iw;
        p->y = c->cy - p->y * c->fy * iw;
        p->iw = iw; p->u *= iw; p->v *= iw; p->r *= iw; p->g *= iw; p->b *= iw; p->sr *= iw; p->sg *= iw; p->sb *= iw;
    }
    raster(R, &poly[0], &poly[1], &poly[2]);
    if (n == 4) raster(R, &poly[0], &poly[2], &poly[3]);
}

typedef struct { int mesh; float depth; } draw_item;
static int by_depth(const void *a, const void *b) { float d = ((const draw_item *)a)->depth - ((const draw_item *)b)->depth; return d < 0 ? -1 : d > 0; }

#if defined(_MSC_VER)
#define R3_TLS __declspec(thread)
#else
#define R3_TLS _Thread_local
#endif
static R3_TLS float *vbuf; static R3_TLS int vcap;

static void draw_mesh(r3_scene *s, r3_target *t, const cam_t *c, int mi, int y0, int y1)
{
    r3_mesh *me = &s->meshes[mi];
    const r3_mat *m = &s->mats[me->mat_override >= 0 ? me->mat_override : me->mat];
    r3_node *nd = &s->nodes[me->node];
    const float *W = nd->world;
    float MV[16]; r3_m4_mul(MV, c->v, W);
    /* the normal matrix (world): inverse transpose of W's 3x3 */
    float Wi[16]; r3_m4_inv(Wi, W);
    float N[9] = { Wi[0], Wi[4], Wi[8], Wi[1], Wi[5], Wi[9], Wi[2], Wi[6], Wi[10] };
    int mirror = det3(W) < 0;
    if (me->nv * 12 > vcap) { vcap = me->nv * 12 + 1024; vbuf = (float *)realloc(vbuf, (size_t)vcap * sizeof(float)); if (!vbuf) { vcap = 0; return; } }
    /* opacity of the node chain */
    float opa = m->opacity;
    for (int k = me->node; k >= 0; k = s->nodes[k].parent) opa *= s->nodes[k].opacity;
    float bright = m->bright;
    for (int k = me->node; k >= 0; k = s->nodes[k].parent) bright *= s->nodes[k].hl;
    const r3_tex *tx = (m->map >= 0 && m->map < s->ntexs && s->texs[m->map].loaded) ? &s->texs[m->map] : NULL;
    float base[3] = { m->col[0], m->col[1], m->col[2] };
    if (m->map >= 0 && !tx) { base[0] *= 0.82f; base[1] *= 0.80f; base[2] *= 0.76f; }   /* missing scan: plain card */
    /* lights in world space */
    float amb[3] = { 0, 0, 0 };
    for (int l = 0; l < s->nlights; l++) if (s->lights[l].kind == 'A') for (int k = 0; k < 3; k++) amb[k] += s->lights[l].col[k] * s->lights[l].I;
    for (int v = 0; v < me->nv; v++) {
        const float *p = me->v + (size_t)v * 8;
        float *o = vbuf + (size_t)v * 12;
        o[0] = MV[0] * p[0] + MV[1] * p[1] + MV[2] * p[2] + MV[3];
        o[1] = MV[4] * p[0] + MV[5] * p[1] + MV[6] * p[2] + MV[7];
        o[2] = MV[8] * p[0] + MV[9] * p[1] + MV[10] * p[2] + MV[11];
        o[3] = p[6]; o[4] = p[7];
        float lr = 1, lg = 1, lb = 1, sr = 0, sg = 0, sb = 0;
        if (m->type != 'B') {
            float n0 = N[0] * p[3] + N[1] * p[4] + N[2] * p[5], n1 = N[3] * p[3] + N[4] * p[4] + N[5] * p[5], n2 = N[6] * p[3] + N[7] * p[4] + N[8] * p[5];
            float L = sqrtf(n0 * n0 + n1 * n1 + n2 * n2); if (L > 0) { n0 /= L; n1 /= L; n2 /= L; }
            /* a face seen from behind (double sided, back side) lights with its normal turned toward the viewer */
            float wp[3] = { W[0] * p[0] + W[1] * p[1] + W[2] * p[2] + W[3], W[4] * p[0] + W[5] * p[1] + W[6] * p[2] + W[7], W[8] * p[0] + W[9] * p[1] + W[10] * p[2] + W[11] };
            float vv[3] = { s->cam[0] - wp[0], s->cam[1] - wp[1], s->cam[2] - wp[2] };
            float VL = sqrtf(vv[0] * vv[0] + vv[1] * vv[1] + vv[2] * vv[2]); if (VL > 0) { vv[0] /= VL; vv[1] /= VL; vv[2] /= VL; }
            if (m->side != 0 && n0 * vv[0] + n1 * vv[1] + n2 * vv[2] < 0) { n0 = -n0; n1 = -n1; n2 = -n2; }
            lr = amb[0]; lg = amb[1]; lb = amb[2];
            for (int l = 0; l < s->nlights; l++) {
                const r3_light *li = &s->lights[l];
                if (li->kind == 'H') {
                    float k = 0.5f * n1 + 0.5f;
                    lr += (li->ground[0] + (li->col[0] - li->ground[0]) * k) * li->I;
                    lg += (li->ground[1] + (li->col[1] - li->ground[1]) * k) * li->I;
                    lb += (li->ground[2] + (li->col[2] - li->ground[2]) * k) * li->I;
                } else if (li->kind == 'D') {
                    float d = n0 * li->dir[0] + n1 * li->dir[1] + n2 * li->dir[2];
                    if (d <= 0) continue;
                    lr += li->col[0] * li->I * d; lg += li->col[1] * li->I * d; lb += li->col[2] * li->I * d;
                    if (m->type == 'P' && m->shin > 0) {
                        float h[3] = { li->dir[0] + vv[0], li->dir[1] + vv[1], li->dir[2] + vv[2] };
                        float HL = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]); if (HL > 0) { h[0] /= HL; h[1] /= HL; h[2] /= HL; }
                        float nh = n0 * h[0] + n1 * h[1] + n2 * h[2];
                        if (nh > 0) {
                            float vh = li->dir[0] * h[0] + li->dir[1] * h[1] + li->dir[2] * h[2];
                            float fr = powf(1 - (vh < 0 ? 0 : vh), 5);
                            float D = 0.25f * (0.5f * m->shin + 1) * powf(nh, m->shin) * d * li->I;
                            sr += (m->spec[0] + (1 - m->spec[0]) * fr) * D * li->col[0];
                            sg += (m->spec[1] + (1 - m->spec[1]) * fr) * D * li->col[1];
                            sb += (m->spec[2] + (1 - m->spec[2]) * fr) * D * li->col[2];
                        }
                    }
                }
            }
        }
        o[5] = lr * base[0] + m->emis[0]; o[6] = lg * base[1] + m->emis[1]; o[7] = lb * base[2] + m->emis[2];
        o[8] = sr; o[9] = sg; o[10] = sb;
    }
    rctx R;
    memset(&R, 0, sizeof R);
    R.t = t; R.tex = tx; R.m = m; R.opa = opa;
    R.blend = m->transparent || opa < 0.999f;
    R.zw = m->depth_write && !(opa < 0.5f);
    R.pick = (int16_t)me->node;
    R.zbias = m->poff ? 1.0005f : 1.0f;
    R.bright = bright;
    R.y0 = y0; R.y1 = y1;
    /* one mip level for the mesh: texels per pixel at its middle */
    R.lv = 0;
    if (tx && me->ni >= 3) {
        double ta = 0, sa = 0;
        int step = me->ni / 3 > 64 ? (me->ni / 3) / 64 : 1;
        for (int f = 0; f + 2 < me->ni; f += 3 * step) {
            const float *A = vbuf + me->idx[f] * 12, *B = vbuf + me->idx[f + 1] * 12, *C = vbuf + me->idx[f + 2] * 12;
            if (A[2] > -s->znear || B[2] > -s->znear || C[2] > -s->znear) continue;
            float ax = A[0] / -A[2] * c->fx, ay = A[1] / -A[2] * c->fy, bx = B[0] / -B[2] * c->fx, by = B[1] / -B[2] * c->fy, cx = C[0] / -C[2] * c->fx, cy = C[1] / -C[2] * c->fy;
            sa += fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
            float u0 = A[3] * tx->rep[0] * tx->w, v0 = A[4] * tx->rep[1] * tx->h, u1 = B[3] * tx->rep[0] * tx->w, v1 = B[4] * tx->rep[1] * tx->h, u2 = C[3] * tx->rep[0] * tx->w, v2 = C[4] * tx->rep[1] * tx->h;
            ta += fabs((u1 - u0) * (v2 - v0) - (v1 - v0) * (u2 - u0));
        }
        if (sa > 0 && ta > 0) {
            float lod = 0.5f * log2f((float)(ta / sa));
            int l = (int)(lod + 0.25f);
            R.lv = l < 0 ? 0 : l >= tx->nlv ? tx->nlv - 1 : l;
        }
    }
    int cull = m->side == 0 ? 1 : m->side == 1 ? -1 : 0;   /* 1: drop back faces, -1: drop front faces */
    if (mirror) cull = -cull;
    for (int f = 0; f + 2 < me->ni; f += 3) {
        const float *A = vbuf + me->idx[f] * 12, *B = vbuf + me->idx[f + 1] * 12, *C = vbuf + me->idx[f + 2] * 12;
        if (A[2] > -s->znear && B[2] > -s->znear && C[2] > -s->znear) continue;
        if (cull) {
            /* facing in view space: (B-A) x (C-A) . A < 0 is front facing for counter-clockwise faces */
            float e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
            float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
            float d = nx * A[0] + ny * A[1] + nz * A[2];
            if (cull > 0 && d >= 0) continue;
            if (cull < 0 && d <= 0) continue;
        }
        pv in[3];
        const float *P[3] = { A, B, C };
        for (int k = 0; k < 3; k++) {
            in[k].x = P[k][0]; in[k].y = P[k][1]; in[k].z = P[k][2]; in[k].iw = 0;
            in[k].u = P[k][3]; in[k].v = P[k][4];
            in[k].r = P[k][5]; in[k].g = P[k][6]; in[k].b = P[k][7];
            in[k].sr = P[k][8]; in[k].sg = P[k][9]; in[k].sb = P[k][10];
        }
        tri_view(&R, c, s->znear, in);
    }
}

int r3_render_begin(r3_scene *s, r3_target *t)
{
    color_init();
    if (t->cap < t->w * t->h) {
        free(t->z); free(t->pick);
        t->z = (float *)malloc((size_t)t->w * t->h * sizeof(float));
        t->pick = (int16_t *)malloc((size_t)t->w * t->h * sizeof(int16_t));
        t->cap = t->z && t->pick ? t->w * t->h : 0;
        if (!t->cap) return 0;
    }
    r3_update(s);
    return 1;
}

void r3_render(r3_scene *s, r3_target *t, const uint32_t *bg)
{
    if (r3_render_begin(s, t)) r3_render_rows(s, t, bg, 0, t->h);
}

void r3_render_rows(r3_scene *s, r3_target *t, const uint32_t *bg, int y0, int y1)
{
    if (!t->cap || t->cap < t->w * t->h) return;
    if (y0 < 0) y0 = 0;
    if (y1 > t->h) y1 = t->h;
    if (y0 >= y1) return;
    for (int i = y0 * t->w; i < y1 * t->w; i++) { t->z[i] = 0; t->pick[i] = -1; }
    if (bg) for (int y = y0; y < y1; y++) memcpy(t->px + (size_t)y * t->pitch, bg + (size_t)y * t->w, (size_t)t->w * 4);
    cam_t c; cam_make(s, t, &c);
    static R3_TLS draw_item *items; static R3_TLS int icap;
    if (icap < s->nmeshes) { icap = s->nmeshes + 64; items = (draw_item *)realloc(items, (size_t)icap * sizeof *items); if (!items) { icap = 0; return; } }
    int no = 0, ntr = 0;
    /* opaque first, by render order; then the see-through ones far to near */
    for (int pass = 0; pass < 2; pass++) {
        int n = 0;
        for (int i = 0; i < s->nmeshes; i++) {
            r3_mesh *me = &s->meshes[i];
            const r3_mat *m = &s->mats[me->mat_override >= 0 ? me->mat_override : me->mat];
            if (!m->visible || !r3_visible(s, me->node)) continue;
            float opa = m->opacity;
            for (int k = me->node; k >= 0; k = s->nodes[k].parent) opa *= s->nodes[k].opacity;
            if (opa <= 0.004f) continue;
            int tr = m->transparent || opa < 0.999f;
            if (tr && m->alpha_test > 0 && opa >= 0.999f) tr = 0;   /* cut-outs draw with the opaque ones */
            if (tr != pass) continue;
            const float *W = s->nodes[me->node].world;
            float vz = c.v[8] * W[3] + c.v[9] * W[7] + c.v[10] * W[11] + c.v[11];
            items[n].mesh = i;
            items[n].depth = pass ? vz + me->ro * 1000.0f : me->ro * 1000.0f - vz;   /* opaque near to far: hidden pixels stop at the depth test */
            n++;
        }
        qsort(items, (size_t)n, sizeof *items, by_depth);
        for (int i = 0; i < n; i++) draw_mesh(s, t, &c, items[i].mesh, y0, y1);
        if (pass) ntr = n; else no = n;
    }
    (void)no; (void)ntr;
}
