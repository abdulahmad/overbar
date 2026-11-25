/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Frame generation (src/framegen.c) and movie colour (src/grade.c) on pictures this test makes:
 *   - the model in third_party/rife loads, and halfway between a picture and the same picture moved 6 pixels
 *     it draws one moved 3 (much nearer that than a crossfade is); drawn from its motion at 2x it agrees
 *   - two shots of different colour are found as two, their table survives the file, and a muted grey-blue shot
 *     comes out with whiter whites and more colour
 *   - a file of frames (ob_frames_*) reads back near what it was given (2x pictures: it keeps colour at half) */
#include "overbar/framegen.h"
#include "overbar/grade.h"
#include "overbar/postfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define W 96
#define H 64

/* soft blobs and stripes, moved dx pixels */
static uint32_t scene(int x, int y, double dx)
{
    double u = x - dx, v = y;
    double r = 128 + 90 * sin(u * 0.21) * cos(v * 0.17), g = 128 + 80 * cos(u * 0.13 + v * 0.09), b = 128 + 70 * sin((u + v) * 0.11);
    int R = (int)r, G = (int)g, B = (int)b;
    return 0xFF000000u | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
}

static double diff(const uint32_t *a, const uint32_t *b, int w, int h, int margin)
{
    double s = 0;
    long n = 0;
    for (int y = margin; y < h - margin; y++) for (int x = margin; x < w - margin; x++, n++)
        for (int k = 0; k < 24; k += 8) s += abs((int)(a[y * w + x] >> k & 255) - (int)(b[y * w + x] >> k & 255));
    return s / (3.0 * n);
}

static void test_framegen(void)
{
    ob_framegen *g = ob_framegen_load("third_party/rife/rife-v4.6", 0);
    CHECK(g != NULL);
    if (!g) return;
    static uint32_t a[W * H], b[W * H], want[W * H], got[W * H], fade[W * H], drawn[W * H];
    static float flow[4 * W * H], mask[W * H];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        a[y * W + x] = scene(x, y, 0); b[y * W + x] = scene(x, y, 6); want[y * W + x] = scene(x, y, 3);
        uint32_t p = a[y * W + x], q = b[y * W + x], f = 0xFF000000u;
        for (int k = 0; k < 24; k += 8) f |= (uint32_t)(((p >> k & 255) + (q >> k & 255) + 1) / 2) << k;
        fade[y * W + x] = f;
    }
    CHECK(ob_framegen_run(g, a, b, W, H, 0.5f, got));
    double dg = diff(got, want, W, H, 8), df = diff(fade, want, W, H, 8);
    printf("  halfway: %.2f from the moved picture (a crossfade: %.2f)\n", dg, df);
    CHECK(dg < 3.0 && dg * 3 < df);
    CHECK(ob_framegen_motion(g, a, b, W, H, 0.5f, flow, mask));
    double mx = 0;
    for (int y = 8; y < H - 8; y++) for (int x = 8; x < W - 8; x++) mx += flow[y * W + x];
    mx /= (double)(W - 16) * (H - 16);
    printf("  motion: a's dx %.2f (want -3)\n", mx);
    CHECK(fabs(mx + 3) < 1.0);
    ob_framegen_draw(g, a, b, W, H, flow, mask, W, H, drawn);
    CHECK(diff(drawn, got, W, H, 0) < 0.05);
    /* at 2x from the same motion */
    static uint32_t A2[4 * W * H], B2[4 * W * H], out2[4 * W * H], want2[4 * W * H];
    for (int y = 0; y < 2 * H; y++) for (int x = 0; x < 2 * W; x++) {
        A2[y * 2 * W + x] = scene(x / 2, y / 2, 0);
        B2[y * 2 * W + x] = scene(x / 2, y / 2, 6);
        want2[y * 2 * W + x] = want[y / 2 * W + x / 2];
    }
    ob_framegen_draw(NULL, A2, B2, 2 * W, 2 * H, flow, mask, W, H, out2);
    CHECK(diff(out2, want2, 2 * W, 2 * H, 16) < 6.0);
    ob_framegen_free(g);
    CHECK(ob_framegen_load("no/such/folder", 1) == NULL);
}

