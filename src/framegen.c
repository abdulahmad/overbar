/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Frame generation (include/overbar/framegen.h): a small reader and runner for the ncnn graph of RIFE 4.6
 * (third_party/rife/rife-v4.6: flownet.param, flownet.bin; MIT, see third_party/rife/LICENSE). Only the layers
 * that graph uses are here: Input, Split, Concat, Interp (bilinear), Convolution (3 x 3), Deconvolution (4 x 4,
 * stride 2), PixelShuffle, Crop (channels), BinaryOp, Eltwise, ReLU, Sigmoid and rife.Warp.
 *
 * The convolutions are matrix products: the input cut into 16-column tiles (each column one output pixel's
 * 3 x 3 x channels neighbourhood) times the weights in groups of 4 rows, a 4 x 16 block of sums kept in
 * registers. On x86 the same kernel is also built for AVX2 + FMA and picked when the CPU has them.
 * Threads: SDL's (OB_SDL_BASE); without SDL it runs on the caller's thread alone.
 */
#ifdef OB_SDL_BASE                 /* (the SDL client build: SDL's threads; without it, one thread) */
#include <SDL.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/framegen.h"

/* ---- a few threads ---- */
typedef void (*job_fn)(void *arg, int i);

#ifdef OB_SDL_BASE
typedef struct {
    int n;                       /* threads, the caller's included */
    SDL_Thread **th;
    SDL_sem *go, *done;
    SDL_atomic_t next, quit;
    job_fn fn;
    void *arg;
    int count;
} pool;

static void pool_work(pool *p)
{
    for (int i; (i = SDL_AtomicAdd(&p->next, 1)) < p->count;) p->fn(p->arg, i);
}

static int pool_thread(void *ud)
{
    pool *p = ud;
    for (;;) {
        SDL_SemWait(p->go);
        if (SDL_AtomicGet(&p->quit)) return 0;
        pool_work(p);
        SDL_SemPost(p->done);
    }
}

static void pool_run(pool *p, job_fn fn, void *arg, int count)
{
    if (count <= 0) return;
    p->fn = fn; p->arg = arg; p->count = count;
    SDL_AtomicSet(&p->next, 0);
    int helpers = p->n - 1 < count - 1 ? p->n - 1 : count - 1;
    for (int i = 0; i < helpers; i++) SDL_SemPost(p->go);
    pool_work(p);
    for (int i = 0; i < helpers; i++) SDL_SemWait(p->done);
}

static int pool_init(pool *p, int n)
{
    if (n <= 0) n = SDL_GetCPUCount();
    p->n = n < 1 ? 1 : n > 64 ? 64 : n;
    p->go = SDL_CreateSemaphore(0);
    p->done = SDL_CreateSemaphore(0);
    p->th = calloc((size_t)p->n, sizeof *p->th);
    if (!p->go || !p->done || !p->th) return 0;
    for (int i = 1; i < p->n; i++)
        if (!(p->th[i] = SDL_CreateThread(pool_thread, "overbar framegen", p))) { p->n = i; break; }
    return 1;
}

static void pool_end(pool *p)
{
    if (!p->th && !p->go) return;
    SDL_AtomicSet(&p->quit, 1);
    for (int i = 1; i < p->n; i++) SDL_SemPost(p->go);
    for (int i = 1; i < p->n; i++) if (p->th && p->th[i]) SDL_WaitThread(p->th[i], NULL);
    if (p->go) SDL_DestroySemaphore(p->go);
    if (p->done) SDL_DestroySemaphore(p->done);
    free(p->th);
}
#else
typedef struct { int n; } pool;
static void pool_run(pool *p, job_fn fn, void *arg, int count) { (void)p; for (int i = 0; i < count; i++) fn(arg, i); }
static int pool_init(pool *p, int n) { (void)n; p->n = 1; return 1; }
static void pool_end(pool *p) { (void)p; }
#endif

/* ---- memory ---- */
static void *amalloc(size_t n)
{
    uint8_t *p = malloc(n + 64 + sizeof(void *));
    if (!p) return NULL;
    uint8_t *a = (uint8_t *)(((uintptr_t)(p + sizeof(void *)) + 63) & ~(uintptr_t)63);
    ((void **)a)[-1] = p;
    return a;
}
static void afree(void *a) { if (a) free(((void **)a)[-1]); }

/* ---- the graph ---- */
enum { L_INPUT, L_SPLIT, L_CONCAT, L_INTERP, L_CONV, L_DECONV, L_SHUFFLE, L_CROP, L_BINARY, L_ELTWISE, L_RELU, L_SIGMOID, L_WARP };

#define MAXIO 6
typedef struct {
    int type, nin, nout, in[MAXIO], out[MAXIO];
    int cout, cin, k, stride, pad, act, op, scalar, crop0, crop1, input;
    float slope, value, sy, sx, coef[2];
    float *w, *bias;             /* packed: rows in 4s, [rows / 4][K][4] */
    int rows, K;
} layer;

typedef struct {
    int c, h, w, refs;
    float *d;
} tensor;

struct ob_framegen {
    layer *L;
    int nl, nblobs, *uses;
    int mask_layer, flow_layer, out_blob;
    pool p;
    void (*gemm)(const layer *l, const float *bp, int K, float *c, int N, int nb);
    float *scratch;              /* the tiled input of the biggest product */
    size_t scratch_n;
};

