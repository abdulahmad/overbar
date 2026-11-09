/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad */
#include "overbar/osk.h"
#include <string.h>
#include "mdrt/menu.h"

#define COLS 10
#define ROWS 5
#define KW 20                              /* a key's width in pixels (the text cell is 6x10) */
#define KH 14

enum { K_CHAR, K_SPACE, K_BACK, K_SHIFT, K_ENTER, K_DONE };

static const char *rows[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.-" };

static int hid(char c)
{
    if (c >= 'A' && c <= 'Z') return 4 + (c - 'A');
    if (c >= '1' && c <= '9') return 30 + (c - '1');
    switch (c) { case '0': return 39; case '\'': return 52; case ',': return 54; case '.': return 55; case '-': return 45; }
    return 0;
}

/* the bottom row: five keys, two columns each */
static int bottom(int col) { return col / 2; }
static const int bottom_kind[5] = { K_SPACE, K_BACK, K_SHIFT, K_ENTER, K_DONE };
static const char *bottom_label[5] = { "Space", "Del", "Shift", "Enter", "Done" };

const char *ob_osk_label(int col, int row)
{
    static char one[2];
    if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return "";
    if (row == 4) return bottom_label[bottom(col)];
    one[0] = rows[row][col];
    one[1] = 0;
    return one;
}

void ob_osk_init(ob_osk *k, void (*key)(int, int, void *), void *ud)
{
    memset(k, 0, sizeof *k);
    k->key = key;
    k->ud = ud;
    k->col = 0;
    k->row = 1;
}

void ob_osk_open(ob_osk *k) { k->open = 1; k->shift = 0; }
void ob_osk_close(ob_osk *k) { k->open = 0; k->shift = 0; }

static void tap(ob_osk *k, int sc, int shifted)
{
    if (!k->key || !sc) return;
    if (shifted) k->key(OB_OSK_LSHIFT, 1, k->ud);
    k->key(sc, 1, k->ud);
    k->key(sc, 0, k->ud);
    if (shifted) k->key(OB_OSK_LSHIFT, 0, k->ud);
}

static void press(ob_osk *k)
{
    if (k->row < 4) {
        char c = rows[k->row][k->col];
        tap(k, hid(c), k->shift && c >= 'A' && c <= 'Z' ? 1 : 0);
        k->shift = 0;
        return;
    }
    switch (bottom_kind[bottom(k->col)]) {
    case K_SPACE: tap(k, 44, 0); break;
    case K_BACK: tap(k, 42, 0); break;
    case K_SHIFT: k->shift = !k->shift; break;
    case K_ENTER: tap(k, 40, 0); break;
    case K_DONE: ob_osk_close(k); break;
    }
}

int ob_osk_input(ob_osk *k, int in)
{
    if (!k->open) return 0;
    switch (in) {
    case MDRT_MENU_UP: k->row = (k->row + ROWS - 1) % ROWS; break;
    case MDRT_MENU_DOWN: k->row = (k->row + 1) % ROWS; break;
    case MDRT_MENU_LEFT: k->col = (k->col + COLS - (k->row == 4 ? 2 : 1)) % COLS; break;
    case MDRT_MENU_RIGHT: k->col = (k->col + (k->row == 4 ? 2 : 1)) % COLS; break;
    case MDRT_MENU_OK: press(k); break;
    case MDRT_MENU_BACK: tap(k, 42, 0); break;     /* B deletes */
    }
    if (k->row == 4) k->col &= ~1;                  /* the bottom keys are two columns wide: the cursor sits on the first */
    return 1;
}

static void fill(uint32_t *px, int w, int h, int pitch, int x, int y, int rw, int rh, uint32_t rgb)
{
    int i, j;
    for (j = 0; j < rh; j++) {
        if (y + j < 0 || y + j >= h) continue;
        for (i = 0; i < rw; i++)
            if (x + i >= 0 && x + i < w) px[(size_t)(y + j) * pitch + x + i] = rgb;
    }
}

#define TW (COLS * KW + 8)
#define TH (ROWS * KH + 8 + 12)                 /* 12 rows of pixels above the keys: the caps indicator */

/* the keyboard is drawn at twice its basic size when the picture is big enough for that (the basic size is 208 x 90 pixels) */
static int osk_scale(int w, int h) { return w >= 2 * TW + 8 && h >= 2 * TH + 8 ? 2 : 1; }

int ob_osk_height(int w, int h) { return TH * osk_scale(w, h) + 4; }

static void osk_draw1(const ob_osk *k, uint32_t *px, int w, int h, int pitch)
{
    int r, c, x0, y0, tw = TW, th = TH;
    if (!k->open || w < tw || h < th) return;
    x0 = (w - tw) / 2;
    y0 = h - th - 4;
    fill(px, w, h, pitch, x0, y0, tw, th, 0xF0101830u);
    {   /* what the next letter will be: CAPS lit while shift is on, lower case letters otherwise (shift is for one letter) */
        const char *m = k->shift ? "CAPS" : "abc";
        fill(px, w, h, pitch, x0 + 4, y0 + 3, 36, 10, k->shift ? 0xFF50A050u : 0xFF303860u);
        mdrt_menu_text(px, w, h, pitch, x0 + 4 + (36 - (int)strlen(m) * 6) / 2, y0 + 4, m, 0xFFFFFFFFu);
    }
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c += (r == 4 ? 2 : 1)) {
            int kw = (r == 4 ? 2 : 1) * KW - 2;
            int on = r == k->row && c == k->col;
            const char *s0 = ob_osk_label(c, r);
            char lab[16];
            int sx, sy, y = y0 + 4 + 12 + r * KH;
            strncpy(lab, s0, sizeof lab - 1);
            lab[sizeof lab - 1] = 0;
            if (!k->shift && r >= 1 && r <= 3 && lab[0] >= 'A' && lab[0] <= 'Z') lab[0] = (char)(lab[0] + 32);
            sx = x0 + 4 + c * KW + (kw - (int)strlen(lab) * 6) / 2;
            sy = y + (KH - 2 - 8) / 2;
            fill(px, w, h, pitch, x0 + 4 + c * KW, y, kw, KH - 2, on ? 0xFFE0E060u : 0xFF303860u);
            if (r == 4 && bottom_kind[bottom(c)] == K_SHIFT && k->shift && !on)
                fill(px, w, h, pitch, x0 + 4 + c * KW, y, kw, KH - 2, 0xFF508050u);
            mdrt_menu_text(px, w, h, pitch, sx, sy, lab, on ? 0xFF000000u : 0xFFFFFFFFu);
        }
}

void ob_osk_draw(const ob_osk *k, uint32_t *px, int w, int h, int pitch) { ob_osk_draw_at(k, px, w, h, pitch, 0); }

void ob_osk_draw_at(const ob_osk *k, uint32_t *px, int w, int h, int pitch, int scale)
{
    int s = scale ? scale : osk_scale(w, h);
    static uint32_t small[(TW + 8) * (TH + 8)];
    int sw = TW + 8, sh = TH + 8, x0, y0, i, j;
    if (!k->open) return;
    if (s == 1) { osk_draw1(k, px, w, h, pitch); return; }
    memset(small, 0, sizeof small);
    osk_draw1(k, small, sw, sh, sw);                       /* the keyboard at its basic size: it sits at the bottom of the small picture */
    x0 = (w - sw * s) / 2;
    y0 = h - sh * s;
    for (j = 0; j < sh * s; j++)
        for (i = 0; i < sw * s; i++) {
            uint32_t v = small[(j / s) * sw + i / s];
            if (v >> 24) px[(size_t)(y0 + j) * pitch + x0 + i] = v;
        }
}
