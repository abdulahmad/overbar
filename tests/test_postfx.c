/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The frame post-process (src/postfx.c) on pictures this test makes: off is the frame itself; live evens out a
 * checkerboard, one-in-four dots and stripes and leaves hard edges and flat colour alone; the cache reads back
 * near what it was given (it is a little lossy), the same in order and out of it; and the pixels are the same on every machine (their hashes). */
#include "overbar/postfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define W 64
#define H 48
#define A 0xFF404040u
#define Bc 0xFF606060u

static uint32_t in[W * H], out[W * H];
static uint8_t scratch[W * H];

static uint32_t hash32(const uint32_t *p, long n) { return ob_postfx_hash((const uint8_t *)p, n * 4); }

/* the cache keeps a frame near enough: no channel off by much, and on average under 2 */
static int near_frame(const uint32_t *a, const uint32_t *b, long n)
{
    long sum = 0;
    int worst = 0;
    for (long i = 0; i < n; i++)
        for (int s = 0; s < 24; s += 8) {
            int d = abs((int)(a[i] >> s & 255) - (int)(b[i] >> s & 255));
            sum += d;
            if (d > worst) worst = d;
        }
    if (worst > 24 || sum > 2 * 3 * n) printf("  (worst %d, mean %.2f)\n", worst, (double)sum / (3 * n));
    return worst <= 24 && sum <= 2 * 3 * n;
}

/* a frame: left third a checkerboard of A and B, middle third B dots one in four over A, right third black
 * and white halves (a hard edge), bottom rows flat; f moves the checkerboard */
static void frame(uint32_t *p, int f)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c;
            if (y >= H - 8) c = 0xFF2080C0u;
            else if (x < W / 3) c = ((x + y + f) & 1) ? Bc : A;
            else if (x < 2 * W / 3) c = (!(x & 1) && !(y & 1)) ? Bc : A;
            else c = y < H / 2 ? 0xFF000000u : 0xFFFFFFFFu;
            p[y * W + x] = c;
        }
}

