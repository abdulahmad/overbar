/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Text chat (overbar/chat.h). */
#include "overbar/chat.h"
#include <string.h>
#include "mdrt/menu.h"

enum { HID_ENTER = 40, HID_ESCAPE = 41, HID_BACKSPACE = 42, HID_SPACE = 44, HID_KP_ENTER = 88,
       HID_LSHIFT = 225, HID_RSHIFT = 229 };

static void copy_text(char *to, const char *from, size_t from_n, size_t max)
{
    size_t i, k = 0;
    for (i = 0; i < from_n && from[i] && k < max; i++)
        if (from[i] >= 32 && from[i] < 127) to[k++] = from[i];
    to[k] = 0;
}

void ob_chat_init(ob_chat *c, const char *me, void (*send)(void *, const void *, size_t), void *ud)
{
    memset(c, 0, sizeof *c);
    copy_text(c->me, me ? me : "", (size_t)-1, OB_CHAT_WHO);
    c->send = send;
    c->ud = ud;
}

void ob_chat_open(ob_chat *c) { c->typing = 1; c->len = 0; c->edit[0] = 0; }
void ob_chat_close(ob_chat *c) { c->typing = 0; c->len = 0; c->edit[0] = 0; }

void ob_chat_add(ob_chat *c, const char *who, const char *text)
{
    ob_chat_line *l = &c->line[c->count % OB_CHAT_LINES];
    copy_text(l->who, who ? who : "", (size_t)-1, OB_CHAT_WHO);
    copy_text(l->text, text ? text : "", (size_t)-1, OB_CHAT_TEXT);
    l->at = c->frame;
    c->count++;
}

void ob_chat_submit(ob_chat *c)
{
    if (c->len > 0) {
        char b[OB_CHAT_WHO + 1 + OB_CHAT_TEXT + 1];
        size_t wn = strlen(c->me);
        memcpy(b, c->me, wn);
        b[wn] = 0;
        memcpy(b + wn + 1, c->edit, (size_t)c->len);
        if (c->send) c->send(c->ud, b, wn + 1 + (size_t)c->len);
        ob_chat_add(c, c->me, c->edit);
    }
    ob_chat_close(c);
}

int ob_chat_message(ob_chat *c, const void *data, size_t size)
{
    const char *b = data;
    size_t i;
    char who[OB_CHAT_WHO + 1], text[OB_CHAT_TEXT + 1];
    for (i = 0; i < size && b[i]; i++) {}
    if (i >= size) return 0;
    copy_text(who, b, i, OB_CHAT_WHO);
    copy_text(text, b + i + 1, size - i - 1, OB_CHAT_TEXT);
    if (!text[0]) return 0;
    ob_chat_add(c, who, text);
    return 1;
}

/* the character a key types (US layout), 0 none */
static char key_char(int sc, int shift)
{
    static const char digits[] = "1234567890", shifted[] = "!@#$%^&*()";
    static const struct { int sc; char plain, up; } punct[] = {
        { 45, '-', '_' }, { 46, '=', '+' }, { 47, '[', '{' }, { 48, ']', '}' }, { 49, '\\', '|' },
        { 51, ';', ':' }, { 52, '\'', '"' }, { 53, '`', '~' }, { 54, ',', '<' }, { 55, '.', '>' }, { 56, '/', '?' } };
    size_t i;
    if (sc >= 4 && sc <= 29) return (char)((shift ? 'A' : 'a') + (sc - 4));
    if (sc >= 30 && sc <= 39) return shift ? shifted[sc - 30] : digits[sc - 30];
    if (sc == HID_SPACE) return ' ';
    for (i = 0; i < sizeof punct / sizeof punct[0]; i++) if (punct[i].sc == sc) return shift ? punct[i].up : punct[i].plain;
    return 0;
}

