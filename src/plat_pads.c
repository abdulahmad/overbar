/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
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
static ob_sides S;                                    /* the select-sides screen */
static int sides_ready;
static unsigned kbd_mask = ~0u;                       /* the players whose keys play (the screen narrows it) */
static int mouse_player = -1;                         /* the player the mouse plays (-1: none) */
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

/* ---- the select-sides screen ---- */

static void (*seats_cb)(const ob_pads_seats *, void *);
static void *seats_ud;
static int seats_players = 2;
static struct { int8_t local, team; } last[OB_SIDES_MAX_DEVICES];   /* where the devices went last time: the screen opens with them there, ready */
static int last_n;

/* seat k of a team becomes player team + 2 * k: a controller plays it, the keyboard's keys of that player, or the mouse */
static void sides_apply(void *ud, const ob_sides_seat *seat, int n)
{
    int count[2] = { 0, 0 };
    ob_pads_seats r;
    (void)ud;
    memset(&r, 0, sizeof r);
    r.mouse_player = -1;
    last_n = 0;
    for (int k = 0; k < n && last_n < OB_SIDES_MAX_DEVICES; k++) { last[last_n].local = (int8_t)seat[k].local; last[last_n].team = (int8_t)seat[k].team; last_n++; }
    for (int k = 0; k < n; k++) {
        int t = seat[k].team & 1, p = t + 2 * count[t]++;
        if (p >= seats_players || p >= OB_PADS_MAX) continue;
        if (seat[k].local == 0) r.kbd_mask |= 1u << p;
        else if (seat[k].local == 100) r.mouse_player = p;
        else r.ctrl[p] = seat[k].local;                        /* 1 + the controller's slot */
    }
    mouse_player = r.mouse_player;
    if (seats_cb) { seats_cb(&r, seats_ud); return; }
    kbd_mask = r.kbd_mask;
    for (int p = 0; p < C.players; p++) player_ctrl[p] = r.ctrl[p];
}

static struct { int set, players; const char *const *names; const ob_sides_skin *skin; void (*apply)(const ob_pads_seats *, void *); void *ud; } reg;

void ob_pads_sides_register(int players, const char *const team_name[2], const ob_sides_skin *skin, void (*apply)(const ob_pads_seats *, void *), void *ud)
{
    reg.set = 1; reg.players = players; reg.names = team_name; reg.skin = skin; reg.apply = apply; reg.ud = ud;
}

void ob_pads_sides_open_ex(int players, const char *const team_name[2], const ob_sides_skin *skin, void (*apply)(const ob_pads_seats *, void *), void *ud)
{
    ob_sides_game g;
    memset(&g, 0, sizeof g);
    seats_players = players < 2 ? 2 : players;
    seats_cb = apply;
    seats_ud = ud;
    g.teams = 2;
    g.seats = seats_players;
    g.per_team = g.seats / 2;
    g.team_name[0] = team_name && team_name[0] ? team_name[0] : "Away";
    g.team_name[1] = team_name && team_name[1] ? team_name[1] : "Home";
    if (skin) g.skin = *skin;
    g.apply = sides_apply;
    ob_plat_overlay_close();
    ob_sides_init(&S, &g, 0, NULL, NULL);
    ob_plat_sides_devices(&S);
    ob_plat_room(&S, NULL);
    ob_sides_open(&S);
    for (int i = 0; i < S.n; i++)                      /* as they were last time, ready: OK starts */
        for (int k = 0; k < last_n; k++)
            if (S.d[i].owner == 0 && S.d[i].local == last[k].local) { S.d[i].team = last[k].team; S.d[i].ready = 1; }
    sides_ready = 1;
}

void ob_pads_sides_open(void)
{
    if (reg.set) ob_pads_sides_open_ex(reg.players, reg.names, reg.skin, reg.apply, reg.ud);
    else ob_pads_sides_open_ex(C.players, C.team_name, C.skin, NULL, NULL);
}

int ob_pads_sides_active(void) { return sides_ready && S.open; }

