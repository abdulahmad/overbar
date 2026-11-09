/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors */
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

void ob_osk_draw(const ob_osk *k, uint32_t *px, int w, int h, int pitch)
{
    int r, c, x0, y0, tw = COLS * KW + 8, th = ROWS * KH + 8;
    if (!k->open || w < tw || h < th) return;
    x0 = (w - tw) / 2;
    y0 = h - th - 4;
    fill(px, w, h, pitch, x0, y0, tw, th, 0xF0101830u);
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c += (r == 4 ? 2 : 1)) {
            int kw = (r == 4 ? 2 : 1) * KW - 2;
            int on = r == k->row && c == k->col;
            const char *s = ob_osk_label(c, r);
            int sx = x0 + 4 + c * KW + (kw - (int)strlen(s) * 6) / 2, sy = y0 + 4 + r * KH + (KH - 2 - 8) / 2;
            fill(px, w, h, pitch, x0 + 4 + c * KW, y0 + 4 + r * KH, kw, KH - 2, on ? 0xFFE0E060u : 0xFF303860u);
            if (r == 4 && bottom_kind[bottom(c)] == K_SHIFT && k->shift && !on)
                fill(px, w, h, pitch, x0 + 4 + c * KW, y0 + 4 + r * KH, kw, KH - 2, 0xFF508050u);
            mdrt_menu_text(px, w, h, pitch, sx, sy, s, on ? 0xFF000000u : 0xFFFFFFFFu);
        }
}