int ob_chat_key(ob_chat *c, int sc, int down)
{
    char ch;
    if (sc == HID_LSHIFT || sc == HID_RSHIFT) { c->shift = down; return c->typing; }
    if (!c->typing) {
        if (down && sc == OB_CHAT_KEY_OPEN && c->send) { ob_chat_open(c); return 1; }
        return 0;
    }
    if (!down) return 1;
    switch (sc) {
    case HID_ENTER: case HID_KP_ENTER: ob_chat_submit(c); return 1;
    case HID_ESCAPE: ob_chat_close(c); return 1;
    case HID_BACKSPACE: if (c->len > 0) c->edit[--c->len] = 0; return 1;
    }
    ch = key_char(sc, c->shift);
    if (ch && c->len < OB_CHAT_TEXT) { c->edit[c->len++] = ch; c->edit[c->len] = 0; }
    return 1;
}

void ob_chat_tick(ob_chat *c) { c->frame++; }

int ob_chat_shows(const ob_chat *c)
{
    const ob_chat_line *l = ob_chat_get(c, 0);
    return c->typing || (l && c->frame - l->at < OB_CHAT_TOAST_FRAMES);
}

const ob_chat_line *ob_chat_get(const ob_chat *c, int k)
{
    if (k < 0 || (uint32_t)k >= c->count || k >= OB_CHAT_LINES) return NULL;
    return &c->line[(c->count - 1 - (uint32_t)k) % OB_CHAT_LINES];
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

/* a line as shown: "who: text" (or the text), cut to chars from its end when tail, else its start */
static void shown(char *b, int chars, const char *who, const char *text, int tail)
{
    char full[OB_CHAT_WHO + 3 + OB_CHAT_TEXT + 2];
    size_t wn = who ? strlen(who) : 0, tn = strlen(text);
    int n;
    if (wn) {
        memcpy(full, who, wn);
        memcpy(full + wn, ": ", 2);
        wn += 2;
    }
    memcpy(full + wn, text, tn + 1);
    n = (int)(wn + tn);
    if (chars < 0) chars = 0;
    if (n <= chars) { memcpy(b, full, (size_t)n + 1); return; }
    if (tail) memcpy(b, full + n - chars, (size_t)chars + 1);
    else { memcpy(b, full, (size_t)chars); b[chars] = 0; }
}

void ob_chat_draw(const ob_chat *c, uint32_t *px, int w, int h, int pitch)
{
    char b[OB_CHAT_WHO + 3 + OB_CHAT_TEXT + 2];
    int chars = (w - 12) / 6, k, y;
    if (w < 60 || h < 40) return;
    if (chars > (int)sizeof b - 1) chars = (int)sizeof b - 1;
    if (!c->typing) {
        /* toasts: the newest few still fresh, the oldest at the top */
        int n = 0;
        while (n < 4) {
            const ob_chat_line *l = ob_chat_get(c, n);
            if (!l || c->frame - l->at >= OB_CHAT_TOAST_FRAMES) break;
            n++;
        }
        for (k = n - 1, y = 4; k >= 0; k--, y += 12) {
            const ob_chat_line *l = ob_chat_get(c, k);
            shown(b, chars, l->who, l->text, 0);
            fill(px, w, h, pitch, 2, y - 1, (int)strlen(b) * 6 + 4, 12, 0xC0101830u);
            mdrt_menu_text(px, w, h, pitch, 4, y + 1, b, l->who[0] ? 0xFFFFFFFFu : 0xFFE0E060u);
        }
        return;
    }
    {
        int rows = (h / 2) / 12, top;
        if (rows > 8) rows = 8;
        top = h - 16 - rows * 12;
        fill(px, w, h, pitch, 0, top - 2, w, h - top + 2, 0xD0101830u);
        for (k = 0; k < rows; k++) {
            const ob_chat_line *l = ob_chat_get(c, k);
            if (!l) break;
            shown(b, chars, l->who, l->text, 0);
            mdrt_menu_text(px, w, h, pitch, 4, h - 16 - (k + 1) * 12 + 1, b, l->who[0] ? 0xFFFFFFFFu : 0xFFE0E060u);
        }
        fill(px, w, h, pitch, 2, h - 14, w - 4, 12, 0xFF303860u);
        {
            char line[OB_CHAT_TEXT + 4];
            memcpy(line, "> ", 2);
            memcpy(line + 2, c->edit, (size_t)c->len);
            line[2 + c->len] = '_';
            line[3 + c->len] = 0;
            shown(b, chars, "", line, 1);
            mdrt_menu_text(px, w, h, pitch, 4, h - 13, b, 0xFFFFFFFFu);
        }
    }
}