SDL_GameController *ob_pads_controller(int p)
{
    return p >= 0 && p < C.players && player_ctrl[p] ? ob_plat_controller(player_ctrl[p] - 1) : NULL;
}

int ob_pads_player(int slot)
{
    for (int p = 0; p < C.players; p++) if (player_ctrl[p] == slot + 1) return p + 1;
    return 0;
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

/* ---- the mouse as a pad ---- */

#define MOUSE_DEAD 0.12f

/* the pointer's distance from the middle of the window is the stick (past a small dead zone, so it can rest), the buttons are the port's */
uint32_t ob_pads_mouse_bits(const uint32_t dir[4], const uint32_t btn[3])
{
    SDL_Window *w = ob_plat_window();
    int ww = 0, wh = 0, mx = 0, my = 0;
    uint32_t b = 0, st;
    float dx, dy;
    if (!w || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & SDL_WINDOW_MOUSE_FOCUS)) return 0;
    SDL_GetWindowSize(w, &ww, &wh);
    if (ww < 2 || wh < 2) return 0;
    st = SDL_GetMouseState(&mx, &my);
    dx = (mx - ww / 2.0f) / (ww / 2.0f);
    dy = (my - wh / 2.0f) / (wh / 2.0f);
    if (dy < -MOUSE_DEAD) b |= dir[0];
    if (dy > MOUSE_DEAD) b |= dir[1];
    if (dx < -MOUSE_DEAD) b |= dir[2];
    if (dx > MOUSE_DEAD) b |= dir[3];
    if (st & SDL_BUTTON_LMASK) b |= btn[0];
    if (st & SDL_BUTTON_RMASK) b |= btn[1];
    if (st & SDL_BUTTON_MMASK) b |= btn[2];
    return b;
}

static uint32_t mouse_bits(void)
{
    uint32_t dir[4], btn[3] = { 0, 0, 0 };
    for (int i = 0; i < 4; i++) dir[i] = C.b[i].bit;
    for (int i = 0; i < 3; i++) if (C.mouse_btn[i] > 0 && C.mouse_btn[i] <= C.n) btn[i] = C.b[C.mouse_btn[i] - 1].bit;
    return ob_pads_mouse_bits(dir, btn);
}

int ob_pads_mouse_player(void) { return mouse_player; }

/* the small green cursor: where the player is being asked to skate to */
void ob_pads_mouse_draw(const SDL_Rect *dst)
{
    SDL_Renderer *ren = ob_plat_renderer();
    SDL_Window *w = ob_plat_window();
    int mx = 0, my = 0, r;
    if (mouse_player < 0 || !ren || !w || S.open || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & SDL_WINDOW_MOUSE_FOCUS)) return;
    SDL_GetMouseState(&mx, &my);
    if (mx < dst->x || my < dst->y || mx >= dst->x + dst->w || my >= dst->y + dst->h) return;
    r = dst->h / 56 + 2;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_Rect sh[2] = { { mx - r - 1, my - 2, 2 * r + 3, 5 }, { mx - 2, my - r - 1, 5, 2 * r + 3 } };
    SDL_RenderFillRects(ren, sh, 2);
    SDL_SetRenderDrawColor(ren, 64, 255, 64, 255);
    SDL_Rect g[2] = { { mx - r, my - 1, 2 * r + 1, 3 }, { mx - 1, my - r, 3, 2 * r + 1 } };
    SDL_RenderFillRects(ren, g, 2);
}

uint32_t ob_pads_read(int p)
{
    const Uint8 *down = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    SDL_GameController *c;
    if (p < 0 || p >= C.players || setup || S.open) return 0;
    if (kbd_mask & (1u << p))
        for (int i = 0; i < C.n; i++) if (key[p][i] != SDL_SCANCODE_UNKNOWN && down[key[p][i]]) b |= C.b[i].bit;
    if (p == mouse_player) b |= mouse_bits();
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
    if (sides_ready && S.open && ob_plat_overlay_open()) ob_sides_close(&S);   /* the menu opening leaves the screen as it was */
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