static tensor *t_new(int c, int h, int w, int refs)
{
    tensor *t = malloc(sizeof *t);
    if (!t) return NULL;
    t->c = c; t->h = h; t->w = w; t->refs = refs;
    if (!(t->d = amalloc((size_t)c * h * w * sizeof(float)))) { free(t); return NULL; }
    return t;
}
static void t_drop(tensor *t) { if (t && --t->refs <= 0) { afree(t->d); free(t); } }

static float half(uint16_t h)
{
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, f;
    if (!e) { if (!m) f = s; else { float v = ldexpf((float)m, -24); return s ? -v : v; } }
    else if (e == 31) f = s | 0x7F800000u | m << 13;
    else f = s | (e + 112) << 23 | m << 13;
    float v;
    memcpy(&v, &f, 4);
    return v;
}

/* ---- the matrix product: c[r][n] = bias[r] + sum_k w[r][k] * b[k][n] (+ leaky), b in 16-column tiles ---- */
#if defined(__GNUC__) || defined(__clang__)
typedef float v8 __attribute__((vector_size(32), aligned(4)));
#define GEMM_BODY                                                                                  \
    const float *bt = bp;                                                                          \
    int n0 = nb * 16, nn = N - n0 < 16 ? N - n0 : 16;                                              \
    for (int r = 0; r < l->rows; r += 4) {                                                         \
        const float *a = l->w + (size_t)r * K;                                                     \
        v8 c00 = { 0 }, c01 = { 0 }, c10 = { 0 }, c11 = { 0 }, c20 = { 0 }, c21 = { 0 }, c30 = { 0 }, c31 = { 0 }; \
        for (int k = 0; k < K; k++, a += 4) {                                                      \
            v8 b0, b1;                                                                             \
            memcpy(&b0, bt + 16 * k, 32); memcpy(&b1, bt + 16 * k + 8, 32);                        \
            c00 += a[0] * b0; c01 += a[0] * b1;                                                    \
            c10 += a[1] * b0; c11 += a[1] * b1;                                                    \
            c20 += a[2] * b0; c21 += a[2] * b1;                                                    \
            c30 += a[3] * b0; c31 += a[3] * b1;                                                    \
        }                                                                                          \
        float o[4][16];                                                                            \
        memcpy(o[0], &c00, 32); memcpy(o[0] + 8, &c01, 32); memcpy(o[1], &c10, 32); memcpy(o[1] + 8, &c11, 32); \
        memcpy(o[2], &c20, 32); memcpy(o[2] + 8, &c21, 32); memcpy(o[3], &c30, 32); memcpy(o[3] + 8, &c31, 32); \
        for (int i = 0; i < 4; i++) {                                                              \
            float *row = c + (size_t)(r + i) * N + n0, bb = l->bias ? l->bias[r + i] : 0;          \
            for (int j = 0; j < nn; j++) {                                                         \
                float v = o[i][j] + bb;                                                            \
                row[j] = l->act && v < 0 ? v * l->slope : v;                                       \
            }                                                                                      \
        }                                                                                          \
    }
static void gemm_plain(const layer *l, const float *bp, int K, float *c, int N, int nb) { GEMM_BODY }
#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("avx2,fma"))) static void gemm_avx2(const layer *l, const float *bp, int K, float *c, int N, int nb) { GEMM_BODY }
#define HAVE_AVX2_KERNEL 1
#endif
#else
static void gemm_plain(const layer *l, const float *bp, int K, float *c, int N, int nb)
{
    int n0 = nb * 16, nn = N - n0 < 16 ? N - n0 : 16;
    for (int r = 0; r < l->rows; r++) {
        const float *a = l->w + (size_t)(r / 4 * 4) * K + r % 4;
        for (int j = 0; j < nn; j++) {
            float s = l->bias ? l->bias[r] : 0;
            for (int k = 0; k < K; k++) s += a[4 * k] * bp[16 * k + j];
            c[(size_t)r * N + n0 + j] = l->act && s < 0 ? s * l->slope : s;
        }
    }
}
#endif

/* ---- loading ---- */
typedef struct { const uint8_t *b; long n, p; } bin;

static float *take(bin *b, long n, int flagged)
{
    float *out = malloc((size_t)n * sizeof(float));
    if (!out) return NULL;
    if (flagged) {
        if (b->p + 4 > b->n) goto bad;
        uint32_t flag = b->b[b->p] | (uint32_t)b->b[b->p + 1] << 8 | (uint32_t)b->b[b->p + 2] << 16 | (uint32_t)b->b[b->p + 3] << 24;
        b->p += 4;
        if (flag == 0x01306B47u) {
            if (b->p + 2 * n > b->n) goto bad;
            for (long i = 0; i < n; i++) out[i] = half((uint16_t)(b->b[b->p + 2 * i] | b->b[b->p + 2 * i + 1] << 8));
            b->p += (2 * n + 3) / 4 * 4;
            return out;
        }
        if (flag) goto bad;
    }
    if (b->p + 4 * n > b->n) goto bad;
    memcpy(out, b->b + b->p, (size_t)n * 4);      /* (little-endian floats, as ncnn writes them) */
    b->p += 4 * n;
    return out;
bad:
    free(out);
    return NULL;
}

