/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Players' pads and the controller setup screen, generalized from the SNES port's platform/main.c
 * and platform/padsetup.c (PulseMan-derived, MIT).
 */
#include "overbar/plat_pads.h"

#ifdef OVERBAR_SDL
#include <ctype.h>
#include <math.h>
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

/* Like the PC game: the pointer is read in 320x200 units from the middle of the window; inside 50 of them nothing is pressed, outside the
 * direction is one of eight (up, up-right ... clockwise). fl is that direction (0 up, clockwise), -1 inside the dead zone. */
static float aim_acc_x, aim_acc_y;                        /* the pointer's offset from the player, in 320x200 units (mouse motion while it plays) */
static int mouse_aim(int *pdx, int *pdy)
{
    SDL_Window *w = ob_plat_window();
    int ww = 0, wh = 0, mx = 0, my = 0, dx, dy;
    double ang;
    if (pdx) *pdx = 0;
    if (pdy) *pdy = 0;
    if (!w || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS))) return -2;
    (void)ww; (void)wh; (void)mx; (void)my;
    dx = (int)aim_acc_x;                                 /* the hand's travel from the player (the mouse is captured in play) */
    dy = (int)aim_acc_y;
    if (pdx) *pdx = dx;
    if (pdy) *pdy = dy;
    if (dx * dx + dy * dy < 2500) return -1;
    ang = atan2((double)dx, (double)-dy) * 180.0 / 3.14159265358979;
    if (ang < 0) ang += 360.0;
    return (int)((ang + 22.5) / 45.0) & 7;
}

static uint32_t mouse_start;                               /* what the mouse's fourth button presses (Start) */
void ob_pads_mouse_start(uint32_t bits) { mouse_start = bits; }

uint32_t ob_pads_mouse_bits(const uint32_t dir[4], const uint32_t btn[3])
{
    static const unsigned char sect[8] = { 1, 3, 2, 6, 4, 12, 8, 9 }; /* bits: 1 up, 2 right, 4 down, 8 left */
    int fl = mouse_aim(NULL, NULL);
    uint32_t b = 0, st;
    if (fl == -2) return 0;
    if (fl >= 0) {
        unsigned m = sect[fl];
        if (m & 1) b |= dir[0];
        if (m & 4) b |= dir[1];
        if (m & 8) b |= dir[2];
        if (m & 2) b |= dir[3];
    }
    st = SDL_GetMouseState(NULL, NULL);
    if (st & SDL_BUTTON_LMASK) b |= btn[0];
    if (st & SDL_BUTTON_RMASK) b |= btn[1];
    if (st & SDL_BUTTON_MMASK) b |= btn[2];
    if (st & SDL_BUTTON_X1MASK) b |= mouse_start;
    return b;
}

/* In play the pointer steers (ob_pads_mouse_bits). Everywhere else (the game's menus, its setup screens, its pause menu) the mouse moves a
 * selection step by step: each stretch of travel (MENU_STEP pixels) up, down, left or right is one short press of that direction, the wheel is up
 * and down, the three buttons are the port's. The port says which it is with ob_pads_mouse_mode each frame. */
#define MENU_STEP 18
static int mouse_play = 1;
static int mouse_rel;                                      /* relative mouse mode is on (the pointer is only a hand then) */
static float macc_x, macc_y;
static int rel_x, rel_y, rel_have;                         /* the pointer's last place, for its travel outside play */
static uint32_t pulse_bits, pulse_off, pulse_next;
static int mouse_steps = 1;                                /* (ob_pads_mouse_steps) its travel steps the selection */

void ob_pads_mouse_mode(int play) { mouse_play = play; }
void ob_pads_mouse_steps(int on) { mouse_steps = on; }
int ob_pads_mouse_playing(void) { return mouse_play; }
int ob_pads_mouse_cursor(void) { return mouse_player >= 0 && mouse_play; }
static int wheel_acc;                                      /* wheel clicks not yet used (up is minus) */
static Uint32 move_ms;                                     /* when the pointer last moved or clicked */
static int menu_on, menu_hover = -1, menu_steps;           /* the published menu; the row under the pointer; steps taken toward it */
static ob_pad_menu menu;
static ob_menu_rect menu_rect[OB_MENU_ROWS];
static SDL_Rect menu_dst;                                  /* where the picture is in the window (from the last draw) */
static int menu_lb, menu_rb;                               /* buttons last frame, for the click edges */
static Uint32 click_off;

