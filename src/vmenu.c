/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "overbar/vmenu.h"
#include "overbar/vfont.h"

#define INK     0xF2F2F2u
#define DIM     0xB4B4B8u
#define MUTE    0x8C8C94u
#define ACCENT  0xF2C230u
#define WARNC   0xFF7A66u

typedef struct { float s, x0, top, rowh, listw, bot; int rows; } geom;

static int selectable(const mdrt_menu *m, int i) { return i >= 0 && i < m->count && m->items[i].kind != MDRT_ITEM_TEXT; }

static void geometry(int w, int h, geom *g)
{
    g->s = h / 540.0f;
    if (g->s < 0.5f) g->s = 0.5f;
    g->x0 = 56 * g->s;
    if (g->x0 > w * 0.12f) g->x0 = w * 0.12f;
    g->top = 184 * g->s;
    g->rowh = 30 * g->s;
    g->listw = w - 2 * g->x0;
    if (g->listw > 560 * g->s) g->listw = 560 * g->s;
    g->bot = h - 64 * g->s;
    g->rows = (int)((g->bot - g->top) / g->rowh);
    if (g->rows < 1) g->rows = 1;
}

int ob_vmenu_edge(const mdrt_menu *m, int x, int y, int w, int h)
{
    geom g;
    if (!m || w <= 0 || h <= 0) return 0;
    geometry(w, h, &g);
    if (x < g.x0 - 12 * g.s || x > g.x0 + g.listw + 12 * g.s) return 0;
    if (y >= g.top + g.rows * g.rowh - g.rowh * 0.5f && m->top + g.rows < m->count) return 1;
    if (y < g.top + g.rowh * 0.5f && y >= g.top - g.rowh * 1.5f && m->top > 0) return -1;
    return 0;
}

int ob_vmenu_at(const mdrt_menu *m, int x, int y, int w, int h)
{
    geom g;
    int row, i;
    if (!m || w <= 0 || h <= 0) return -1;
    geometry(w, h, &g);
    if (x < g.x0 - 12 * g.s || x > g.x0 + g.listw + 12 * g.s || y < g.top) return -1;
    row = (int)((y - g.top) / g.rowh);
    if (row >= g.rows) return -1;
    i = m->top + row;
    return selectable(m, i) ? i : -1;
}

/* the text shortened with an ellipsis until it fits */
static void fitv(char *out, size_t n, const char *s, ob_vf_face f, float px, float room)
{
    size_t len = strlen(s);
    snprintf(out, n, "%s", s);
    if (ob_vf_width(f, px, out, 0) <= room) return;
    while (len > 1) {
        len--;
        while (len > 1 && ((unsigned char)s[len] & 0xC0) == 0x80) len--;
        snprintf(out, n, "%.*s\xE2\x80\xA6", (int)len, s);
        if (ob_vf_width(f, px, out, 0) <= room) return;
    }
}

static void keycap(uint32_t *px, int w, int h, int pitch, float x, float y, float cw, float ch, float s, const char *label, int n)
{
    ob_vf_rrect(px, w, h, pitch, x, y, cw, ch, 4 * s, 0xFF222226u, 0xFF54545Cu, 1.2f * s);
    if (n == 1 && (label[0] == '<' || label[0] == '>' || label[0] == '^' || label[0] == 'v')) {
        float cx = x + cw / 2, cy = y + ch / 2, r = 4.5f * s;
        if (label[0] == '<') ob_vf_tri(px, w, h, pitch, cx + r * 0.6f, cy - r, cx + r * 0.6f, cy + r, cx - r * 0.8f, cy, 0xFFFFFFFFu);
        else if (label[0] == '>') ob_vf_tri(px, w, h, pitch, cx - r * 0.6f, cy - r, cx - r * 0.6f, cy + r, cx + r * 0.8f, cy, 0xFFFFFFFFu);
        else if (label[0] == '^') ob_vf_tri(px, w, h, pitch, cx - r, cy + r * 0.6f, cx + r, cy + r * 0.6f, cx, cy - r * 0.8f, 0xFFFFFFFFu);
        else ob_vf_tri(px, w, h, pitch, cx - r, cy - r * 0.6f, cx + r, cy - r * 0.6f, cx, cy + r * 0.8f, 0xFFFFFFFFu);
        return;
    }
    {
        char buf[32];
        float px_em = 12 * s, tw;
        snprintf(buf, sizeof buf, "%.*s", n < 31 ? n : 31, label);
        tw = ob_vf_width(OB_VF_MEDIUM, px_em, buf, 0);
        ob_vf_draw(px, w, h, pitch, x + (cw - tw) / 2, y + ch / 2 + px_em * 0.35f, OB_VF_MEDIUM, px_em, 0xFFFFFF, 255, buf, 0, 0);
    }
}

