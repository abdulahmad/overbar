/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * Players' pads and the controller setup screen, generalized from the SNES port's platform/main.c
 * and platform/padsetup.c (PulseMan-derived, MIT).
 */
#include "overbar/plat_pads.h"

#ifdef OVERBAR_SDL
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mdrt/menu.h"

#define MAXB 16
enum { TRIGGER_L = 100, TRIGGER_R };

static ob_pads_cfg C;
static SDL_Scancode key[OB_PADS_MAX][MAXB];
static int player_ctrl[OB_PADS_MAX];                  /* each player's controller slot + 1 (0: none) */
static int cbutton[MAXB];
static int dead_zone = 50;
static int setup, setup_slot, setup_at, setup_dead, setup_map[MAXB];
static uint32_t setup_since;
static uint32_t *setup_buf;
static SDL_Texture *setup_tex;

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int cinput_from(const char *v)
{
    if (!strcmp(v, "lefttrigger")) return TRIGGER_L;
    if (!strcmp(v, "righttrigger")) return TRIGGER_R;
    return SDL_GameControllerGetButtonFromString(v);
}

static const char *cinput_name(int v)
{
    if (v == TRIGGER_L) return "lefttrigger";
    if (v == TRIGGER_R) return "righttrigger";
    const char *n = v != SDL_CONTROLLER_BUTTON_INVALID ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)v) : NULL;
    return n ? n : "";
}

static int cinput_held(SDL_GameController *c, int v)
{
    if (v == TRIGGER_L || v == TRIGGER_R)
        return SDL_GameControllerGetAxis(c, v == TRIGGER_L ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384;
    return v != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)v);
}

static SDL_Scancode bound(const char *name)
{
    SDL_Scancode k = name && *name ? SDL_GetScancodeFromName(name) : SDL_SCANCODE_UNKNOWN;
    if (name && *name && k == SDL_SCANCODE_UNKNOWN) fprintf(stderr, "settings: no key called \"%s\"\n", name);
    return k;
}

void ob_pads_init(const ob_pads_cfg *c)
{
    C = *c;
    if (C.n > MAXB) C.n = MAXB;
    if (C.players > OB_PADS_MAX) C.players = OB_PADS_MAX;
    for (int p = 0; p < C.players; p++) {
        char k[48], slot[4];
        for (int b = 0; b < C.n; b++) {
            snprintf(k, sizeof k, "pad%d.%s", p + 1, C.b[b].name);
            key[p][b] = bound(C.get(k, p == 0 ? C.b[b].key1 : p == 1 ? C.b[b].key2 : ""));
        }
        snprintf(k, sizeof k, "player%d.controller", p + 1);
        snprintf(slot, sizeof slot, "%d", p + 1);
        player_ctrl[p] = clamp(atoi(C.get(k, slot)), 0, OB_PLAT_SLOTS);
    }
    for (int b = 0; b < C.n; b++) {
        cbutton[b] = SDL_CONTROLLER_BUTTON_INVALID;
        if (b < 4) continue;                              /* the directions: the d-pad and the stick */
        char k[48];
        snprintf(k, sizeof k, "controller.%s", C.b[b].name);
        const char *v = C.get(k, C.b[b].ctrl);
        cbutton[b] = v && *v ? cinput_from(v) : SDL_CONTROLLER_BUTTON_INVALID;
        if (v && *v && cbutton[b] == SDL_CONTROLLER_BUTTON_INVALID) fprintf(stderr, "settings: no controller button called \"%s\"\n", v);
    }
    dead_zone = clamp(atoi(C.get("controller.dead_zone", "50")), 5, 95);
}

SDL_GameController *ob_pads_controller(int p)
{
    return p >= 0 && p < C.players && player_ctrl[p] ? ob_plat_controller(player_ctrl[p] - 1) : NULL;
}