static void test_grade(void)
{
    static uint32_t f[W * H], o[W * H];
    ob_grade_maker *m = ob_grade_make(W, H);
    CHECK(m != NULL);
    if (!m) return;
    int cuts = 0;
    for (int k = 0; k < 20; k++) {
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int v = 40 + (x + y) * 120 / (W + H);              /* a muted grey-blue ramp, then a reddish one */
            f[y * W + x] = k < 10 ? 0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)(v + 12)
                                  : 0xFF000000u | (uint32_t)(v + 60) << 16 | (uint32_t)(v / 2) << 8 | (uint32_t)(v / 2);
        }
        cuts += ob_grade_add(m, f);
    }
    ob_grade_scene *s = NULL, *t = NULL;
    int n = ob_grade_end(m, &s);
    CHECK(cuts == 1 && n == 2);
    if (n != 2) { free(s); return; }
    CHECK(s[0].start == 0 && s[0].end == 10 && s[1].start == 10 && s[1].end == 20);
    CHECK(s[0].p.gain[2] < 1 && s[0].p.sat > 1.2f);
    CHECK(ob_grade_save("test_grade.tmp", 1234, s, n));
    CHECK(ob_grade_load("test_grade.tmp", 1234, &t) == 2 && t && !memcmp(s, t, 2 * sizeof *s));
    CHECK(ob_grade_load("test_grade.tmp", 99, &t) == -1);
    remove("test_grade.tmp");
    CHECK(ob_grade_find(s, n, 15) == &s[1] && ob_grade_find(s, n, 20) == NULL);
    ob_grade_lut l;
    ob_grade_prepare(&s[0].p, &l);
    uint32_t bright = 0xFFA0A0ACu, out;
    ob_grade_apply(&l, &bright, &out, 1);
    CHECK((out >> 16 & 255) > 0xA0 + 20 && (out & 255) - (out >> 16 & 255) < 12);   /* whiter, and the blue cast gone */
    ob_grade_params none = { 0, 1, { 1, 1, 1 }, 1 };
    ob_grade_prepare(&none, &l);
    for (int i = 0; i < W * H; i++) f[i] = 0xFF000000u | (uint32_t)(i * 7 & 255) << 16 | (uint32_t)(i * 3 & 255) << 8 | (uint32_t)(i & 127);
    ob_grade_apply(&l, f, o, W * H);
    int worst = 0;
    for (int i = 0; i < W * H; i++) for (int k = 0; k < 24; k += 8) {
        int d = abs((int)(o[i] >> k & 255) - (int)(f[i] >> k & 255));
        if ((f[i] >> k & 255) < 200 && d > worst) worst = d;    /* (below the shoulder: unchanged) */
    }
    CHECK(worst <= 1);
    free(s); free(t);
}

static void test_frames_file(void)
{
    static uint32_t f[W * H];
    ob_ddcache_maker *m = ob_frames_make("test_frames.tmp", W, H, 77);
    CHECK(m != NULL);
    for (int k = 0; m && k < 3; k++) {
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) f[y * W + x] = scene(x / 2, y / 2, 2 * k);   /* (2x pictures: the colour is kept at half) */
        CHECK(ob_ddcache_add(m, f));
    }
    CHECK(ob_ddcache_end(m, 1));
    ob_ddcache *c = ob_frames_open("test_frames.tmp", W, H, 77);
    CHECK(c && ob_ddcache_frames(c) == 3);
    const uint32_t *r = c ? ob_ddcache_read(c, 1) : NULL;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) f[y * W + x] = scene(x / 2, y / 2, 2);
    if (r) printf("  frames file: %.2f off\n", diff(r, f, W, H, 0));
    CHECK(r && diff(r, f, W, H, 0) < 2.0);
    ob_ddcache_close(c);
    CHECK(ob_frames_open("test_frames.tmp", W, H, 78) == NULL && ob_ddcache_open("test_frames.tmp", W / 2, H / 2, 77) == NULL);
    remove("test_frames.tmp");
}

int main(void)
{
    test_framegen();
    test_grade();
    test_frames_file();
    printf("framegen: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