void ob_pads_mouse_motion(int dx, int dy)
{
    move_ms = SDL_GetTicks();
    if (mouse_play && mouse_rel) {
        SDL_Window *w = ob_plat_window();
        int ww = 0, wh = 0;
        if (w) SDL_GetWindowSize(w, &ww, &wh);
        if (ww > 1 && wh > 1) {
            aim_acc_x += dx * 320.0f / ww; aim_acc_y += dy * 200.0f / wh;
            if (aim_acc_x < -320) aim_acc_x = -320; else if (aim_acc_x > 320) aim_acc_x = 320;
            if (aim_acc_y < -200) aim_acc_y = -200; else if (aim_acc_y > 200) aim_acc_y = 200;
        }
    }
}
static unsigned press_latch;                                /* buttons pressed since the menu looked (bit 1 left, 2 right): a click too quick to show in the button state */
void ob_pads_mouse_press(int button) { if (button == SDL_BUTTON_LEFT) press_latch |= 1; else if (button == SDL_BUTTON_RIGHT) press_latch |= 2; }
void ob_pads_mouse_wheel(int y) { macc_y -= (float)y * MENU_STEP; wheel_acc -= y; }

void ob_pads_menu(const ob_pad_menu *m)
{
    if (!m || m->n <= 0 || m->n > OB_MENU_ROWS) { menu_on = 0; ob_pointer_hand_set(2, 0); return; }
    menu = *m;
    for (int i = 0; i < m->n; i++) menu_rect[i] = m->row[i];
    menu.row = menu_rect;
    menu_on = 1;
}

static void mouse_relative(int on)
{
    if (on == mouse_rel) return;
    mouse_rel = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    macc_x = macc_y = 0;
}

/* A published menu (ob_pads_menu): the pointer is a real pointer. The row under it is the one the game's selection is walked to, with short
 * presses of up / down (the game keeps its own rules: skipped lines and so on); a click on the selected row presses ok (on a row with left / right
 * options: left or right of the middle moves the option); a right click presses back; the wheel is up and down. */
