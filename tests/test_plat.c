/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The platform layer on SDL's dummy video and audio drivers: no display needed. make test-plat SDL=1 */
#include <stdio.h>
#include "overbar/plat.h"

#define W 64
#define H 48
static uint32_t pic[W * H];
static int quit_called, changed_calls;
static const uint32_t *picture(void) { return pic; }
static void quit_hook(void) { quit_called++; }
static void changed(void) { changed_calls++; }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    ob_profile profile;
    ob_host host;
    ob_plat_hooks hk = { .title = "test", .win_w = 320, .win_h = 240, .cls = OB_CLASS_LORES, .src_w = W, .src_h = H,
                         .picture = picture, .changed = changed, .quit = quit_hook };
    SDL_Event e;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(ob_plat_init(&profile));
    ob_host_init(&host, OB_PORT_SNES, profile);
    CHECK(ob_plat_open(&hk, &host));
    CHECK(ob_plat_window() && ob_plat_renderer());
    for (int i = 0; i < W * H; i++) pic[i] = 0xFF000000u | (uint32_t)i;
    for (int f = 0; f < 3; f++) { ob_plat_pump(); ob_plat_present(); ob_plat_frame_done(f & 1); }
    /* the overlay key opens the overlay; a draw with it open must not fail */
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_GRAVE;
    SDL_PushEvent(&e);
    ob_plat_pump();
    CHECK(host.overlay_open);
    CHECK(changed_calls >= 1);
    ob_plat_present();
    ob_plat_close();
    CHECK(!ob_plat_window());
    printf("test_plat ok\n");
    return 0;
}