static void padbtn(uint32_t *px, int w, int h, int pitch, float x, float y, float d, float s, char c)
{
    uint32_t col = c == 'A' ? 0xFF58AA3Cu : c == 'B' ? 0xFFD63428u : c == 'X' ? 0xFF2E78D2u : 0xFFE0B020u;
    char one[2] = { c, 0 };
    float em = 12 * s, tw = ob_vf_width(OB_VF_SEMIBOLD, em, one, 0);
    ob_vf_disc(px, w, h, pitch, x + d / 2, y + d / 2, d / 2, col, 0xFF141416u, 1.2f * s);
    ob_vf_draw(px, w, h, pitch, x + (d - tw) / 2, y + d / 2 + em * 0.35f, OB_VF_SEMIBOLD, em, 0xFFFFFF, 255, one, 0, 0);
}

/* the hint markup: [Key], [<] arrows, {A} pad buttons, other text as words. y is the line's vertical centre. Returns the width. */
static float hints(uint32_t *px, int w, int h, int pitch, float x, float yc, float s, const char *str, int draw)
{
    float x0 = x, ch = 20 * s, gap = 6 * s, em = 13 * s;
    while (*str) {
        if (*str == '[') {
            const char *e = strchr(str + 1, ']');
            int n;
            float cw;
            char buf[32];
            if (!e) break;
            n = (int)(e - str - 1);
            snprintf(buf, sizeof buf, "%.*s", n < 31 ? n : 31, str + 1);
            cw = (n == 1 && strchr("<>^v", buf[0])) ? ch * 1.15f : ob_vf_width(OB_VF_MEDIUM, 12 * s, buf, 0) + 14 * s;
            if (cw < ch) cw = ch;
            if (draw) keycap(px, w, h, pitch, x, yc - ch / 2, cw, ch, s, buf, n);
            x += cw + gap;
            str = e + 1;
        } else if (*str == '{' && str[1] && str[2] == '}') {
            if (draw) padbtn(px, w, h, pitch, x, yc - ch / 2, ch, s, str[1]);
            x += ch + gap;
            str += 3;
        } else {
            char run[96];
            const char *r;
            int n = 0, raw;
            while (str[n] && str[n] != '[' && str[n] != '{' && n < 95) n++;
            raw = n;
            memcpy(run, str, (size_t)n);
            run[n] = 0;
            while (n > 0 && run[n - 1] == ' ') run[--n] = 0;
            r = run;
            while (*r == ' ') r++;
            if (*r) {
                float tight = (r[0] == '/' && r[1] == 0) ? -gap * 0.5f : 0;   /* a lone slash sits close between two keys */
                x += tight;
                if (draw) ob_vf_draw(px, w, h, pitch, x, yc + em * 0.35f, OB_VF_REGULAR, em, MUTE, 255, r, 0, 1);
                x += ob_vf_width(OB_VF_REGULAR, em, r, 0) + (tight ? 0 : gap) + (tight ? -gap * 0.5f + gap : gap);
            } else x += gap;
            str += raw ? raw : 1;
        }
    }
    return x - x0;
}