static uint32_t menu_pointer(const uint32_t dir[4])
{
    SDL_Window *w = ob_plat_window();
    int mx = 0, my = 0, hit = -1;
    Uint32 now = SDL_GetTicks(), st;
    uint32_t b = 0;
    mouse_relative(0);
    macc_x = macc_y = 0;
    if (!w || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & SDL_WINDOW_MOUSE_FOCUS) || menu_dst.h <= 0) { menu_lb = menu_rb = 0; wheel_acc = 0; ob_pointer_hand_set(2, 0); return 0; }
    st = SDL_GetMouseState(&mx, &my);
    {   float pw = menu.w > 0 ? (float)menu.w : 320.0f, px = (mx - menu_dst.x) * pw / menu_dst.w, py = (my - menu_dst.y) * 224.0f / menu_dst.h;   /* (the picture fills dst, however wide the pixels are) */
        for (int i = 0; i < menu.n; i++)
            if (px >= menu.row[i].x && px < menu.row[i].x + menu.row[i].w && py >= menu.row[i].y && py < menu.row[i].y + menu.row[i].h) { hit = i; break; }
        if (hit != menu_hover) { menu_hover = hit; menu_steps = 0; }
        ob_pointer_hand_set(2, hit >= 0);                              /* a hand over a row that can be clicked */
        {   int lb = (st & SDL_BUTTON_LMASK) != 0, rb = (st & SDL_BUTTON_RMASK) != 0, pl, pr;
            static unsigned pend; static Uint32 pend_ms;           /* a click waits a moment for its turn (a step in progress) */
            if (press_latch) { pend |= press_latch; pend_ms = now; press_latch = 0; }
            if (now - pend_ms > 400u) pend = 0;
            pl = pend & 1; pr = pend & 2;
            if (pl || pr || (lb && !menu_lb) || (rb && !menu_rb)) { move_ms = now; menu_steps = 0; }
            if (now >= pulse_off) pulse_bits = 0;
            if (!pulse_bits && now >= pulse_next) {
                if (wheel_acc) { pulse_bits = wheel_acc < 0 ? dir[0] : dir[1]; wheel_acc += wheel_acc < 0 ? 1 : -1; }
                else if (hit < 0 && menu.n > 1 && !(st & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {        /* above or below the box: it scrolls */
                    int top = 1 << 30, bot = -1, left = 1 << 30, right = -1;
                    for (int i = 0; i < menu.n; i++) {
                        if (menu.row[i].y < top) top = menu.row[i].y;
                        if (menu.row[i].y + menu.row[i].h > bot) bot = menu.row[i].y + menu.row[i].h;
                        if (menu.row[i].x < left) left = menu.row[i].x;
                        if (menu.row[i].x + menu.row[i].w > right) right = menu.row[i].x + menu.row[i].w;
                    }
                    if (px >= left && px < right && (py < top || py >= bot)) pulse_bits = py < top ? dir[0] : dir[1];
                }
                if (pulse_bits) { }
                else if (hit >= 0 && hit != menu.sel && menu_steps < 12 && now - move_ms < 700u + 12u * (Uint32)(menu.step_ms > 0 ? menu.step_ms : 0)) {
                    if (menu.grid && menu.sel >= 0 && menu.sel < menu.n) {
                        const ob_menu_rect *h = &menu.row[hit], *c = &menu.row[menu.sel];
                        int dx = (h->x + h->w / 2) - (c->x + c->w / 2), dy = (h->y + h->h / 2) - (c->y + c->h / 2);
                        if (dx < -c->w / 2 || dx > c->w / 2) pulse_bits = dx < 0 ? dir[2] : dir[3];
                        else pulse_bits = dy < 0 ? dir[0] : dir[1];
                    } else pulse_bits = hit < menu.sel ? dir[0] : dir[1];
                    menu_steps++;
                } else if ((pl || (lb && !menu_lb)) && hit >= 0 && hit == menu.sel) {
                    pulse_bits = menu.ok; pend &= ~1u;
                    if (menu.lr) {
                        pulse_bits = px < menu.row[hit].x + menu.row[hit].w / 2.0f ? dir[2] : dir[3];
                    }
                } else if (pr || (rb && !menu_rb)) { pulse_bits = menu.back; pend &= ~2u; }
                if (pulse_bits) { pulse_off = now + 70; pulse_next = now + (menu.step_ms > 0 ? (Uint32)menu.step_ms : 140u); }
            }
            menu_lb = lb; menu_rb = rb;
        }
    }
    (void)click_off;
    b = pulse_bits;
    if (st & SDL_BUTTON_X1MASK) b |= mouse_start;
    return b;
}

/* the mouse player's buttons this moment: the pointer's 8 directions in play, direction steps for the menus anywhere else */
uint32_t ob_pads_mouse_read(const uint32_t dir[4], const uint32_t btn[3])
{
    uint32_t b = 0, now, st;
    SDL_Window *w = ob_plat_window();
    if (mouse_play) {                                      /* in play the mouse is captured and steers from the player */
        if (!w || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS))) { mouse_relative(0); return 0; }
        if (!mouse_rel) { aim_acc_x = aim_acc_y = 0; mouse_relative(1); }
        return ob_pads_mouse_bits(dir, btn);
    }
    if (menu_on) return menu_pointer(dir);
    mouse_relative(0);                                     /* (no capture outside play: the pointer stays free) */
    if (!w || ob_plat_overlay_open() || !(SDL_GetWindowFlags(w) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS))) { rel_have = 0; return 0; }
    {   int mx = 0, my = 0;                                /* the travel of the pointer itself steps the selection */
        SDL_GetMouseState(&mx, &my);
        if (rel_have) { macc_x += (float)(mx - rel_x); macc_y += (float)(my - rel_y); }
        rel_x = mx; rel_y = my; rel_have = 1;
    }
    now = SDL_GetTicks();
    if (now >= pulse_off) pulse_bits = 0;
    if (!mouse_steps) macc_x = macc_y = 0;                 /* (a screen any press leaves: moving the mouse is not one) */
    if (!pulse_bits && now >= pulse_next) {
        float ax = macc_x < 0 ? -macc_x : macc_x, ay = macc_y < 0 ? -macc_y : macc_y;
        if (ay >= MENU_STEP && ay >= ax) pulse_bits = macc_y < 0 ? dir[0] : dir[1];
        else if (ax >= MENU_STEP) pulse_bits = macc_x < 0 ? dir[2] : dir[3];
        if (pulse_bits) { macc_x = macc_y = 0; pulse_off = now + 70; pulse_next = now + 140; }
    }
    b = pulse_bits;
    st = SDL_GetMouseState(NULL, NULL);
    if (st & SDL_BUTTON_LMASK) b |= btn[0];
    if (st & SDL_BUTTON_RMASK) b |= btn[1];
    if (st & SDL_BUTTON_MMASK) b |= btn[2];
    if (st & SDL_BUTTON_X1MASK) b |= mouse_start;
    return b;
}

