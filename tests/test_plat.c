/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The platform layer on SDL's dummy video and audio drivers: no display needed. make test-plat SDL=1 */
#include <stdio.h>
#include <string.h>
#include "overbar/plat.h"
#include "overbar/osk.h"
#include "overbar/sides.h"
#include "overbar/chat.h"
#include "mdrt/menu.h"

#define W 64
#define H 48
static uint32_t pic[W * H];
static int quit_called, changed_calls;
static const uint32_t *picture(void) { return pic; }
static void quit_hook(void) { quit_called++; }
static void changed(void) { changed_calls++; }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int sent[64], nsent;
static void osk_sink(int sc, int down, void *ud) { (void)ud; if (nsent < 64) sent[nsent++] = down ? sc : -sc; }

static int test_osk(void)
{
    ob_osk k;
    static uint32_t px[320 * 240];
    ob_osk_init(&k, osk_sink, NULL);
    CHECK(!ob_osk_input(&k, MDRT_MENU_OK) && nsent == 0);          /* closed: nothing */
    ob_osk_open(&k);
    CHECK(!strcmp(ob_osk_label(k.col, k.row), "Q"));
    CHECK(ob_osk_input(&k, MDRT_MENU_OK));                          /* Q down, up */
    CHECK(nsent == 2 && sent[0] == 20 && sent[1] == -20);
    ob_osk_input(&k, MDRT_MENU_UP);                                 /* the digits */
    ob_osk_input(&k, MDRT_MENU_OK);                                 /* 1 */
    CHECK(sent[2] == 30);
    nsent = 0;
    ob_osk_input(&k, MDRT_MENU_DOWN);
    ob_osk_input(&k, MDRT_MENU_DOWN);
    ob_osk_input(&k, MDRT_MENU_DOWN);
    ob_osk_input(&k, MDRT_MENU_DOWN);                               /* the bottom row */
    CHECK(!strcmp(ob_osk_label(k.col, k.row), "Space"));
    ob_osk_input(&k, MDRT_MENU_RIGHT);                              /* the bottom keys are two columns wide */
    CHECK(!strcmp(ob_osk_label(k.col, k.row), "Del"));
    ob_osk_input(&k, MDRT_MENU_RIGHT);
    CHECK(!strcmp(ob_osk_label(k.col, k.row), "Shift"));
    ob_osk_input(&k, MDRT_MENU_OK);                                 /* Shift latches */
    CHECK(k.shift && nsent == 0);
    ob_osk_input(&k, MDRT_MENU_UP);
    ob_osk_input(&k, MDRT_MENU_OK);                                 /* a shifted letter: Shift down, key down, up, Shift up */
    CHECK(nsent == 4 && sent[0] == OB_OSK_LSHIFT && sent[3] == -OB_OSK_LSHIFT && !k.shift);
    nsent = 0;
    ob_osk_input(&k, MDRT_MENU_BACK);                               /* B: backspace */
    CHECK(nsent == 2 && sent[0] == 42);
    ob_osk_draw(&k, px, 320, 240, 320);
    CHECK(px[239 * 320 + 160] != 0 || px[230 * 320 + 160] != 0);
    for (int i = 0; i < 8; i++) ob_osk_input(&k, MDRT_MENU_DOWN);
    ob_osk_close(&k);
    CHECK(!k.open);
    return 0;
}

static void key(int sc, int down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.scancode = (SDL_Scancode)sc;
    SDL_PushEvent(&e);
}
static void tap(int sc) { key(sc, 1); key(sc, 0); ob_plat_pump(); }
static int sides_applied, chat_sent;
static char chat_text[64];
static void sides_apply(void *ud, const ob_sides_seat *seat, int n) { (void)ud; (void)seat; sides_applied = n + 1; }
static void chat_send(void *ud, const void *d, size_t n) { (void)ud; chat_sent++; if (n < sizeof chat_text) { memcpy(chat_text, d, n); chat_text[n] = 0; } }

