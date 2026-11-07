/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The README's pictures: Overbar's overlay, its pages and its looks over a made-up rink (drawn
 * here, so no game's art is in the repo). `make shots` writes them to docs/images/ as PNG
 * (stored, not compressed: run them through a PNG optimiser before committing).
 */
#include "overbar/overbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 320
#define SH 224

static uint32_t rink[SW * SH];

/* ---- a PNG with stored (uncompressed) deflate blocks: no zlib needed ---- */

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const unsigned char *p, size_t n)
{
    size_t i;
    if (!crc_table[1])
        for (i = 0; i < 256; i++) {
            uint32_t v = (uint32_t)i;
            int k;
            for (k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    for (i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (unsigned char)v; }

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char b[4];
    uint32_t c = crc(0, (const unsigned char *)type, 4);
    be32(b, (uint32_t)n);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    c = crc(c, data, n);
    be32(b, c);
    fwrite(b, 1, 4, f);
}

static int write_png(const char *path, const uint32_t *px, int w, int h)
{
    size_t raw_n = (size_t)h * (w * 3 + 1), blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4, i, o = 0;
    unsigned char *raw = malloc(raw_n), *z = malloc(z_n), hdr[13];
    uint32_t a = 1, b = 0;
    FILE *f;
    int x, y;
    if (!raw || !z) return 0;
    for (y = 0; y < h; y++) {
        unsigned char *r = raw + (size_t)y * (w * 3 + 1);
        r[0] = 0;
        for (x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * w + x];
            r[1 + x * 3] = p >> 16;
            r[2 + x * 3] = p >> 8;
            r[3 + x * 3] = (unsigned char)p;
        }
    }
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (i = 0; i < raw_n; i += 65535) {
        size_t n = raw_n - i < 65535 ? raw_n - i : 65535;
        z[o++] = i + n == raw_n;
        z[o++] = n & 255;
        z[o++] = n >> 8;
        z[o++] = ~n & 255;
        z[o++] = (~n >> 8) & 255;
        memcpy(z + o, raw + i, n);
        o += n;
    }
    for (i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + o, b << 16 | a);
    o += 4;
    f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(hdr, (uint32_t)w);
    be32(hdr + 4, (uint32_t)h);
    hdr[8] = 8; hdr[9] = 2; hdr[10] = hdr[11] = hdr[12] = 0;
    chunk(f, "IHDR", hdr, 13);
    chunk(f, "IDAT", z, o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    printf("%s (%dx%d)\n", path, w, h);
    return 1;
}

/* ---- the rink ---- */

static void dot(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < SW && y < SH) rink[y * SW + x] = 0xFF000000u | c; }
static void box(int x0, int y0, int w, int h, uint32_t c) { int x, y; for (y = y0; y < y0 + h; y++) for (x = x0; x < x0 + w; x++) dot(x, y, c); }

static void ring(int cx, int cy, int r, uint32_t c, int fill)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r * r && (fill || d >= (r - 1) * (r - 1))) dot(cx + x, cy + y, c);
        }
}

/* a skater from above-ish: helmet, shoulders in the team colour, a stick */
static void skater(int x, int y, uint32_t team, uint32_t trim, int stick)
{
    box(x - 4, y - 2, 9, 7, team);
    box(x - 4, y + 2, 9, 1, trim);
    box(x - 3, y + 5, 3, 4, 0x203040);
    box(x + 1, y + 5, 3, 4, 0x203040);
    ring(x, y - 5, 3, 0xF0C8A0, 1);
    box(x - 3, y - 8, 7, 3, team);
    if (stick > 0) { int i; for (i = 0; i < 8; i++) dot(x + 5 + i, y + 2 + i / 2, 0x8B5A2B); box(x + 12, y + 6, 4, 2, 0x202020); }
    else { int i; for (i = 0; i < 8; i++) dot(x - 5 - i, y + 2 + i / 2, 0x8B5A2B); box(x - 15, y + 6, 4, 2, 0x202020); }
}