static uint32_t mouse_bits(void)
{
    uint32_t dir[4], btn[3] = { 0, 0, 0 };
    for (int i = 0; i < 4; i++) dir[i] = C.b[i].bit;
    for (int i = 0; i < C.n; i++) if (C.b[i].name && !strcmp(C.b[i].name, "start")) mouse_start = C.b[i].bit;   /* (button 4 is Start) */
    for (int i = 0; i < 3; i++) if (C.mouse_btn[i] > 0 && C.mouse_btn[i] <= C.n) btn[i] = C.b[C.mouse_btn[i] - 1].bit;
    return ob_pads_mouse_read(dir, btn);
}

int ob_pads_mouse_player(void) { return mouse_player; }
/* does player p have a device (the keyboard's keys, a controller or the mouse) after the select-sides screen? */
int ob_pads_seated(int p) { return p >= 0 && p < C.players && p < OB_PADS_MAX && ((kbd_mask >> p & 1) || player_ctrl[p] || p == mouse_player); }

/* The green arrow of the NHL 95 PC game (AimArrowFrames): it points where the player is asked to skate, drawn a quarter of the pointer's
 * distance from the player. Frames 0x2C4 up, 0x2C5 up-right, 0x2C6 right, 0x2C9 down-right, 0x2CA down (mirrored for the left side), 0x164 at rest.
 * O is the dark outline (0,64,0), G the fill (0,192,0); {origin x, origin y} is the point that sits on the spot. */
static const struct { int w, h, ox, oy; const char *row[9]; } AIM[6] = {
    { 12, 9, 4, 4, { "....O.......", "...OGO......", "..OGGGO.....", ".OGGGGGO....", "OOOOGOOOO...", "...OGO......", "...OGO......", "...OGO......", "...OOO......" } },
    {  8, 8, 4, 3, { ".OOOOOOO", "..OGGGGO", "...OGGGO", "..OOGGGO", ".OOGOOGO", "OOGOO.OO", ".OOO...O", "..O....." } },
    { 12, 9, 4, 4, { "....O.......", "....OO......", "....OGO.....", "OOOOOGGO....", "OGGGGGGGO...", "OOOOOGGO....", "....OGO.....", "....OO......", "....O......." } },
    {  8, 8, 4, 4, { "..O.....", ".OOO...O", "OOGOO.OO", ".OOGOOGO", "..OOGGGO", "...OGGGO", "..OGGGGO", ".OOOOOOO" } },
    { 12, 9, 4, 4, { "...OOO......", "...OGO......", "...OGO......", "...OGO......", "OOOOGOOOO...", ".OGGGGGO....", "..OGGGO.....", "...OGO......", "....O......." } },
    { 12, 9, 5, 4, { "....OOO.....", "....OGO.....", "....OGO.....", ".OOOOGOOOO..", ".OGGGGGGGO..", ".OOOOGOOOO..", "....OGO.....", "....OGO.....", "....OOO....." } },
};
static int aim_x, aim_y;                 /* where the player is, in picture pixels from the middle of the picture */