/* raw [rows][K] into [rows / 4][K][4] */
static float *pack(const float *raw, int rows, int K)
{
    float *w = amalloc((size_t)rows * K * sizeof(float));
    if (!w) return NULL;
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < K; k++) w[(size_t)(r / 4) * K * 4 + (size_t)k * 4 + r % 4] = raw[(size_t)r * K + k];
    return w;
}

static char *read_all(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *d = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long len = ftell(f);
        if (len > 0 && !fseek(f, 0, SEEK_SET) && (d = malloc((size_t)len + 1))) {
            if (fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
            else { d[len] = 0; *n = len; }
        }
    }
    fclose(f);
    return d;
}

/* (strtok_r is not everywhere) */
static char *next_line(char **at)
{
    char *s = *at;
    if (!s || !*s) return NULL;
    char *e = strchr(s, '\n');
    if (e) { *e = 0; *at = e + 1; } else *at = s + strlen(s);
    return s;
}

static char *next_tok(char *s, char **at)
{
    if (!s) s = *at;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    if (!*s) { *at = s; return NULL; }
    char *e = s;
    while (*e && *e != ' ' && *e != '\t' && *e != '\r') e++;
    if (*e) *e++ = 0;
    *at = e;
    return s;
}

typedef struct { char **names; int n, cap; } names;

static int blob_id(names *nm, const char *s)
{
    for (int i = 0; i < nm->n; i++) if (!strcmp(nm->names[i], s)) return i;
    if (nm->n == nm->cap) {
        int cap = nm->cap ? nm->cap * 2 : 256;
        char **p = realloc(nm->names, (size_t)cap * sizeof *p);
        if (!p) return -1;
        nm->names = p; nm->cap = cap;
    }
    size_t len = strlen(s) + 1;
    if (!(nm->names[nm->n] = malloc(len))) return -1;
    memcpy(nm->names[nm->n], s, len);
    return nm->n++;
}

static const struct { const char *name; int type; } types[] = {
    { "Input", L_INPUT }, { "Split", L_SPLIT }, { "Concat", L_CONCAT }, { "Interp", L_INTERP }, { "Convolution", L_CONV },
    { "Deconvolution", L_DECONV }, { "PixelShuffle", L_SHUFFLE }, { "Crop", L_CROP }, { "BinaryOp", L_BINARY },
    { "Eltwise", L_ELTWISE }, { "ReLU", L_RELU }, { "Sigmoid", L_SIGMOID }, { "rife.Warp", L_WARP },
};

void ob_framegen_free(ob_framegen *g)
{
    if (!g) return;
    for (int i = 0; i < g->nl; i++) { afree(g->L[i].w); free(g->L[i].bias); }
    free(g->L); free(g->uses);
    afree(g->scratch);
    pool_end(&g->p);
    free(g);
}