/* the select-sides screen and chat through the layer's keys */
static int test_room_keys(void)
{
    ob_sides s;
    ob_chat c;
    ob_sides_game g = { .teams = 2, .seats = 2, .apply = sides_apply };
    ob_sides_init(&s, &g, 0, NULL, NULL);
    ob_chat_init(&c, "Ann", chat_send, NULL);
    ob_plat_room(&s, &c);
    ob_plat_sides_devices(&s);
    CHECK(s.n >= 1 && !strcmp(s.d[0].name, "Keyboard"));
    ob_sides_open(&s);
    tap(SDL_SCANCODE_LEFT);
    CHECK(s.d[0].team == 0);
    tap(SDL_SCANCODE_RETURN);
    CHECK(s.d[0].ready);
    ob_plat_present();
    tap(SDL_SCANCODE_RETURN);
    CHECK(sides_applied == 2 && s.done);
    tap(SDL_SCANCODE_T);
    CHECK(c.typing);
    tap(SDL_SCANCODE_H);
    tap(SDL_SCANCODE_I);
    ob_plat_present();
    tap(SDL_SCANCODE_RETURN);
    CHECK(!c.typing && chat_sent == 1 && !strcmp(chat_text + 4, "hi"));
    ob_plat_present();
    ob_plat_room(NULL, NULL);
    return 0;
}

static void mouse_click(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    SDL_PushEvent(&e);
    ob_plat_pump();
}

static void pad_press(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    SDL_PushEvent(&e);
    ob_plat_pump();
}

/* auto-open only after a controller; F9 always toggles */
static int test_osk_auto(void)
{
    CHECK(!ob_plat_osk_is_open());
    ob_plat_osk_open();
    CHECK(!ob_plat_osk_is_open());                         /* last input was a key (room / overlay), not a pad */
    tap(SDL_SCANCODE_F9);
    CHECK(ob_plat_osk_is_open());                           /* F9 opens by hand */
    tap(SDL_SCANCODE_F9);
    CHECK(!ob_plat_osk_is_open());
    mouse_click();
    ob_plat_osk_open();
    CHECK(!ob_plat_osk_is_open());                         /* last input was the mouse */
    pad_press();
    ob_plat_osk_open();
    CHECK(ob_plat_osk_is_open());                           /* last input was a controller */
    ob_plat_osk_close();
    CHECK(!ob_plat_osk_is_open());
    tap(SDL_SCANCODE_A);
    ob_plat_osk_open();
    CHECK(!ob_plat_osk_is_open());                         /* last input was a key again */
    tap(SDL_SCANCODE_F9);
    CHECK(ob_plat_osk_is_open());                           /* F9 still opens after a key */
    ob_plat_osk_close();
    return 0;
}

int main(void)
{
    if (test_osk()) return 1;
    ob_profile profile;
    ob_host host;
    ob_plat_hooks hk = { .title = "test", .win_w = 320, .win_h = 240, .cls = OB_CLASS_LORES, .src_w = W, .src_h = H,
                         .picture = picture, .changed = changed, .quit = quit_hook };
    SDL_Event e;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("OVERBAR_TEST_KEYS", "0:f11", 1);                 /* the scripted key hook: opens the overlay with no real input */
    CHECK(ob_plat_init(&profile));
    ob_host_init(&host, OB_PORT_SNES, profile);
    CHECK(ob_plat_open(&hk, &host));
    CHECK(ob_plat_window() && ob_plat_renderer());
    for (int i = 0; i < W * H; i++) pic[i] = 0xFF000000u | (uint32_t)i;
    for (int f = 0; f < 3; f++) { ob_plat_pump(); ob_plat_present(); ob_plat_frame_done(f & 1); }
    SDL_Delay(20);
    ob_plat_pump();
    CHECK(host.overlay_open);                               /* from OVERBAR_TEST_KEYS */
    /* the overlay key opens the overlay; a draw with it open must not fail */
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_GRAVE;
    SDL_PushEvent(&e);
    ob_plat_pump();
    CHECK(!host.overlay_open);                              /* the overlay key again closes it */
    CHECK(changed_calls >= 1);
    ob_plat_present();
    if (test_room_keys()) return 1;
    if (test_osk_auto()) return 1;
    ob_plat_close();
    CHECK(!ob_plat_window());
    printf("test_plat ok\n");
    return 0;
}