static void draw_rink(void)
{
    int x, y;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            /* the crowd in the stands: a few colours in a pattern */
            static const uint32_t crowd[6] = { 0x3A3F5C, 0x5C3A3A, 0x2F4A3A, 0x4A4A2F, 0x6B5A8A, 0x8A6B4A };
            uint32_t c = crowd[((x / 3) * 7 + (y / 4) * 5 + (x * y) % 3) % 6];
            rink[y * SW + x] = 0xFF000000u | c;
        }
    box(0, 0, SW, 14, 0x101830);                     /* the score bar */
    box(8, 3, 40, 8, 0xC03030);
    box(272, 3, 40, 8, 0x3050C0);
    box(140, 3, 40, 8, 0xE0E0E0);
    box(10, 26, 300, 190, 0xF0F0F0);                 /* the boards */
    box(14, 30, 292, 182, 0xE6EEF6);                 /* the ice */
    for (y = 30; y < 212; y++)                       /* a little shading on the ice */
        for (x = 14; x < 306; x++)
            if (((x * 13 + y * 7) % 23) == 0) dot(x, y, 0xD6E2EE);
    box(159, 30, 3, 182, 0xD02828);                  /* centre line */
    box(110, 30, 3, 182, 0x2848C8);                  /* blue lines */
    box(208, 30, 3, 182, 0x2848C8);
    box(40, 30, 1, 182, 0xD02828);                   /* goal lines */
    box(279, 30, 1, 182, 0xD02828);
    ring(160, 121, 22, 0x2848C8, 0);
    ring(160, 121, 2, 0x2848C8, 1);
    ring(72, 80, 16, 0xD02828, 0);
    ring(72, 162, 16, 0xD02828, 0);
    ring(248, 80, 16, 0xD02828, 0);
    ring(248, 162, 16, 0xD02828, 0);
    ring(40, 121, 9, 0x88AAEE, 1);                   /* creases */
    ring(279, 121, 9, 0x88AAEE, 1);
    box(14, 30, 26, 182, 0xE6EEF6);
    box(280, 30, 26, 182, 0xE6EEF6);
    box(40, 30, 1, 182, 0xD02828);
    box(279, 30, 1, 182, 0xD02828);
    box(32, 114, 8, 15, 0xC0C0C0);                   /* nets */
    box(33, 115, 6, 13, 0xF8F8F8);
    box(280, 114, 8, 15, 0xC0C0C0);
    box(281, 115, 6, 13, 0xF8F8F8);
    skater(130, 110, 0xC03030, 0xFFFFFF, 1);
    skater(96, 150, 0xC03030, 0xFFFFFF, 1);
    skater(186, 128, 0x3050C0, 0xF0D040, -1);
    skater(226, 92, 0x3050C0, 0xF0D040, -1);
    skater(266, 122, 0x204090, 0xF0D040, -1);        /* the goalie */
    box(150, 122, 3, 2, 0x101010);                   /* the puck */
}

/* ---- shots ---- */

static uint32_t *scale_nearest(const uint32_t *src, int sw, int sh, int k)
{
    uint32_t *out = malloc((size_t)sw * k * sh * k * 4);
    int x, y;
    for (y = 0; y < sh * k; y++)
        for (x = 0; x < sw * k; x++) out[(size_t)y * sw * k + x] = src[(y / k) * sw + x / k];
    return out;
}