ob_framegen *ob_framegen_load(const char *dir, int threads)
{
    char path[1100];
    long pn = 0, bn = 0;
    snprintf(path, sizeof path, "%s/flownet.param", dir);
    char *param = read_all(path, &pn);
    snprintf(path, sizeof path, "%s/flownet.bin", dir);
    char *binf = read_all(path, &bn);
    ob_framegen *g = calloc(1, sizeof *g);
    names nm = { 0 };
    int ok = param && binf && g;
    bin b = { (const uint8_t *)binf, bn, 0 };
    char *save = param, *line = ok ? next_line(&save) : NULL;
    int nlines = 0, nlayers = 0;
    if (ok && (!line || atoi(line) != 7767517)) ok = 0;
    if (ok && (line = next_line(&save)) && sscanf(line, "%d", &nlayers) == 1 && nlayers > 0 && nlayers < 10000)
        g->L = calloc((size_t)nlayers, sizeof *g->L);
    ok = ok && g->L;
    if (g) g->mask_layer = g->flow_layer = -1;
    while (ok && (line = next_line(&save))) {
        char *tok[64], *s2 = NULL;
        int nt = 0;
        for (char *t = next_tok(line, &s2); t && nt < 64; t = next_tok(NULL, &s2)) tok[nt++] = t;
        if (!nt) continue;
        if (nt < 4 || nlines >= nlayers) { ok = 0; break; }
        layer *l = &g->L[nlines];
        l->type = -1;
        for (size_t i = 0; i < sizeof types / sizeof *types; i++) if (!strcmp(tok[0], types[i].name)) l->type = types[i].type;
        l->nin = atoi(tok[2]); l->nout = atoi(tok[3]);
        if (l->type < 0 || l->nin > MAXIO || l->nout > MAXIO || 4 + l->nin + l->nout > nt) { ok = 0; break; }
        for (int i = 0; i < l->nin; i++) l->in[i] = blob_id(&nm, tok[4 + i]);
        for (int i = 0; i < l->nout; i++) l->out[i] = blob_id(&nm, tok[4 + l->nin + i]);
        if (l->type == L_INPUT) l->input = !strcmp(tok[1], "in0") ? 0 : !strcmp(tok[1], "in1") ? 1 : 2;
        /* its parameters */
        int iv[32] = { 0 }, have5 = 0;
        float fv[32] = { 0 }, arr[32][4] = { { 0 } };
        for (int i = 4 + l->nin + l->nout; i < nt; i++) {
            char *eq = strchr(tok[i], '=');
            if (!eq) continue;
            int k = atoi(tok[i]);
            if (k <= -23300) {
                int idx = -k - 23300, cnt = atoi(eq + 1);
                char *v = strchr(eq + 1, ',');
                for (int j = 0; v && j < cnt && j < 4 && idx < 32; j++) { arr[idx][j] = (float)atof(v + 1); v = strchr(v + 1, ','); }
            } else if (k >= 0 && k < 32) {
                iv[k] = atoi(eq + 1); fv[k] = (float)atof(eq + 1);
                if (k == 5) have5 = 1;
            }
        }
        switch (l->type) {
        case L_CONV: case L_DECONV: {
            l->cout = iv[0]; l->k = iv[1]; l->stride = iv[3] ? iv[3] : 1; l->pad = iv[4];
            long nw = iv[6];
            l->cin = l->cout && l->k ? (int)(nw / ((long)l->cout * l->k * l->k)) : 0;
            if (!l->cin || (long)l->cin * l->cout * l->k * l->k != nw || l->cout % 4 || iv[2] > 1
                || (l->type == L_CONV && (l->k != 3 || l->pad != 1)) || (l->type == L_DECONV && (l->k != 4 || l->stride != 2 || l->pad != 1))) { ok = 0; break; }
            float *raw = take(&b, nw, 1);
            if (have5 && iv[5]) l->bias = take(&b, l->cout, 0);
            if (!raw || (have5 && iv[5] && !l->bias)) { free(raw); ok = 0; break; }
            if (iv[9] == 1) { l->act = 1; l->slope = 0; }
            else if (iv[9] == 2) { l->act = 1; l->slope = arr[10][0]; }
            else if (iv[9]) { free(raw); ok = 0; break; }
            if (l->type == L_CONV) { l->rows = l->cout; l->K = l->cin * 9; l->w = pack(raw, l->rows, l->K); }
            else {
                /* [cout][cin][4][4] as a product: row co * 16 + ky * 4 + kx, column ci */
                l->rows = l->cout * 16; l->K = l->cin;
                float *t = malloc((size_t)nw * sizeof(float));
                if (t) for (int co = 0; co < l->cout; co++) for (int ci = 0; ci < l->cin; ci++) for (int kk = 0; kk < 16; kk++)
                    t[((size_t)co * 16 + kk) * l->cin + ci] = raw[((size_t)co * l->cin + ci) * 16 + kk];
                l->w = t ? pack(t, l->rows, l->K) : NULL;
                free(t);
            }
            free(raw);
            if (!l->w) ok = 0;
            break;
        }
        case L_INTERP: l->sy = fv[1]; l->sx = fv[2]; if (iv[0] != 2 || l->sy <= 0 || l->sx <= 0) ok = 0; break;
        case L_SHUFFLE: if (iv[0] != 2) ok = 0; break;
        case L_CROP: l->crop0 = (int)arr[9][0]; l->crop1 = (int)arr[10][0]; if (arr[11][0] != 0 || l->crop1 <= l->crop0) ok = 0; break;
        case L_BINARY: l->op = iv[0]; l->scalar = iv[1]; l->value = fv[2]; if (l->op != 0 && l->op != 1 && l->op != 2 && l->op != 3 && l->op != 7) ok = 0; break;
        case L_ELTWISE: l->coef[0] = arr[1][0]; l->coef[1] = arr[1][1]; if (iv[0] != 1 || l->nin != 2) ok = 0; break;
        case L_RELU: l->slope = fv[0]; break;
        case L_SIGMOID: g->mask_layer = nlines; break;
        default: break;
        }
        nlines++;
    }
    ok = ok && nlines == nlayers && b.p == b.n;
    if (ok) {
        g->nl = nlines;
        g->nblobs = nm.n;
        g->uses = calloc((size_t)nm.n, sizeof *g->uses);
        ok = g->uses != NULL;
        for (int i = 0; ok && i < g->nl; i++) for (int j = 0; j < g->L[i].nin; j++) g->uses[g->L[i].in[j]]++;
        int out = -1;
        for (int i = 0; i < nm.n; i++) if (!strcmp(nm.names[i], "out0")) out = i;
        g->out_blob = out;
        /* the motion: the crop the last warp takes its flow from reads the whole flow */
        int last_warp = -1;
        for (int i = 0; i < g->nl; i++) if (g->L[i].type == L_WARP) last_warp = i;
        for (int i = 0; last_warp >= 0 && i < g->nl; i++)
            if (g->L[i].type == L_CROP && g->L[i].out[0] == g->L[last_warp].in[1]) g->flow_layer = i;
        ok = ok && out >= 0 && g->mask_layer >= 0 && g->flow_layer >= 0;
    }
    if (ok) {
        g->gemm = gemm_plain;
#ifdef HAVE_AVX2_KERNEL
        __builtin_cpu_init();
        if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) g->gemm = gemm_avx2;
#endif
        ok = pool_init(&g->p, threads);
    }
    for (int i = 0; i < nm.n; i++) free(nm.names[i]);
    free(nm.names);
    free(param); free(binf);
    if (!ok) { if (g) g->nl = g->L ? nlayers : 0; ob_framegen_free(g); return NULL; }
    return g;
}

/* ---- the layers ---- */
typedef struct {
    ob_framegen *g;
    const layer *l;
    const tensor *x, *y;
    tensor *o;
    float *bp, *col;
    int N, oh, ow;
} job;