void ob_vmenu_draw(mdrt_menu *m, uint32_t *px, int w, int h, int pitch, const char *name, const char *platform)
{
    geom g;
    float s, x0, y, right;
    int r, i;
    char buf[200];
    geometry(w, h, &g);
    s = g.s; x0 = g.x0;
    /* the shade at the left, fading out toward the right */
    for (int yy = 0; yy < h; yy++) {
        uint32_t *row = px + (size_t)yy * pitch;
        for (int xx = 0; xx < w; xx++) {
            float t = (float)xx / (w * 0.62f);
            int a = t >= 1 ? 0 : (int)(150 * (1 - t) * (1 - t) + 0.5f);
            if (a) row[xx] = (uint32_t)a << 24;
        }
    }
    /* the title: the game's name, its platform in small yellow type, an accent line */
    y = 84 * s;
    if (name && *name) {
        ob_vf_draw(px, w, h, pitch, x0, y, OB_VF_LIGHT, 48 * s, 0xFFFFFF, 255, name, 0, 3);
        if (platform && *platform) {
            char up[80];
            int n = 0;
            for (; platform[n] && n < 79; n++) up[n] = (platform[n] >= 'a' && platform[n] <= 'z') ? platform[n] - 32 : platform[n];
            up[n] = 0;
            ob_vf_draw(px, w, h, pitch, x0 + 2 * s, y + 22 * s, OB_VF_SEMIBOLD, 12 * s, ACCENT, 255, up, 2.6f * s, 2);
        }
        ob_vf_rrect(px, w, h, pitch, x0 + 2 * s, y + 31 * s, 40 * s, 2 * s, 1 * s, 0xFF000000u | ACCENT, 0, 0);
    }
    /* this page's title and key hints */
    {   /* this page's title (not on the main page, whose title is just the program's name) and the key hints */
        int sub = strcmp(m->title, "Overbar") != 0;
        float hy = sub ? 170 * s : 152 * s;
        fitv(buf, sizeof buf, m->title, OB_VF_MEDIUM, 15 * s, g.listw);
        if (!name || !*name) ob_vf_draw(px, w, h, pitch, x0, 100 * s, OB_VF_LIGHT, 40 * s, 0xFFFFFF, 255, buf, 0, 3);
        else if (sub) ob_vf_draw(px, w, h, pitch, x0 + 2 * s, 152 * s, OB_VF_MEDIUM, 15 * s, DIM, 255, buf, 1.5f * s, 1);
        if (strpbrk(m->subtitle, "[{")) hints(px, w, h, pitch, x0 + 2 * s, hy, s, m->subtitle, 1);
        else if (m->subtitle[0]) {
            fitv(buf, sizeof buf, m->subtitle, OB_VF_REGULAR, 13 * s, g.listw);
            ob_vf_draw(px, w, h, pitch, x0 + 2 * s, hy + 5 * s, OB_VF_REGULAR, 13 * s, MUTE, 255, buf, 0, 1);
        }
    }
    /* the list */
    m->rows = g.rows;
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= m->top + g.rows) m->top = m->sel - g.rows + 1;
    if (m->top > m->count - g.rows) m->top = m->count - g.rows > 0 ? m->count - g.rows : 0;
    right = x0 + g.listw;
    if (m->top > 0) ob_vf_tri(px, w, h, pitch, right - 20 * s, g.top - 6 * s, right - 12 * s, g.top - 6 * s, right - 16 * s, g.top - 12 * s, 0xFFB4B4B8u);
    for (r = 0; r < g.rows && m->top + r < m->count; r++) {
        const mdrt_menu_item *it;
        char rt[120] = "";
        float ry, base, rw = 0;
        int sel;
        i = m->top + r;
        it = &m->items[i];
        sel = i == m->sel && selectable(m, i);
        ry = g.top + r * g.rowh;
        base = ry + g.rowh * 0.5f + 22 * s * 0.34f;
        if (sel) {
            ob_vf_rrect(px, w, h, pitch, x0 - 12 * s, ry + 1 * s, g.listw + 24 * s, g.rowh - 2 * s, 6 * s, 0x2EFFFFFFu, 0, 0);
            ob_vf_tri(px, w, h, pitch, x0 - 8 * s, ry + g.rowh * 0.5f - 5 * s, x0 - 8 * s, ry + g.rowh * 0.5f + 5 * s, x0 - 0.5f * s, ry + g.rowh * 0.5f, 0xFF000000u | ACCENT);
        }
        if (it->kind == MDRT_ITEM_CHOICE && it->count > 0) snprintf(rt, sizeof rt, "%s", it->choices[it->value]);
        else if (it->kind == MDRT_ITEM_NUMBER) snprintf(rt, sizeof rt, "%d%s", it->value, it->unit ? it->unit : "");
        else if (it->kind == MDRT_ITEM_FIELD) snprintf(rt, sizeof rt, i == m->editing ? "%s_" : "%s", it->detail);
        else snprintf(rt, sizeof rt, "%s", it->detail);
        if (rt[0]) {
            float arrows = (sel && (it->kind == MDRT_ITEM_CHOICE || it->kind == MDRT_ITEM_NUMBER)) ? 30 * s : 0;
            { char tmp[120]; snprintf(tmp, sizeof tmp, "%s", rt); fitv(rt, sizeof rt, tmp, OB_VF_REGULAR, 18 * s, g.listw * 0.5f - arrows); }
            rw = ob_vf_width(OB_VF_REGULAR, 18 * s, rt, 0);
            if (arrows > 0) {
                float cy = ry + g.rowh * 0.5f, t = 4.5f * s;
                ob_vf_tri(px, w, h, pitch, right - arrows - rw - 6 * s + t, cy - t, right - arrows - rw - 6 * s + t, cy + t, right - arrows - rw - 6 * s - t * 0.8f, cy, 0xFFFFFFFFu);
                ob_vf_tri(px, w, h, pitch, right - 6 * s - t, cy - t, right - 6 * s - t, cy + t, right - 6 * s + t * 0.8f, cy, 0xFFFFFFFFu);
                ob_vf_draw(px, w, h, pitch, right - arrows - rw + 8 * s - 2 * s, base - 1 * s, OB_VF_REGULAR, 18 * s, ACCENT, 255, rt, 0, 2);
            } else
                ob_vf_draw(px, w, h, pitch, right - rw, base - 1 * s, OB_VF_REGULAR, 18 * s, it->kind == MDRT_ITEM_TEXT ? MUTE : DIM, 255, rt, 0, 2);
            rw += arrows + 20 * s;
        }
        fitv(buf, sizeof buf, it->label, sel ? OB_VF_REGULAR : OB_VF_LIGHT, 22 * s, g.listw - rw - 8 * s);
        ob_vf_draw(px, w, h, pitch, x0 + 6 * s, base, sel ? OB_VF_REGULAR : OB_VF_LIGHT, 22 * s, it->kind == MDRT_ITEM_TEXT ? MUTE : sel ? 0xFFFFFF : INK, 255, buf, 0, 2);
    }
    if (m->top + g.rows < m->count)
        ob_vf_tri(px, w, h, pitch, right - 20 * s, g.top + g.rows * g.rowh + 8 * s, right - 12 * s, g.top + g.rows * g.rowh + 8 * s, right - 16 * s, g.top + g.rows * g.rowh + 14 * s, 0xFFB4B4B8u);
    if (m->image && m->image_w > 1 && m->image_h > 1) {
        float iw = m->image_w * s * 0.5f, ih = m->image_h * s * 0.5f, ix = right + 24 * s, iy = g.top;
        if (ix + iw > w - 8 * s) ix = w - 8 * s - iw;
        for (int yy = 0; yy < (int)ih; yy++)
            for (int xx = 0; xx < (int)iw; xx++) {
                int px_y = (int)(yy / ih * m->image_h), px_x = (int)(xx / iw * m->image_w), X = (int)ix + xx, Y = (int)iy + yy;
                if (X >= 0 && X < w && Y >= 0 && Y < h) px[Y * pitch + X] = 0xFF000000u | (m->image[px_y * m->image_pitch + px_x] & 0xFFFFFFu);
            }
    }
    if (m->message[0]) {
        fitv(buf, sizeof buf, m->message, OB_VF_REGULAR, 13 * s, w - 2 * x0);
        ob_vf_draw(px, w, h, pitch, x0 + 2 * s, h - 66 * s + 4 * s, OB_VF_REGULAR, 13 * s, WARNC, 255, buf, 0, 1);
    }
    {
        const mdrt_menu_item *it = m->count && selectable(m, m->sel) ? &m->items[m->sel] : NULL;
        const char *keys = m->editing >= 0 ? "Type it   [Enter] keep it   [Esc] as it was"
                         : it && it->kind == MDRT_ITEM_FIELD ? "[Enter] type into it   [Backspace]/{B} back"
                         : it && (it->kind == MDRT_ITEM_CHOICE || it->kind == MDRT_ITEM_NUMBER)
                         ? "[Enter]/{A} choose  [Backspace]/{B} back  [<][>] change" : "[Enter]/{A} choose   [Backspace]/{B} back";
        hints(px, w, h, pitch, x0 + 2 * s, h - 32 * s, s, keys, 1);
    }
}