/* the overlay as the ports draw it: the picture dimmed, the menu's buffer at a whole fraction */
static void overlay_over(ob_host *h, uint32_t *pic, int w, int hgt, int t)
{
    int mw = w / t, mh = hgt / t, x, y;
    uint32_t *m = malloc((size_t)mw * mh * 4);
    for (y = 0; y < w * hgt; y++) {
        uint32_t p = pic[y];
        int r = (p >> 16 & 255) * 159 / 255, g = (p >> 8 & 255) * 159 / 255, b = (p & 255) * 159 / 255;
        pic[y] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    for (y = 0; y < mw * mh; y++) m[y] = 0x00040404u;
    ob_overlay_draw(h, m, mw, mh, mw);
    for (y = 0; y < mh * t; y++)
        for (x = 0; x < mw * t; x++) {
            uint32_t p = m[(y / t) * mw + x / t];
            if (p >> 24) pic[(y + (hgt - mh * t) / 2) * w + x + (w - mw * t) / 2] = p;
        }
    free(m);
}

static void go(ob_host *h, int page, int sel_id)
{
    int i;
    h->overlay_page = page;
    ob_overlay_build(h);
    for (i = 0; i < h->overlay.count; i++)
        if (h->overlay.items[i].id == sel_id) h->overlay.sel = i;
}

static void menu_shot(ob_host *h, const char *path, int page, int sel_id)
{
    uint32_t *pic = scale_nearest(rink, SW, SH, 3);
    ob_overlay_open(h);
    go(h, page, sel_id);
    overlay_over(h, pic, SW * 3, SH * 3, 2);
    write_png(path, pic, SW * 3, SH * 3);
    free(pic);
}

/* a corner of the rink at 4x the README's size, so the look's detail shows */
static void look_shot(ob_host *h, const char *path, int mode, int crt)
{
    enum { OW = 1280, OH = 896, CX = 300, CY = 200, CW = 480, CH = 336 };
    uint32_t *px = NULL, *crop = malloc((size_t)CW * CH * 4);
    int dw, dh, x, y;
    h->lores.mode = (ob_scale_mode)mode;
    h->lores.crt = (ob_crt_look)crt;
    if (ob_look_render(&h->lores, rink, SW, SH, SW, OW, OH, &px, &dw, &dh)) {
        for (y = 0; y < CH; y++) memcpy(crop + y * CW, px + (size_t)(CY + y) * dw + CX, CW * 4);
    } else {
        uint32_t *big = scale_nearest(rink, SW, SH, 4);
        for (y = 0; y < CH; y++)
            for (x = 0; x < CW; x++) crop[y * CW + x] = big[(size_t)(CY + y) * SW * 4 + CX + x];
        free(big);
    }
    write_png(path, crop, CW, CH);
    free(crop);
}

static void stats_shot(const char *path)
{
    static const char *const lines[3] = { "60.0 fps", "frame 2.1 ms", "Sharp 3x 960x672" };
    uint32_t *pic = scale_nearest(rink, SW, SH, 3), box[200 * 40];
    int bw, bh, x, y, w, hh, px0, py0, k;
    ob_stats_draw(lines, box, 200, 200, 40, &bw, &bh);
    ob_stats_place(SW * 3, SH * 3, bw, bh, &px0, &py0, &w, &hh);
    k = w / bw;
    for (y = 0; y < hh; y++)
        for (x = 0; x < w; x++) {
            uint32_t s = box[(y / k) * 200 + x / k], *d = &pic[(py0 + y) * SW * 3 + px0 + x];
            unsigned a = s >> 24, r, g, b;
            if (!a) continue;
            r = ((s >> 16 & 255) * a + (*d >> 16 & 255) * (255 - a)) / 255;
            g = ((s >> 8 & 255) * a + (*d >> 8 & 255) * (255 - a)) / 255;
            b = ((s & 255) * a + (*d & 255) * (255 - a)) / 255;
            *d = 0xFF000000u | r << 16 | g << 8 | b;
        }
    write_png(path, pic, SW * 3, SH * 3);
    free(pic);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "docs/images";
    char p[512];
    ob_host h;
    draw_rink();
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    snprintf(p, sizeof p, "%s/overlay-main.png", dir);
    menu_shot(&h, p, OB_PAGE_MAIN, OB_ID_VIDEO);
    h.lores.mode = OB_MODE_CRT;
    h.lores.crt = OB_CRT_SOFT_APERTURE;
    snprintf(p, sizeof p, "%s/overlay-crt.png", dir);
    menu_shot(&h, p, OB_PAGE_SCALE_LO, OB_ID_CRT_LOOK);
    h.lores.mode = OB_MODE_MISTER;
    snprintf(p, sizeof p, "%s/overlay-mister.png", dir);
    menu_shot(&h, p, OB_PAGE_SCALE_LO, OB_ID_MISTER_PRESET);
    ob_overlay_close(&h);
    snprintf(p, sizeof p, "%s/look-sharp.png", dir);
    look_shot(&h, p, OB_MODE_SHARP, 0);
    snprintf(p, sizeof p, "%s/look-crt-medium.png", dir);
    look_shot(&h, p, OB_MODE_CRT, OB_CRT_MEDIUM);
    snprintf(p, sizeof p, "%s/look-crt-soft.png", dir);
    look_shot(&h, p, OB_MODE_CRT, OB_CRT_SOFT);
    snprintf(p, sizeof p, "%s/look-crt-grille.png", dir);
    look_shot(&h, p, OB_MODE_CRT, OB_CRT_SOFT_APERTURE);
    snprintf(p, sizeof p, "%s/look-crt-custom.png", dir);
    look_shot(&h, p, OB_MODE_CRT, OB_CRT_CUSTOM);
    /* (with mister/ beside it, the real MiSTer files and a shadow mask; else the built-in stand-ins) */
    snprintf(h.lores.mister_path[3], sizeof h.lores.mister_path[3], "%s", "Shadow_Masks/Sony PVM (Generic) (~1980).txt");
    snprintf(p, sizeof p, "%s/look-mister.png", dir);
    look_shot(&h, p, OB_MODE_MISTER, 0);
    snprintf(p, sizeof p, "%s/stats.png", dir);
    stats_shot(p);
    ob_host_free(&h);
    return 0;
}