/* a convolution's 16-column tiles: tile nb, K = cin * 9 rows of 16 */
static void conv_tile(void *arg, int nb)
{
    job *j = arg;
    const tensor *x = j->x;
    const layer *l = j->l;
    float *bt = j->bp + (size_t)nb * l->K * 16;
    int iy0[16], ix0[16], ok[16];
    for (int q = 0; q < 16; q++) {
        int n = nb * 16 + q;
        ok[q] = n < j->N;
        iy0[q] = ok[q] ? n / j->ow * l->stride - 1 : 0;
        ix0[q] = ok[q] ? n % j->ow * l->stride - 1 : 0;
    }
    for (int ci = 0; ci < x->c; ci++) {
        const float *src = x->d + (size_t)ci * x->h * x->w;
        for (int ky = 0; ky < 3; ky++) for (int kx = 0; kx < 3; kx++, bt += 16)
            for (int q = 0; q < 16; q++) {
                int y = iy0[q] + ky, xx = ix0[q] + kx;
                bt[q] = ok[q] && y >= 0 && y < x->h && xx >= 0 && xx < x->w ? src[y * x->w + xx] : 0;
            }
    }
    j->g->gemm(l, j->bp + (size_t)nb * l->K * 16, l->K, j->o->d, j->N, nb);
}

/* a deconvolution's product: the input itself in tiles, out to col (rows = cout * 16) */
static void deconv_tile(void *arg, int nb)
{
    job *j = arg;
    const tensor *x = j->x;
    const layer *l = j->l;
    float *bt = j->bp + (size_t)nb * l->K * 16;
    int n0 = nb * 16, nn = j->N - n0 < 16 ? j->N - n0 : 16;
    for (int ci = 0; ci < x->c; ci++, bt += 16) {
        const float *src = x->d + (size_t)ci * j->N + n0;
        for (int q = 0; q < 16; q++) bt[q] = q < nn ? src[q] : 0;
    }
    layer nobias = *l;
    nobias.bias = NULL;
    nobias.act = 0;
    j->g->gemm(&nobias, j->bp + (size_t)nb * l->K * 16, l->K, j->col, j->N, nb);
}

/* its sums put in place: out[co][oy][ox] = bias + col[co][ky][kx][iy][ix] where oy = 2 iy - 1 + ky */
static void deconv_gather(void *arg, int co)
{
    job *j = arg;
    const tensor *x = j->x;
    const layer *l = j->l;
    float *o = j->o->d + (size_t)co * j->oh * j->ow;
    for (int oy = 0; oy < j->oh; oy++) for (int ox = 0; ox < j->ow; ox++) {
        float s = l->bias ? l->bias[co] : 0;
        for (int ky = (oy + 1) & 1; ky < 4; ky += 2) {
            int iy = (oy + 1 - ky) / 2;
            if (iy < 0 || iy >= x->h) continue;
            for (int kx = (ox + 1) & 1; kx < 4; kx += 2) {
                int ix = (ox + 1 - kx) / 2;
                if (ix < 0 || ix >= x->w) continue;
                s += j->col[((size_t)co * 16 + ky * 4 + kx) * j->N + iy * x->w + ix];
            }
        }
        o[oy * j->ow + ox] = s;
    }
}

static void interp_ch(void *arg, int c)
{
    job *j = arg;
    const tensor *x = j->x;
    const float *s = x->d + (size_t)c * x->h * x->w;
    float *o = j->o->d + (size_t)c * j->oh * j->ow;
    float ry = 1.0f / j->l->sy, rx = 1.0f / j->l->sx;
    for (int oy = 0; oy < j->oh; oy++) {
        float fy = (oy + 0.5f) * ry - 0.5f;
        if (fy < 0) fy = 0;
        int y0 = (int)fy, y1 = y0 + 1 < x->h ? y0 + 1 : x->h - 1;
        if (y0 > x->h - 1) y0 = x->h - 1;
        float ly = fy - y0;
        for (int ox = 0; ox < j->ow; ox++) {
            float fx = (ox + 0.5f) * rx - 0.5f;
            if (fx < 0) fx = 0;
            int x0 = (int)fx, x1 = x0 + 1 < x->w ? x0 + 1 : x->w - 1;
            if (x0 > x->w - 1) x0 = x->w - 1;
            float lx = fx - x0;
            float a = s[y0 * x->w + x0] * (1 - lx) + s[y0 * x->w + x1] * lx;
            float b = s[y1 * x->w + x0] * (1 - lx) + s[y1 * x->w + x1] * lx;
            o[oy * j->ow + ox] = a * (1 - ly) + b * ly;
        }
    }
}

/* bilinear, the sample place clamped to the picture (grid_sample, border, corners aligned) */
static inline float sample(const float *s, int w, int h, float fx, float fy)
{
    fx = fx < 0 ? 0 : fx > w - 1 ? (float)(w - 1) : fx;
    fy = fy < 0 ? 0 : fy > h - 1 ? (float)(h - 1) : fy;
    int x0 = (int)fx, y0 = (int)fy, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
    float lx = fx - x0, ly = fy - y0;
    return (s[y0 * w + x0] * (1 - lx) + s[y0 * w + x1] * lx) * (1 - ly) + (s[y1 * w + x0] * (1 - lx) + s[y1 * w + x1] * lx) * ly;
}