int main(void)
{
    printf("postfx\n");
    frame(in, 0);
    int ow, oh;
    ob_postfx *p = ob_postfx_new(W, H, NULL, 1);
    CHECK(p != NULL);
    CHECK(ob_postfx_run(p, OB_POSTFX_OFF, 0, in, &ow, &oh) == in && ow == W && oh == H);   /* off: the frame itself */

    ob_dedither_live(in, out, scratch, W, H);
    CHECK(out[10 * W + 5] == 0xFF505050u && out[11 * W + 6] == 0xFF505050u);        /* the checkerboard: halfway */
    CHECK(out[10 * W + 30] == 0xFF484848u && out[11 * W + 31] == 0xFF484848u);       /* one in four: a quarter */
    CHECK(out[10 * W + 50] == 0xFF000000u && out[30 * W + 50] == 0xFFFFFFFFu);       /* the edge stays */
    CHECK(out[H / 2 * W + 50] == 0xFFFFFFFFu && out[(H / 2 - 1) * W + 50] == 0xFF000000u);
    CHECK(!memcmp(out + (H - 6) * W, in + (H - 6) * W, 6 * W * 4));                    /* flat colour: the same bits */
    for (int y = 0; y < H; y++)                                                        /* stripes */
        for (int x = 0; x < W; x++) in[y * W + x] = (x & 1) ? Bc : A;
    ob_dedither_live(in, out, scratch, W, H);
    CHECK(out[20 * W + 20] == 0xFF505050u && out[20 * W + 21] == 0xFF505050u);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) in[y * W + x] = (y & 1) ? Bc : A;
    ob_dedither_live(in, out, scratch, W, H);
    CHECK(out[20 * W + 20] == 0xFF505050u && out[21 * W + 20] == 0xFF505050u);
    frame(in, 0);
    ob_dedither_live(in, out, scratch, W, H);
    uint32_t live_hash = hash32(out, W * H);

    /* the cached quality and its file */
    ob_dedither_hq *q = ob_dedither_hq_new(W, H);
    static uint32_t hq[6][4 * W * H];
    for (int f = 0; f < 6; f++) { frame(in, f); memcpy(hq[f], ob_dedither_hq_frame(q, in), sizeof hq[f]); }
    ob_dedither_hq_free(q);
    const uint32_t *c = hq[3];
    CHECK(abs((int)(c[20 * 2 * W + 10] & 255) - 0x50) <= 2);                         /* the checkerboard's middle grey, at 2x */
    CHECK((c[10 * 2 * W + 110] & 255) <= 8 && (c[60 * 2 * W + 110] & 255) >= 247);   /* the edge still black and white */
    uint32_t hq_hash = hash32(hq[5], 4 * W * H);

    const char *path = "test_postfx.ddc";
    remove(path);
    CHECK(ob_ddcache_open(path, W, H, 7) == NULL);
    ob_ddcache_maker *m = ob_ddcache_make(path, W, H, 7);
    CHECK(m != NULL);
    for (int f = 0; f < 40; f++) { frame(in, f % 6); CHECK(ob_ddcache_add(m, in)); }
    CHECK(ob_ddcache_end(m, 1));
    CHECK(ob_ddcache_open(path, W, H, 8) == NULL);                                    /* another movie's */
    CHECK(ob_ddcache_open(path, W + 8, H, 7) == NULL);
    ob_ddcache *dc = ob_ddcache_open(path, W, H, 7);
    CHECK(dc && ob_ddcache_frames(dc) == 40);
    for (int f = 0; f < 6; f++) { const uint32_t *r = ob_ddcache_read(dc, f); CHECK(r && near_frame(r, hq[f], 4 * W * H)); }
    static uint32_t seq[40][4 * W * H];
    for (int f = 0; f < 40; f++) { const uint32_t *r = ob_ddcache_read(dc, f); CHECK(r != NULL); if (r) memcpy(seq[f], r, sizeof seq[f]); }
    static const int jumps[] = { 37, 3, 33, 32, 31, 0, 39, 39, 12 };
    for (unsigned k = 0; k < sizeof jumps / sizeof jumps[0]; k++) {
        const uint32_t *r = ob_ddcache_read(dc, jumps[k]);
        CHECK(r && !memcmp(r, seq[jumps[k]], sizeof seq[0]));
    }
    CHECK(ob_ddcache_read(dc, 40) == NULL);
    ob_ddcache_close(dc);

    ob_postfx *pc = ob_postfx_new(W, H, path, 7);                                      /* the stage: cached from the file */
    CHECK(ob_postfx_has_cache(pc));
    const uint32_t *r = ob_postfx_run(pc, OB_POSTFX_CACHED, 4, in, &ow, &oh);
    CHECK(r && ow == 2 * W && oh == 2 * H && near_frame(r, hq[4], 4 * W * H));
    ob_postfx_free(pc);
    pc = ob_postfx_new(W, H, "no-such.ddc", 7);                                        /* no cache yet: live */
    frame(in, 0);
    r = ob_postfx_run(pc, OB_POSTFX_CACHED, 0, in, &ow, &oh);
    CHECK(r && ow == W && oh == H && hash32(r, W * H) == live_hash);
    ob_postfx_free(pc);
    ob_postfx_free(p);
    FILE *f = fopen(path, "rb");                                                       /* the file: the same bytes everywhere */
    static uint8_t file[1 << 20];
    long size = f ? (long)fread(file, 1, sizeof file, f) : 0;
    if (f) fclose(f);
    uint32_t file_hash = ob_postfx_hash(file, size);
    if (size > 8) {                                                                    /* an older version's file (another pass): not used, so made again */
        file[4] ^= 1;
        f = fopen(path, "wb");
        if (f) { fwrite(file, 1, (size_t)size, f); fclose(f); }
        pc = ob_postfx_new(W, H, path, 7);
        CHECK(!ob_postfx_has_cache(pc));
        ob_postfx_free(pc);
        file[4] ^= 1;
    }
    remove(path);

    printf("  live %08X, cached %08X, file %08X (%ld bytes for 40 frames of %d x %d)\n", (unsigned)live_hash, (unsigned)hq_hash, (unsigned)file_hash, size, 2 * W, 2 * H);
    CHECK(file_hash == 0x4DEDEB3Fu);   /* (DD_VERSION 4) */
    CHECK(live_hash == 0xD8F6FF9Fu);   /* (the same on every machine: integers only) */
    CHECK(hq_hash == 0x493F49ABu);
    printf("postfx: %d of %d checks failed\n", fails, checks);
    return fails != 0;
}