static uint32_t ctrl_buttons(SDL_GameController *c)
{
    static const SDL_GameControllerButton dpad[4] = { SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                                      SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    uint32_t b = 0;
    for (int i = 0; i < 4; i++) if (SDL_GameControllerGetButton(c, dpad[i])) b |= C.b[i].bit;
    for (int i = 4; i < C.n; i++) if (cinput_held(c, cbutton[i])) b |= C.b[i].bit;
    int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    int dead = 32767 * dead_zone / 100;
    if (y < -dead) b |= C.b[0].bit;
    if (y > dead) b |= C.b[1].bit;
    if (x < -dead) b |= C.b[2].bit;
    if (x > dead) b |= C.b[3].bit;
    return b;
}

uint32_t ob_pads_read(int p)
{
    const Uint8 *down = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    SDL_GameController *c;
    if (p < 0 || p >= C.players || setup) return 0;
    for (int i = 0; i < C.n; i++) if (key[p][i] != SDL_SCANCODE_UNKNOWN && down[key[p][i]]) b |= C.b[i].bit;
    if ((c = ob_pads_controller(p))) b |= ctrl_buttons(c);
    return b;
}

int ob_pads_extra(void)
{
    for (int p = 2; p < C.players; p++) {
        if (ob_pads_controller(p)) return 1;
        for (int b = 0; b < C.n; b++) if (key[p][b] != SDL_SCANCODE_UNKNOWN) return 1;
    }
    return 0;
}

/* ---- the controller setup screen ---- */

static void assign(int *map, int n, int i, int value)
{
    for (int j = 0; j < n; j++)
        if (j != i && map[j] == value) map[j] = map[i];
    map[i] = value;
}

void ob_pads_setup_open(int slot)
{
    setup = 1;
    setup_slot = slot;
    setup_at = 4;
    setup_since = SDL_GetTicks();
    memcpy(setup_map, cbutton, sizeof setup_map);
    setup_dead = dead_zone;
}

int ob_pads_setup_active(void) { return setup; }

static void setup_close(int save)
{
    if (save) {
        char k[48], v[8];
        for (int i = 4; i < C.n; i++) {
            snprintf(k, sizeof k, "controller.%s", C.b[i].name);
            C.set(k, cinput_name(setup_map[i]));
            cbutton[i] = setup_map[i];
        }
        dead_zone = setup_dead;
        snprintf(v, sizeof v, "%d", dead_zone);
        C.set("controller.dead_zone", v);
        C.save();
    }
    setup = 0;
}

void ob_pads_setup_button(int slot, int button)
{
    int dpad;
    if (button == -1) button = TRIGGER_L;
    else if (button == -2) button = TRIGGER_R;
    if (setup_slot < 0) { ob_pads_setup_open(slot); return; }
    if (slot != setup_slot) return;
    dpad = button >= SDL_CONTROLLER_BUTTON_DPAD_UP && button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (setup_at >= 0) {
        if (dpad) return;
        assign(setup_map + 4, C.n - 4, setup_at - 4, button);
        if (++setup_at == C.n) setup_at = -1;
        setup_since = SDL_GetTicks();
        return;
    }
    if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) setup_dead = clamp(setup_dead - 5, 5, 95);
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) setup_dead = clamp(setup_dead + 5, 5, 95);
    else if (button == setup_map[C.start]) setup_close(1);
}

void ob_pads_setup_key(int k)
{
    if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F9) setup_close(0);
    else if (setup_at == -1 && k == SDL_SCANCODE_LEFT) setup_dead = clamp(setup_dead - 5, 5, 95);
    else if (setup_at == -1 && k == SDL_SCANCODE_RIGHT) setup_dead = clamp(setup_dead + 5, 5, 95);
    else if (setup_at == -1 && k == SDL_SCANCODE_RETURN) setup_close(1);
}

void ob_pads_tick(void)
{
    if (setup && setup_slot >= 0 && !ob_plat_controller(setup_slot)) { setup_close(0); return; }
    if (setup && setup_at >= 0 && setup_slot >= 0 && SDL_GetTicks() - setup_since >= 5000) {
        if (++setup_at == C.n) setup_at = -1;
        setup_since = SDL_GetTicks();
    }
}

enum { CW = MDRT_MENU_CELL_W, CH = MDRT_MENU_CELL_H };
#define SHADE 0xC0000000u
#define WHITE 0xFFFFFF
#define DIM 0x9090A0
#define GOLD 0xFFD84A
#define RED 0xFF5A4A

static void text(uint32_t *px, int w, int h, int col, int row, const char *s, uint32_t rgb)
{
    mdrt_menu_text(px, w, h, w, 4 + col * CW, 4 + row * CH, s, rgb);
}

static void dot(uint32_t *px, int w, int h, int x, int y, uint32_t rgb)
{
    if (x >= 0 && x < w && y >= 0 && y < h) px[y * w + x] = rgb;
}

static void square(uint32_t *px, int w, int h, int cx, int cy, int r, uint32_t rgb)
{
    for (int k = -r; k <= r; k++) {
        dot(px, w, h, cx + k, cy - r, rgb); dot(px, w, h, cx + k, cy + r, rgb);
        dot(px, w, h, cx - r, cy + k, rgb); dot(px, w, h, cx + r, cy + k, rgb);
    }
}