static void warp_ch(void *arg, int c)
{
    job *j = arg;
    const tensor *x = j->x, *f = j->y;
    int w = x->w, h = x->h;
    const float *s = x->d + (size_t)c * h * w, *fx = f->d, *fy = f->d + (size_t)h * w;
    float *o = j->o->d + (size_t)c * h * w;
    for (int y = 0; y < h; y++) for (int xx = 0; xx < w; xx++) {
        int i = y * w + xx;
        o[i] = sample(s, w, h, xx + fx[i], y + fy[i]);
    }
}

static void elem_ch(void *arg, int c)
{
    job *j = arg;
    const layer *l = j->l;
    const tensor *x = j->x, *y = j->y;
    size_t hw = (size_t)x->h * x->w;
    float *o = j->o->d + c * hw;
    const float *a = x->d + (x->c == 1 ? 0 : c * hw), *b = y ? y->d + (y->c == 1 ? 0 : c * hw) : NULL;
    switch (l->type) {
    case L_BINARY: {
        if (l->scalar) {
            float v = l->value;
            switch (l->op) {
            case 0: for (size_t i = 0; i < hw; i++) o[i] = a[i] + v; break;
            case 1: for (size_t i = 0; i < hw; i++) o[i] = a[i] - v; break;
            case 2: for (size_t i = 0; i < hw; i++) o[i] = a[i] * v; break;
            case 3: for (size_t i = 0; i < hw; i++) o[i] = a[i] / v; break;
            default: for (size_t i = 0; i < hw; i++) o[i] = v - a[i]; break;
            }
        } else switch (l->op) {
            case 0: for (size_t i = 0; i < hw; i++) o[i] = a[i] + b[i]; break;
            case 1: for (size_t i = 0; i < hw; i++) o[i] = a[i] - b[i]; break;
            case 2: for (size_t i = 0; i < hw; i++) o[i] = a[i] * b[i]; break;
            case 3: for (size_t i = 0; i < hw; i++) o[i] = a[i] / b[i]; break;
            default: for (size_t i = 0; i < hw; i++) o[i] = b[i] - a[i]; break;
        }
        break;
    }
    case L_ELTWISE: for (size_t i = 0; i < hw; i++) o[i] = a[i] * l->coef[0] + b[i] * l->coef[1]; break;
    case L_RELU: for (size_t i = 0; i < hw; i++) o[i] = a[i] < 0 ? a[i] * l->slope : a[i]; break;
    case L_SIGMOID: for (size_t i = 0; i < hw; i++) o[i] = 1.0f / (1.0f + expf(-a[i])); break;
    case L_SHUFFLE: {
        /* out[c][2y + i][2x + j] = in[4c + 2i + j][y][x] */
        int iw = x->w, ih = x->h, ow = iw * 2;
        for (int k = 0; k < 4; k++) {
            const float *s = x->d + (size_t)(4 * c + k) * ih * iw;
            float *d = j->o->d + (size_t)c * 4 * ih * iw;
            for (int yy = 0; yy < ih; yy++) for (int xx = 0; xx < iw; xx++) d[(2 * yy + k / 2) * ow + 2 * xx + k % 2] = s[yy * iw + xx];
        }
        break;
    }
    }
}

static int ensure_scratch(ob_framegen *g, size_t n)
{
    if (g->scratch_n >= n) return 1;
    afree(g->scratch);
    g->scratch = amalloc(n * sizeof(float));
    g->scratch_n = g->scratch ? n : 0;
    return g->scratch != NULL;
}

