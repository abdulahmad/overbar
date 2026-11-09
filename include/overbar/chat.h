/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Text chat between the machines of a room: T opens a line to type into, Enter sends it, Escape
 * closes it. Lines that come show for a few seconds over the game (toasts); while the line is open the
 * last ones show above it. No SDL here: the port (or the platform layer) passes keys in as USB HID
 * usage codes, the same values as SDL's scancodes and the on-screen keyboard's (overbar/osk.h), so the
 * on-screen keyboard types into it on a handheld.
 *
 * A line goes out as a message of type OB_NET_TYPE_CHAT (the sender's name, a 0, the text) through
 * ob_chat.send; ob_chat_message takes the ones that come. Steps are not touched: the game goes on while
 * someone types (pause is the game's own key). */
#ifndef OVERBAR_CHAT_H
#define OVERBAR_CHAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_CHAT_LINES 32            /* kept */
#define OB_CHAT_TEXT 96             /* characters of a line */
#define OB_CHAT_WHO 23
#define OB_CHAT_TOAST_FRAMES 360    /* how long a line shows over the game (60 a second) */
#define OB_CHAT_KEY_OPEN 23         /* T */

typedef struct ob_chat_line {
    char who[OB_CHAT_WHO + 1];      /* "" for the room's own news ("Bea joined") */
    char text[OB_CHAT_TEXT + 1];
    uint32_t at;                    /* the frame it came */
} ob_chat_line;

typedef struct ob_chat {
    ob_chat_line line[OB_CHAT_LINES];
    uint32_t count;                 /* lines added in all (the newest is line[(count - 1) % OB_CHAT_LINES]) */
    uint32_t frame;
    int typing;                     /* the line is open */
    int shift;                      /* a Shift key held */
    char edit[OB_CHAT_TEXT + 1];
    int len;
    char me[OB_CHAT_WHO + 1];
    void (*send)(void *ud, const void *data, size_t size);   /* to every other machine; NULL: not online */
    void *ud;
} ob_chat;

void ob_chat_init(ob_chat *c, const char *me, void (*send)(void *ud, const void *data, size_t size), void *ud);
/* a key down (down 1) or up (0), HID usage code. 1 when chat used it (the game must not see it):
 * T opens the line; while it is open every key is chat's */
int ob_chat_key(ob_chat *c, int scancode, int down);
void ob_chat_open(ob_chat *c);
void ob_chat_close(ob_chat *c);
/* (while typing) sends the line as typed, adds it here too, closes */
void ob_chat_submit(ob_chat *c);
/* a line from someone (who "" for the room's news) */
void ob_chat_add(ob_chat *c, const char *who, const char *text);
/* a message of type OB_NET_TYPE_CHAT. 1 when it was a line */
int ob_chat_message(ob_chat *c, const void *data, size_t size);
void ob_chat_tick(ob_chat *c);      /* once a frame */
int ob_chat_shows(const ob_chat *c);   /* something to draw now (the line, or a toast) */
/* the k-th newest line (0 the newest), NULL past the kept ones */
const ob_chat_line *ob_chat_get(const ob_chat *c, int k);
/* the toasts (top left) and, while typing, the line and the last lines above it (bottom), over a w x h ARGB picture */
void ob_chat_draw(const ob_chat *c, uint32_t *px, int w, int h, int pitch);

#ifdef __cplusplus
}
#endif

#endif