void ob_pads_mouse_anchor(int x, int y) { aim_x = x; aim_y = y; }

void ob_pads_mouse_draw(const SDL_Rect *dst)
{
    menu_dst = *dst;
    static const int frame[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    SDL_Renderer *ren = ob_plat_renderer();
    SDL_Window *w = ob_plat_window();
    int dx, dy, fl, f, flip, c, r;
    float sc, ax, ay;
    if (mouse_player < 0 || !mouse_play || !ren || !w || S.open || ob_plat_overlay_open()) return;
    fl = mouse_aim(&dx, &dy);
    if (getenv("OVERBAR_TEST_AIMDBG")) { static Uint32 t; if (SDL_GetTicks() - t > 1000) { t = SDL_GetTicks(); fprintf(stderr, "aim fl=%d acc=%.0f,%.0f rel=%d flags=%x\n", fl, aim_acc_x, aim_acc_y, mouse_rel, (unsigned)SDL_GetWindowFlags(w)); } }
    if (fl == -2) return;
    f = fl < 0 ? 5 : frame[fl];
    flip = fl >= 5;
    sc = dst->h / 224.0f;                                   /* the picture is 224 lines tall; sc is the size of one picture pixel */
    ax = dst->x + (dst->w / 2.0f + aim_x * sc) + (fl < 0 ? 0 : dx / 2.0f * sc);
    ay = dst->y + (dst->h / 2.0f + aim_y * sc) + (fl < 0 ? 0 : dy / 2.0f * sc);
    {   float lo_x = dst->x + 8 * sc, hi_x = dst->x + dst->w - 8 * sc, lo_y = dst->y + 8 * sc, hi_y = dst->y + dst->h - 8 * sc;   /* off the picture the arrow stays on its edge */
        if (ax < lo_x) ax = lo_x; else if (ax > hi_x) ax = hi_x;
        if (ay < lo_y) ay = lo_y; else if (ay > hi_y) ay = hi_y;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    /* outline first, fill on top: two passes over the same cells */
    {   SDL_Rect o[160], g[160]; int no = 0, ng = 0;
        for (r = 0; r < AIM[f].h; r++)
            for (c = 0; c < AIM[f].w; c++) {
                char ch = AIM[f].row[r][c];
                int px = flip ? -(c - AIM[f].ox) - 1 : c - AIM[f].ox;
                SDL_Rect q = { (int)(ax + px * sc + 0.5f), (int)(ay + (r - AIM[f].oy) * sc + 0.5f), (int)(sc + 1.0f), (int)(sc + 1.0f) };
                if (ch == 'O') o[no++] = q; else if (ch == 'G') g[ng++] = q;
            }
        SDL_SetRenderDrawColor(ren, 0, 64, 0, 255); SDL_RenderFillRects(ren, o, no);
        SDL_SetRenderDrawColor(ren, 0, 192, 0, 255); SDL_RenderFillRects(ren, g, ng);
    }
}

uint32_t ob_pads_read(int p)
{
    const Uint8 *down = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    SDL_GameController *c;
    if (p < 0 || p >= C.players || setup || S.open) return 0;
    if (kbd_mask & (1u << p)) {
        /* the select-sides screen seats the one keyboard on one side: it plays with the first player's keys wherever it sits (the second
         * player's keys, or none for a third, are for two on one keyboard) */
        unsigned m = kbd_mask & ((1u << C.players) - 1u);
        int ks = (m & (m - 1)) == 0 ? 0 : p;
        for (int i = 0; i < C.n; i++) if (key[ks][i] != SDL_SCANCODE_UNKNOWN && down[key[ks][i]]) b |= C.b[i].bit;
    }
    if (p == mouse_player || (mouse_player < 0 && !mouse_play && p == 0)) b |= mouse_bits();   /* (in the menus the mouse is pad 1 unless it plays) */
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