/* the graph over in0, in1 (3 x ph x pw RGB 0..1) and in2 (the time); keeps the flow and mask when asked */
static tensor *graph(ob_framegen *g, tensor *in[3], tensor **flow, tensor **mask)
{
    tensor **blob = calloc((size_t)g->nblobs, sizeof *blob);
    tensor *result = NULL;
    int ok = blob != NULL;
    for (int i = 0; ok && i < g->nl; i++) {
        const layer *l = &g->L[i];
        tensor *x = l->nin > 0 ? blob[l->in[0]] : NULL, *y = l->nin > 1 ? blob[l->in[1]] : NULL, *o = NULL;
        int uses = g->uses[l->out[0]] + (l->out[0] == g->out_blob);
        job j = { g, l, x, y, NULL, NULL, NULL, 0, 0, 0 };
        for (int k = 0; k < l->nin; k++) if (!blob[l->in[k]]) ok = 0;
        if (!ok) break;
        switch (l->type) {
        case L_INPUT: o = in[l->input]; o->refs = uses + 1; break;   /* (+1: the caller's) */
        case L_SPLIT: {
            int total = 0;
            for (int k = 0; k < l->nout; k++) { blob[l->out[k]] = x; total += g->uses[l->out[k]]; }
            x->refs += total - 1;
            continue;
        }
        case L_CONCAT: {
            int c = 0;
            for (int k = 0; k < l->nin; k++) c += blob[l->in[k]]->c;
            if (!(o = t_new(c, x->h, x->w, uses))) { ok = 0; break; }
            size_t at = 0;
            for (int k = 0; k < l->nin; k++) {
                tensor *t = blob[l->in[k]];
                size_t n = (size_t)t->c * t->h * t->w;
                memcpy(o->d + at, t->d, n * sizeof(float));
                at += n;
            }
            break;
        }
        case L_INTERP:
            j.oh = (int)(x->h * l->sy); j.ow = (int)(x->w * l->sx);
            if (!(o = t_new(x->c, j.oh, j.ow, uses))) { ok = 0; break; }
            j.o = o;
            pool_run(&g->p, interp_ch, &j, x->c);
            break;
        case L_CONV:
            if (x->c != l->cin) { ok = 0; break; }
            j.oh = (x->h + 2 - 3) / l->stride + 1; j.ow = (x->w + 2 - 3) / l->stride + 1; j.N = j.oh * j.ow;
            if (!(o = t_new(l->cout, j.oh, j.ow, uses)) || !ensure_scratch(g, (size_t)(j.N + 15) / 16 * 16 * l->K)) { ok = 0; break; }
            j.o = o; j.bp = g->scratch;
            pool_run(&g->p, conv_tile, &j, (j.N + 15) / 16);
            break;
        case L_DECONV:
            if (x->c != l->cin) { ok = 0; break; }
            j.N = x->h * x->w; j.oh = x->h * 2; j.ow = x->w * 2;
            j.col = amalloc((size_t)l->rows * j.N * sizeof(float));
            if (!j.col || !(o = t_new(l->cout, j.oh, j.ow, uses)) || !ensure_scratch(g, (size_t)(j.N + 15) / 16 * 16 * l->K)) { afree(j.col); ok = 0; break; }
            j.o = o; j.bp = g->scratch;
            pool_run(&g->p, deconv_tile, &j, (j.N + 15) / 16);
            pool_run(&g->p, deconv_gather, &j, l->cout);
            afree(j.col);
            break;
        case L_SHUFFLE:
            if (x->c % 4 || !(o = t_new(x->c / 4, x->h * 2, x->w * 2, uses))) { ok = 0; break; }
            j.o = o;
            pool_run(&g->p, elem_ch, &j, o->c);
            break;
        case L_CROP: {
            int c1 = l->crop1 < x->c ? l->crop1 : x->c;
            if (c1 <= l->crop0 || !(o = t_new(c1 - l->crop0, x->h, x->w, uses))) { ok = 0; break; }
            memcpy(o->d, x->d + (size_t)l->crop0 * x->h * x->w, (size_t)o->c * x->h * x->w * sizeof(float));
            if (i == g->flow_layer && flow) { *flow = x; x->refs++; }
            break;
        }
        case L_BINARY: case L_ELTWISE: case L_RELU: case L_SIGMOID: {
            int c = x->c;
            if (y && y->c > c) c = y->c;
            if ((y && (y->h != x->h || y->w != x->w || (y->c != x->c && y->c != 1 && x->c != 1))) || !(o = t_new(c, x->h, x->w, uses))) { ok = 0; break; }
            j.o = o;
            if (!y && l->type == L_BINARY && !l->scalar) { ok = 0; break; }
            pool_run(&g->p, elem_ch, &j, c);
            if (i == g->mask_layer && mask) { *mask = o; o->refs++; }
            break;
        }
        case L_WARP:
            if (y->c < 2 || y->h != x->h || y->w != x->w || !(o = t_new(x->c, x->h, x->w, uses))) { ok = 0; break; }
            j.o = o;
            pool_run(&g->p, warp_ch, &j, x->c);
            break;
        }
        if (!ok) { if (o && l->type != L_INPUT) { o->refs = 1; t_drop(o); } break; }
        blob[l->out[0]] = o;
        if (l->type != L_INPUT) for (int k = 0; k < l->nin; k++) t_drop(blob[l->in[k]]);
        if (!o->refs) { if (l->type != L_INPUT) { o->refs = 1; t_drop(o); } blob[l->out[0]] = NULL; }
    }
    if (ok) result = blob[g->out_blob];
    else {
        /* (what is still held is dropped once per remaining use) */
        for (int i = 0; i < g->nblobs; i++) if (blob && blob[i] && blob[i] != in[0] && blob[i] != in[1] && blob[i] != in[2]) {
            tensor *t = blob[i];
            for (int k = i; k < g->nblobs; k++) if (blob[k] == t) blob[k] = NULL;
            t->refs = 1; t_drop(t);
        }
        if (flow && *flow) { t_drop(*flow); *flow = NULL; }
        if (mask && *mask) { t_drop(*mask); *mask = NULL; }
    }
    free(blob);
    return result;
}

static int setup(const uint32_t *a, const uint32_t *b, int w, int h, float t, tensor *in[3], int *ph, int *pw)
{
    *ph = (h + 31) / 32 * 32; *pw = (w + 31) / 32 * 32;
    in[0] = t_new(3, *ph, *pw, 1); in[1] = t_new(3, *ph, *pw, 1); in[2] = t_new(1, *ph, *pw, 1);
    if (!in[0] || !in[1] || !in[2]) return 0;
    for (int k = 0; k < 2; k++) {
        const uint32_t *s = k ? b : a;
        float *d = in[k]->d;
        size_t plane = (size_t)*ph * *pw;
        for (int y = 0; y < *ph; y++) for (int x = 0; x < *pw; x++) {
            uint32_t p = s[(y < h ? y : h - 1) * w + (x < w ? x : w - 1)];      /* (padding repeats the edge) */
            size_t i = (size_t)y * *pw + x;
            d[i] = (float)(p >> 16 & 255) / 255.0f;
            d[plane + i] = (float)(p >> 8 & 255) / 255.0f;
            d[2 * plane + i] = (float)(p & 255) / 255.0f;
        }
    }
    for (size_t i = 0; i < (size_t)*ph * *pw; i++) in[2]->d[i] = t;
    return 1;
}