static void draw_screen(uint32_t *px, int w, int h)
{
    char line[64], names[MAXB][16];
    const char *name_p[MAXB], *value_p[MAXB], *role_p[MAXB];
    int n = C.n - 4, none = 1, sx_ = 0, sy_ = 0;
    SDL_GameController *c = setup_slot >= 0 ? ob_plat_controller(setup_slot) : NULL;
    for (int i = 0; i < OB_PLAT_SLOTS; i++) if (ob_plat_controller(i)) none = 0;
    for (int i = 0; i < n; i++) {
        snprintf(names[i], sizeof names[i], "%s", C.b[i + 4].name);
        for (char *q = names[i]; *q; q++) *q = (char)toupper((unsigned char)*q);
        name_p[i] = names[i];
        value_p[i] = cinput_name(setup_map[i + 4]);
        role_p[i] = C.b[i + 4].role ? C.b[i + 4].role : "";
    }
    if (c) { sx_ = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX); sy_ = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY); }
    const char *cname = c ? SDL_GameControllerName(c) : NULL;
    const char *ctl = setup_slot < 0 ? "" : cname ? cname : "a controller";
    int at = setup_slot < 0 ? -2 : setup_at < 0 ? -1 : setup_at - 4;
    int seconds = 5 - (int)((SDL_GetTicks() - setup_since) / 1000);
    for (int i = 0; i < w * h; i++) px[i] = SHADE;
    text(px, w, h, 0, 0, "CONTROLLER SETUP", GOLD);
    if (ctl[0]) { snprintf(line, sizeof line, "%.40s", ctl); text(px, w, h, 0, 1, line, DIM); }
    if (at == -2) {
        text(px, w, h, 0, 4, none ? "No controller is plugged in." : "Press any button on the controller", WHITE);
        if (!none) text(px, w, h, 0, 5, "to set up.", WHITE);
        text(px, w, h, 0, 8, "Esc: close", DIM);
    } else if (at >= 0) {
        snprintf(line, sizeof line, "Press the button for %s", name_p[at]);
        text(px, w, h, 0, 3, line, WHITE);
        if (role_p[at][0]) snprintf(line, sizeof line, "(%s; %d s: kept as it is)", role_p[at], seconds);
        else snprintf(line, sizeof line, "(nothing for %d s: kept as it is)", seconds);
        text(px, w, h, 0, 4, line, DIM);
        for (int i = 0; i < n; i++) {
            snprintf(line, sizeof line, "%c %-6s %-13s %s", i == at ? 0x7F : ' ', name_p[i], value_p[i][0] ? value_p[i] : "-", role_p[i]);
            text(px, w, h, 0, 6 + i, line, i == at ? GOLD : i < at ? WHITE : DIM);
        }
        text(px, w, h, 0, 7 + n, "Esc: cancel", DIM);
    } else {
        snprintf(line, sizeof line, "Dead zone: %d%%", setup_dead);
        text(px, w, h, 0, 3, line, WHITE);
        text(px, w, h, 0, 4, "Left / Right: smaller / larger", DIM);
        int r = 50, cx = w / 2, cy = 4 + 7 * CH + r;
        square(px, w, h, cx, cy, r, DIM);
        square(px, w, h, cx, cy, r * setup_dead / 100, GOLD);
        int sx = cx + sx_ * r / 32768, sy = cy + sy_ * r / 32768, dead = 32767 * setup_dead / 100;
        int out = abs(sx_) > dead || abs(sy_) > dead;
        for (int y = -2; y <= 2; y++)
            for (int x = -2; x <= 2; x++) if (x * x + y * y <= 5) dot(px, w, h, sx + x, sy + y, out ? RED : WHITE);
        text(px, w, h, 0, 6, "Red: the stick counts as a direction.", DIM);
        text(px, w, h, 0, 20, "Start or Enter: save   Esc: cancel", WHITE);
    }
    for (int i = 0; i < w * h; i++) if (!(px[i] >> 24)) px[i] |= 0xFF000000u;
}

void ob_pads_setup_draw(const SDL_Rect *dst)
{
    SDL_Renderer *ren = ob_plat_renderer();
    if (!setup || !ren) return;
    if (!setup_buf && !(setup_buf = malloc((size_t)C.src_w * (size_t)C.src_h * 4))) return;
    if (!setup_tex) {
        setup_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, C.src_w, C.src_h);
        if (setup_tex) SDL_SetTextureBlendMode(setup_tex, SDL_BLENDMODE_BLEND);
    }
    if (!setup_tex) return;
    draw_screen(setup_buf, C.src_w, C.src_h);
    SDL_UpdateTexture(setup_tex, NULL, setup_buf, C.src_w * 4);
    SDL_RenderCopy(ren, setup_tex, NULL, dst);
}
#endif
