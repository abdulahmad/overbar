/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The README's pictures: Overbar's overlay, picture shapes and looks over three sample
 * screens (docs/samples: a Genesis 320x224, a SNES 256x224 and a DOS 320x200 one). `make shots`
 * turns the samples into PPM (this tool reads P6 PPM) and writes docs/images/ as PNG, stored,
 * not compressed: run them through a PNG optimiser before committing.
 *
 *   shots <out dir> <genesis.ppm> <snes.ppm> <dos.ppm>
 */
#include "overbar/overbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t *px; int w, h; } image;

static image gen, snes, dos;
static const char *out_dir;

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

static image load_ppm(const char *path)
{
    image im = { NULL, 0, 0 };
    FILE *f = fopen(path, "rb");
    int max, i, c;
    if (!f) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    if (fscanf(f, "P6 %d %d %d", &im.w, &im.h, &max) != 3 || max != 255) { fprintf(stderr, "%s: not a P6 PPM\n", path); exit(1); }
    fgetc(f);
    im.px = malloc((size_t)im.w * im.h * 4);
    for (i = 0; i < im.w * im.h; i++) {
        int r = fgetc(f), g = fgetc(f), b = fgetc(f);
        if ((c = b) == EOF) break;
        im.px[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    fclose(f);
    return im;
}

static void save(const char *name, const uint32_t *px, int w, int h)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", out_dir, name);
    write_png(p, px, w, h);
}

/* Sharp as the ports draw it: nearest to a whole multiple (3x), then bilinear to ow x oh */
static uint32_t *sharp(const image *im, int ow, int oh)
{
    const int k = 3;
    int bw = im->w * k, bh = im->h * k, x, y;
    uint32_t *out = malloc((size_t)ow * oh * 4);
    for (y = 0; y < oh; y++)
        for (x = 0; x < ow; x++) {
            float fx = (x + 0.5f) * bw / ow - 0.5f, fy = (y + 0.5f) * bh / oh - 0.5f;
            int x0 = (int)fx, y0 = (int)fy, t;
            float ax = fx - x0, ay = fy - y0;
            uint32_t q[4], o = 0xFF000000u;
            if (fx < 0) { x0 = 0; ax = 0; }
            if (fy < 0) { y0 = 0; ay = 0; }
            q[0] = im->px[(y0 / k) * im->w + x0 / k];
            q[1] = im->px[(y0 / k) * im->w + (x0 + 1 < bw ? x0 + 1 : x0) / k];
            q[2] = im->px[((y0 + 1 < bh ? y0 + 1 : y0) / k) * im->w + x0 / k];
            q[3] = im->px[((y0 + 1 < bh ? y0 + 1 : y0) / k) * im->w + (x0 + 1 < bw ? x0 + 1 : x0) / k];
            for (t = 0; t < 24; t += 8) {
                float v = ((q[0] >> t & 255) * (1 - ax) + (q[1] >> t & 255) * ax) * (1 - ay)
                        + ((q[2] >> t & 255) * (1 - ax) + (q[3] >> t & 255) * ax) * ay;
                o |= (uint32_t)(v + 0.5f) << t;
            }
            out[(size_t)y * ow + x] = o;
        }
    return out;
}

/* the picture as the look draws it, at ow x oh */
static uint32_t *look_pic(ob_look *look, const image *im, int ow, int oh)
{
    uint32_t *px = NULL, *out;
    int dw, dh;
    if (!ob_look_render(look, im->px, im->w, im->h, im->w, ow, oh, &px, &dw, &dh)) return sharp(im, ow, oh);
    out = malloc((size_t)ow * oh * 4);
    memcpy(out, px, (size_t)ow * oh * 4);
    return out;
}

/* the picture's size in a window of the given height, as ob_view places it */
static void shape(const image *im, int console, int win_h, int *w, int *h)
{
    ob_view_in in;
    ob_view_out v;
    memset(&in, 0, sizeof in);
    in.src_w = im->w;
    in.src_h = im->h;
    in.dar_h = console ? 240 : im->h;
    in.window_w = 4 * win_h;
    in.window_h = win_h;
    in.height = OB_HEIGHT_FIT;
    ob_view(&in, &v);
    *w = v.blit_w;
    *h = v.blit_h;
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

static void menu_shot(ob_host *h, const char *name, const image *im, int page, int sel_id)
{
    int w, hh, i;
    uint32_t *pic;
    shape(im, 1, 672, &w, &hh);
    pic = look_pic(&h->lores, im, w, hh);
    ob_overlay_open(h);
    h->overlay_page = page;
    ob_overlay_build(h);
    for (i = 0; i < h->overlay.count; i++)
        if (h->overlay.items[i].id == sel_id) h->overlay.sel = i;
    overlay_over(h, pic, w, hh, 2);
    ob_overlay_close(h);
    save(name, pic, w, hh);
    free(pic);
}

/* a part of the picture drawn 4 rows a line, at its own size, so the look's detail shows */
static void crop_shot(ob_look *look, const char *name, const image *im, int console, int cx, int cy, int cw, int ch)
{
    int w, hh, y;
    uint32_t *pic, *crop = malloc((size_t)cw * ch * 4);
    shape(im, console, im->h * 4 * (look->khz31 ? 1 : 1), &w, &hh);
    pic = look_pic(look, im, w, hh);
    for (y = 0; y < ch; y++) memcpy(crop + (size_t)y * cw, pic + (size_t)(cy + y) * w + cx, (size_t)cw * 4);
    save(name, crop, cw, ch);
    free(pic);
    free(crop);
}

/* the three samples side by side, each the shape a 4:3 screen gives it, 360 lines tall */
static void shapes_shot(void)
{
    const image *ims[3] = { &gen, &snes, &dos };
    const int console[3] = { 1, 1, 0 };
    int w[3], h[3], total = 0, gap = 16, x0, i, y, H = 360;
    uint32_t *out;
    for (i = 0; i < 3; i++) { shape(ims[i], console[i], H, &w[i], &h[i]); total += w[i]; }
    total += gap * 4;
    out = malloc((size_t)total * (H + 2 * gap) * 4);
    for (i = 0; i < total * (H + 2 * gap); i++) out[i] = 0xFF181818u;
    x0 = gap;
    for (i = 0; i < 3; i++) {
        uint32_t *p = sharp(ims[i], w[i], h[i]);
        for (y = 0; y < h[i]; y++) memcpy(out + (size_t)(gap + (H - h[i]) / 2 + y) * total + x0, p + (size_t)y * w[i], (size_t)w[i] * 4);
        x0 += w[i] + gap;
        free(p);
    }
    save("shapes.png", out, total, H + 2 * gap);
    free(out);
}

static void stats_shot(ob_host *h)
{
    static const char *const lines[3] = { "60.0 fps", "frame 2.1 ms", "Sharp 3x 960x720" };
    uint32_t *pic, box[200 * 40];
    int W, H, bw, bh, x, y, w, hh, px0, py0, k;
    (void)h;
    shape(&dos, 0, 720, &W, &H);
    pic = sharp(&dos, W, H);
    ob_stats_draw(lines, box, 200, 200, 40, &bw, &bh);
    ob_stats_place(W, H, bw, bh, &px0, &py0, &w, &hh);
    k = w / bw;
    for (y = 0; y < hh; y++)
        for (x = 0; x < w; x++) {
            uint32_t s = box[(y / k) * 200 + x / k], *d = &pic[(py0 + y) * W + px0 + x];
            unsigned a = s >> 24, r, g, b;
            if (!a) continue;
            r = ((s >> 16 & 255) * a + (*d >> 16 & 255) * (255 - a)) / 255;
            g = ((s >> 8 & 255) * a + (*d >> 8 & 255) * (255 - a)) / 255;
            b = ((s & 255) * a + (*d & 255) * (255 - a)) / 255;
            *d = 0xFF000000u | r << 16 | g << 8 | b;
        }
    save("stats.png", pic, W, H);
    free(pic);
}

int main(int argc, char **argv)
{
    ob_host h;
    ob_look l;
    if (argc < 5) { fprintf(stderr, "shots <out dir> <genesis.ppm> <snes.ppm> <dos.ppm>\n"); return 1; }
    out_dir = argv[1];
    gen = load_ppm(argv[2]);
    snes = load_ppm(argv[3]);
    dos = load_ppm(argv[4]);
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);

    menu_shot(&h, "overlay-main.png", &gen, OB_PAGE_MAIN, OB_ID_VIDEO);
    h.lores.mode = OB_MODE_CRT;
    h.lores.crt = OB_CRT_SOFT_APERTURE;
    menu_shot(&h, "overlay-crt.png", &snes, OB_PAGE_SCALE_LO, OB_ID_CRT_LOOK);
    h.lores.mode = OB_MODE_MISTER;
    menu_shot(&h, "overlay-mister.png", &gen, OB_PAGE_SCALE_LO, OB_ID_MISTER_PRESET);
    shapes_shot();
    stats_shot(&h);

    /* the looks, on the SNES goal: the GOAL! box and the crease */
    l = h.lores;
    l.mode = OB_MODE_SHARP;
    crop_shot(&l, "look-sharp.png", &snes, 1, 560, 60, 480, 336);
    l.mode = OB_MODE_CRT;
    l.crt = OB_CRT_MEDIUM;
    crop_shot(&l, "look-crt-medium.png", &snes, 1, 560, 60, 480, 336);
    l.crt = OB_CRT_SOFT;
    crop_shot(&l, "look-crt-soft.png", &snes, 1, 560, 60, 480, 336);
    l.crt = OB_CRT_SOFT_APERTURE;
    crop_shot(&l, "look-crt-grille.png", &snes, 1, 560, 60, 480, 336);
    l.crt = OB_CRT_CUSTOM;
    crop_shot(&l, "look-crt-custom.png", &snes, 1, 560, 60, 480, 336);
    /* (with mister/ beside it, the real MiSTer files and a shadow mask; else the built-in stand-ins) */
    l.mode = OB_MODE_MISTER;
    snprintf(l.mister_path[3], sizeof l.mister_path[3], "%s", "Shadow_Masks/Sony PVM (Generic) (~1980).txt");
    crop_shot(&l, "look-mister.png", &snes, 1, 560, 60, 480, 336);

    /* DOS 320x200 on a 15 kHz TV (200 scanlines) and a 31 kHz VGA monitor (each line twice) */
    l.mode = OB_MODE_CRT;
    l.crt = OB_CRT_MEDIUM;
    l.khz31 = 0;
    crop_shot(&l, "dos-15khz.png", &dos, 0, 300, 220, 480, 336);
    l.khz31 = 1;
    crop_shot(&l, "dos-31khz.png", &dos, 0, 300, 220, 480, 336);
    ob_host_free(&h);
    return 0;
}