static void unset(tensor *in[3])
{
    for (int k = 0; k < 3; k++) if (in[k]) { afree(in[k]->d); free(in[k]); }
}

static uint32_t argb(float r, float g, float b)
{
    int R = (int)(r * 255 + 0.5f), G = (int)(g * 255 + 0.5f), B = (int)(b * 255 + 0.5f);
    R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
    return 0xFF000000u | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
}

int ob_framegen_run(ob_framegen *g, const uint32_t *a, const uint32_t *b, int w, int h, float t, uint32_t *out)
{
    tensor *in[3] = { 0 };
    int ph, pw, ok = 0;
    if (g && setup(a, b, w, h, t, in, &ph, &pw)) {
        tensor *o = graph(g, in, NULL, NULL);
        if (o && o->c == 3 && o->h == ph && o->w == pw) {
            size_t plane = (size_t)ph * pw;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                size_t i = (size_t)y * pw + x;
                out[y * w + x] = argb(o->d[i], o->d[plane + i], o->d[2 * plane + i]);
            }
            ok = 1;
        }
        if (o) { o->refs = 1; t_drop(o); }
    }
    unset(in);
    return ok;
}

int ob_framegen_motion(ob_framegen *g, const uint32_t *a, const uint32_t *b, int w, int h, float t, float *flow, float *mask)
{
    tensor *in[3] = { 0 }, *f = NULL, *m = NULL;
    int ph, pw, ok = 0;
    if (g && setup(a, b, w, h, t, in, &ph, &pw)) {
        tensor *o = graph(g, in, &f, &m);
        if (o && f && m && f->c >= 4 && f->h == ph && f->w == pw && m->h == ph && m->w == pw) {
            for (int c = 0; c < 4; c++) for (int y = 0; y < h; y++)
                memcpy(flow + ((size_t)c * h + y) * w, f->d + ((size_t)c * ph + y) * pw, (size_t)w * sizeof(float));
            for (int y = 0; y < h; y++) memcpy(mask + (size_t)y * w, m->d + (size_t)y * pw, (size_t)w * sizeof(float));
            ok = 1;
        }
        if (o) { o->refs = 1; t_drop(o); }
        if (f) t_drop(f);
        if (m) t_drop(m);
    }
    unset(in);
    return ok;
}

/* bilinear on a w x h plane at a place in its own pixels (half-pixel centres, edges repeated) */
#define up sample

typedef struct {
    const uint32_t *A, *B;
    int W, H, w, h;
    const float *flow, *mask;
    uint32_t *out;
} draw_job;

static void draw_row(void *arg, int y)
{
    const draw_job *d = arg;
    const uint32_t *A = d->A, *B = d->B;
    int W = d->W, H = d->H, w = d->w, h = d->h;
    const float *flow = d->flow, *mask = d->mask;
    float kx = (float)w / W, ky = (float)h / H, sx = (float)W / w, sy = (float)H / h;
    size_t hw = (size_t)w * h;
    float fy = (y + 0.5f) * ky - 0.5f;
    for (int x = 0; x < W; x++) {
        float fx = (x + 0.5f) * kx - 0.5f;
        float ax = x + up(flow, w, h, fx, fy) * sx, ay = y + up(flow + hw, w, h, fx, fy) * sy;
        float bx = x + up(flow + 2 * hw, w, h, fx, fy) * sx, by = y + up(flow + 3 * hw, w, h, fx, fy) * sy;
        float m = up(mask, w, h, fx, fy);
        ax = ax < 0 ? 0 : ax > W - 1 ? (float)(W - 1) : ax; ay = ay < 0 ? 0 : ay > H - 1 ? (float)(H - 1) : ay;
        bx = bx < 0 ? 0 : bx > W - 1 ? (float)(W - 1) : bx; by = by < 0 ? 0 : by > H - 1 ? (float)(H - 1) : by;
        float ch[3] = { 0, 0, 0 };
        for (int k = 0; k < 2; k++) {
            const uint32_t *s = k ? B : A;
            float px = k ? bx : ax, py = k ? by : ay, wt = k ? 1 - m : m;
            int x0 = (int)px, y0 = (int)py, x1 = x0 + 1 < W ? x0 + 1 : x0, y1 = y0 + 1 < H ? y0 + 1 : y0;
            float lx = px - x0, ly = py - y0;
            uint32_t p00 = s[y0 * W + x0], p01 = s[y0 * W + x1], p10 = s[y1 * W + x0], p11 = s[y1 * W + x1];
            for (int c = 0; c < 3; c++) {
                int sh = 16 - 8 * c;
                float v = ((p00 >> sh & 255) * (1 - lx) + (p01 >> sh & 255) * lx) * (1 - ly) + ((p10 >> sh & 255) * (1 - lx) + (p11 >> sh & 255) * lx) * ly;
                ch[c] += v * wt;
            }
        }
        d->out[y * W + x] = argb(ch[0] / 255, ch[1] / 255, ch[2] / 255);
    }
}

void ob_framegen_draw(ob_framegen *g, const uint32_t *A, const uint32_t *B, int W, int H, const float *flow, const float *mask, int w, int h,
                      uint32_t *out)
{
    draw_job d = { A, B, W, H, w, h, flow, mask, out };
    if (g) pool_run(&g->p, draw_row, &d, H);
    else for (int y = 0; y < H; y++) draw_row(&d, y);
}
