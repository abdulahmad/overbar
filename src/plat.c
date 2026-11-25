/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The shared SDL2 platform layer (docs/PLATFORM.md). Lifted from the SNES C port's window code, which
 * the other ports duplicated, so one copy runs on a desktop and on a Linux handheld.
 */
#include "overbar/plat.h"
#include "overbar/sfx.h"
#include "overbar/osk.h"
#include "overbar/sides.h"
#include "overbar/chat.h"
#include "overbar/plat_pads.h"
#include "overbar/screen.h"

#ifdef OVERBAR_SDL
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#endif
#include "mdrt/menu.h"

#define OVER_W 640
#define OVER_H 480
#define FPS_W 160
#define FPS_H 28

static struct {
    ob_plat_hooks hk;
    ob_host *host;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex, *over_tex, *fps_tex, *look_tex;
    int look_w, look_h, cur_w, cur_h;
    SDL_GameController *ctrl[OB_PLAT_SLOTS];
    int stick_dir[OB_PLAT_SLOTS];
    uint8_t trigger_down[OB_PLAT_SLOTS][2];
    int text_on, cursor_hidden;
    uint32_t mouse_ms;          /* SDL_GetTicks of the last mouse move or press (the pointer hides 3 s after it, on auto) */
    uint32_t over_px[OVER_W * OVER_H];
    uint32_t fps_px[FPS_W * FPS_H];
    int over_mw, over_mh, over_x, over_y, over_w, over_h;
    int mx, my, mvalid;               /* the mouse over the overlay, in its buffer's pixels (for the edge auto-scroll) */
    int fps_frames, fps_shown, fps_all, fps_new;
    uint32_t fps_since;
    int vsync0;                    /* the renderer was made with vsync (handhelds) */
} P = { .text_on = -1, .cursor_hidden = -1 };
static ob_sides *R_sides;                    /* the select-sides screen and chat (ob_plat_room) */
static ob_chat *R_chat;
enum { IN_NONE = 0, IN_KEY, IN_MOUSE, IN_PAD };
static int last_in;                           /* last real input: a key, the mouse, or a pad (host keys do not count) */

SDL_Window *ob_plat_window(void) { return P.win; }
SDL_Renderer *ob_plat_renderer(void) { return P.ren; }
SDL_GameController *ob_plat_controller(int slot) { return slot >= 0 && slot < OB_PLAT_SLOTS ? P.ctrl[slot] : NULL; }

int ob_plat_controller_slot(SDL_JoystickID id)
{
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (P.ctrl[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(P.ctrl[i])) == id) return i;
    return -1;
}

int ob_plat_window_size(int *w, int *h)
{
    if (!P.win) return 0;
    if (!ob_filemenu_client_size(w, h)) {
        if (P.ren) SDL_GetRendererOutputSize(P.ren, w, h);
        else SDL_GetWindowSize(P.win, w, h);
    }
    return 1;
}

int ob_plat_init(ob_profile *profile)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 0;
    }
    if (profile) {
        *profile = ob_profile_detect(SDL_GetCurrentVideoDriver(), ob_handheld_env());
        ob_present_apply_hints(*profile);
    }
    return 1;
}

void ob_plat_devices_refresh(void);

static void ctrl_added(int index)
{
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (P.ctrl[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(P.ctrl[i])) == id) return;
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (!P.ctrl[i]) {
            P.ctrl[i] = SDL_GameControllerOpen(index);
            if (R_sides && P.ctrl[i]) ob_sides_add(R_sides, 1 + i, SDL_GameControllerName(P.ctrl[i]));
            if (P.hk.controller) P.hk.controller(i, 1);
            ob_plat_devices_refresh();
            break;
        }
}

/* The Input page's device list: the keyboard and mouse (a desktop always has them; SDL2 cannot ask, so on Linux and
 * handhelds they appear once the first key or mouse event comes) and every controller SDL can map, with the player it
 * plays, if any. Rebuilt when one comes or goes, and when the page is opened. */
static int seen_key, seen_mouse, dev_dirty = 1;   /* dirty: refresh on the next pump (the port sets its pads up after open) */

static int desktop(void)
{
    const char *p = SDL_GetPlatform();
    return !strcmp(p, "Windows") || !strcmp(p, "Mac OS X");
}

void ob_plat_devices_refresh(void)
{
    char name[64];
    if (!P.host) return;
    ob_host_devices_clear(P.host);
    if (desktop() || seen_key) ob_host_device_add(P.host, "Keyboard", "connected");
    if (desktop() || seen_mouse) ob_host_device_add(P.host, "Mouse", "connected");
    for (int i = 0; !P.hk.no_pads && i < OB_PLAT_SLOTS; i++) {
        char kind[24];
        int pl;
        if (!P.ctrl[i]) continue;
        pl = ob_pads_player(i);
        snprintf(kind, sizeof kind, "Controller %d", i + 1);
        if (pl) snprintf(name, sizeof name, "%.30s - player %d", SDL_GameControllerName(P.ctrl[i]) ? SDL_GameControllerName(P.ctrl[i]) : "", pl);
        else snprintf(name, sizeof name, "%.40s", SDL_GameControllerName(P.ctrl[i]) ? SDL_GameControllerName(P.ctrl[i]) : "");
        ob_host_device_add(P.host, kind, name);
    }
    if (P.host->overlay_open && P.host->overlay_page == OB_PAGE_INPUT) ob_overlay_build(P.host);
}

static void ctrl_removed(SDL_JoystickID id)
{
    int s = ob_plat_controller_slot(id);
    if (s < 0) return;
    SDL_GameControllerClose(P.ctrl[s]);
    P.ctrl[s] = NULL;
    P.stick_dir[s] = 0;
    if (R_sides) ob_sides_remove(R_sides, 1 + s);
    if (P.hk.controller) P.hk.controller(s, 0);
    ob_plat_devices_refresh();
}

static void bar_for_window(void)
{
    Uint32 f = SDL_GetWindowFlags(P.win);
    int full = !!(f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
    int handheld = P.host->profile == OB_PROFILE_HANDHELD;
    ob_filemenu_show(ob_filemenu_wanted(!full && !handheld, full, handheld));
}

#ifdef _WIN32
/* While Windows runs its own loop for the window's menu or for sizing it, this thread does not return to the main loop: the game stops
 * (the port mutes its sound for as long, modal) and, sizing, each size change is drawn here so the picture follows the window. */
static WNDPROC old_proc;
static int sizing, drawing;
static LRESULT CALLBACK plat_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (msg == WM_ENTERMENULOOP || msg == WM_ENTERSIZEMOVE) { if (P.hk.modal) P.hk.modal(1); }
    else if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE) { if (P.hk.modal) P.hk.modal(0); }
    if (msg == WM_ENTERSIZEMOVE) sizing = 1;
    else if (msg == WM_EXITSIZEMOVE) sizing = 0;
    r = CallWindowProcW(old_proc, h, msg, wp, lp);
    if (sizing && !drawing && (msg == WM_SIZE || msg == WM_PAINT)) { drawing = 1; ob_plat_present(); drawing = 0; }
    return r;
}
#endif

void ob_plat_dpi_aware(void)
{
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");   /* SDL 2.24+ (by name: older headers lack the macro) */
#ifdef _WIN32
    /* and directly, for an older SDL: per-monitor v2 (Windows 10 1703+), else system aware */
    typedef BOOL(WINAPI * ctx_fn)(HANDLE);
    HMODULE u = GetModuleHandleA("user32.dll");
    ctx_fn f = u ? (ctx_fn)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!f || !f((HANDLE)(INT_PTR)-4)) SetProcessDPIAware();
#endif
}

float ob_plat_dpi_scale(int display)
{
    float d = 0;
    if (SDL_GetDisplayDPI(display, &d, NULL, NULL) != 0 || d < 48 || d > 960) return 1;
#ifdef __APPLE__
    return 1;                                       /* macOS scales points itself */
#else
    return d / 96.0f;
#endif
}

int ob_plat_open(const ob_plat_hooks *hooks, ob_host *h)
{
    Uint32 wflags = SDL_WINDOW_RESIZABLE, rflags = SDL_RENDERER_ACCELERATED;
    SDL_RendererInfo rinfo;
    if (!hooks || !h || (!hooks->picture && !hooks->frame && !hooks->window_only)) return 0;
    P.hk = *hooks;
    P.host = h;
    h->esc_toggles = hooks->esc_opens;
    if (h->profile == OB_PROFILE_HANDHELD) {
        wflags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (h->vsync) rflags |= SDL_RENDERER_PRESENTVSYNC, P.vsync0 = 1;
    }
    if (hooks->gl) wflags |= SDL_WINDOW_OPENGL;
    P.win = SDL_CreateWindow(hooks->title ? hooks->title : "Overbar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             hooks->win_w > 0 ? hooks->win_w : 960, hooks->win_h > 0 ? hooks->win_h : 720, wflags);
    if (hooks->window_only) {                              /* the port makes its own renderer or GL context on the window and draws it */
        if (!P.win) return 0;
        goto window_ready;
    }
    P.ren = P.win ? SDL_CreateRenderer(P.win, -1, rflags) : NULL;
    if (!P.ren && P.win) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
        P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!P.ren && P.win) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        P.ren = SDL_CreateRenderer(P.win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (P.ren && SDL_GetRendererInfo(P.ren, &rinfo) == 0 && rinfo.name) {
        int gles = strstr(rinfo.name, "opengles") || strstr(rinfo.name, "gles");
        ob_host_set_present(h, gles, 0);
    }
    if (P.ren) {
        P.tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, hooks->src_w, hooks->src_h);
        P.cur_w = hooks->src_w;
        P.cur_h = hooks->src_h;
        P.fps_tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FPS_W, FPS_H);
    }
    if (!P.tex || !P.fps_tex) { fprintf(stderr, "SDL: %s (%s%s%s)\n", SDL_GetError(), P.ren ? "renderer ok" : "no renderer", P.tex ? "" : ", no picture texture", P.fps_tex ? "" : ", no fps texture"); ob_plat_close(); return 0; }
    SDL_SetTextureBlendMode(P.fps_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(P.fps_tex, SDL_ScaleModeNearest);
window_ready:
#ifdef _WIN32
    {
        SDL_SysWMinfo info;
        void *hwnd = NULL;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(P.win, &info) && info.subsystem == SDL_SYSWM_WINDOWS) hwnd = info.info.win.window;
        ob_filemenu_init(hwnd);
        if (hwnd) old_proc = (WNDPROC)SetWindowLongPtrW((HWND)hwnd, GWLP_WNDPROC, (LONG_PTR)plat_proc);
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    }
#else
    ob_filemenu_init(NULL);
#endif
    ob_filemenu_sync(h);
    bar_for_window();
    P.fps_since = SDL_GetTicks();
    for (int i = 0; !hooks->no_pads && i < SDL_NumJoysticks(); i++) ctrl_added(i);
    SDL_StopTextInput();                                  /* (SDL starts with it on on the desktop) */
    P.text_on = 0;
    return 1;
}

void ob_plat_close(void)
{
    if (P.look_tex) SDL_DestroyTexture(P.look_tex);
    if (P.over_tex) SDL_DestroyTexture(P.over_tex);
    if (P.fps_tex) SDL_DestroyTexture(P.fps_tex);
    if (P.tex) SDL_DestroyTexture(P.tex);
    if (P.ren) SDL_DestroyRenderer(P.ren);
    if (P.win) SDL_DestroyWindow(P.win);
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (P.ctrl[i]) SDL_GameControllerClose(P.ctrl[i]);
    memset(&P, 0, sizeof P);
    P.text_on = -1;
    P.cursor_hidden = -1;
    last_in = IN_NONE;
}

/* ---- text input, the cursor, the overlay's keys ---- */

static ob_osk osk;
static int osk_ready;

static void osk_key(int sc, int down, void *ud)
{
    (void)ud;
    if (R_chat && ob_chat_key(R_chat, sc, down)) return;  /* typing a chat line (or T opening one) */
    if (P.hk.key) P.hk.key(sc, down);
}
static void osk_init_once(void) { if (!osk_ready) { ob_osk_init(&osk, osk_key, NULL); osk_ready = 1; } }

/* F9: open or close, whether or not a pad was last. Overlay and the keyboard are not open together. */
static void osk_show(int on)
{
    osk_init_once();
    if (on) {
        if (P.host && P.host->overlay_open) ob_overlay_close(P.host);
        ob_osk_open(&osk);
    } else ob_osk_close(&osk);
}
static void osk_toggle(void) { osk_init_once(); osk_show(!osk.open); }

/* the port calls this when the game waits for text (a line editor). It pops only after a controller. */
void ob_plat_osk_open(void)
{
    osk_init_once();
    if (last_in != IN_PAD) return;
    if (P.host && P.host->overlay_open) return;
    ob_osk_open(&osk);
}
void ob_plat_osk_close(void) { osk_init_once(); ob_osk_close(&osk); }
int ob_plat_osk_is_open(void) { return osk_ready && osk.open; }
void ob_plat_osk_draw(uint32_t *px, int w, int h, int pitch) { if (osk_ready) ob_osk_draw(&osk, px, w, h, pitch); }

static void note_input(const SDL_Event *e)
{
    int v;
    switch (e->type) {
    case SDL_KEYDOWN:
        if (!seen_key) { seen_key = 1; dev_dirty = 1; }
        if (e->key.repeat) return;
        if (ob_map_key(e->key.keysym.scancode) != OB_HOSTKEY_NONE) return;  /* F9 / F12 / ` */
        last_in = IN_KEY;
        return;
    case SDL_MOUSEMOTION:
        if (!seen_mouse) { seen_mouse = 1; dev_dirty = 1; }
        if (e->motion.xrel || e->motion.yrel) { last_in = IN_MOUSE; P.mouse_ms = SDL_GetTicks() | 1u; }
        return;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEWHEEL:
        if (!seen_mouse) { seen_mouse = 1; dev_dirty = 1; }
        last_in = IN_MOUSE;
        P.mouse_ms = SDL_GetTicks() | 1u;
        return;
    case SDL_CONTROLLERBUTTONDOWN:
        last_in = IN_PAD;
        return;
    case SDL_CONTROLLERAXISMOTION:
        v = e->caxis.value;
        if (v > 16384 || v < -16384) last_in = IN_PAD;
        return;
    case SDL_JOYBUTTONDOWN:
        last_in = IN_PAD;
        return;
    case SDL_JOYHATMOTION:
        if (e->jhat.value != SDL_HAT_CENTERED) last_in = IN_PAD;
        return;
    case SDL_JOYAXISMOTION:
        v = e->jaxis.value;
        if (v > 16384 || v < -16384) last_in = IN_PAD;
        return;
    default:
        return;
    }
}

/* the keyboard takes the d-pad, A / Enter and B / Esc while it is open (its own copy of the event: 1 when used) */
static int osk_event(const SDL_Event *e)
{
    int in = -1;
    if (!ob_plat_osk_is_open()) return 0;
    if (P.host->overlay_open) { ob_osk_close(&osk); return 0; }   /* the overlay and the keyboard are not open together */
    if (e->type == SDL_CONTROLLERBUTTONDOWN && !P.hk.no_pads) {
        int b = e->cbutton.button;
        in = b == SDL_CONTROLLER_BUTTON_DPAD_UP ? MDRT_MENU_UP : b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? MDRT_MENU_DOWN
           : b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? MDRT_MENU_LEFT : b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? MDRT_MENU_RIGHT
           : b == SDL_CONTROLLER_BUTTON_A ? MDRT_MENU_OK : b == SDL_CONTROLLER_BUTTON_B ? MDRT_MENU_BACK
           : b == SDL_CONTROLLER_BUTTON_START ? -2 : -1;
        if (in == -2) { ob_osk_close(&osk); return 1; }
        if (in >= 0) { ob_osk_input(&osk, in); return 1; }
        return e->type != 0;                               /* other pad buttons do not reach the game meanwhile */
    }
    if (e->type == SDL_CONTROLLERBUTTONUP || e->type == SDL_CONTROLLERAXISMOTION) return !P.hk.no_pads;
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {  /* arrows, Enter and Esc drive it (a real keyboard types the letters itself) */
        int k = e->key.keysym.scancode;
        in = k == SDL_SCANCODE_UP ? MDRT_MENU_UP : k == SDL_SCANCODE_DOWN ? MDRT_MENU_DOWN : k == SDL_SCANCODE_LEFT ? MDRT_MENU_LEFT
           : k == SDL_SCANCODE_RIGHT ? MDRT_MENU_RIGHT : k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER ? MDRT_MENU_OK
           : k == SDL_SCANCODE_ESCAPE ? MDRT_MENU_BACK : -1;
        if (in < 0) return 0;
        if (e->type == SDL_KEYDOWN && !e->key.repeat) ob_osk_input(&osk, in);
        return 1;
    }
    return 0;
}


/* ---- the select-sides screen and chat ---- */

int ob_plat_overlay_open(void) { return P.host && P.host->overlay_open; }
void ob_plat_overlay_close(void) { if (P.host && P.host->overlay_open) ob_overlay_close(P.host); }

void ob_plat_room(ob_sides *sides, ob_chat *chat) { R_sides = sides; R_chat = chat; }

void ob_plat_sides_devices(ob_sides *sides)
{
    int i;
    if (!sides) return;
    ob_sides_add(sides, 0, "Keyboard");
    if (desktop() || seen_mouse || getenv("OVERBAR_TEST_FAKEPAD")) ob_sides_add(sides, 100, "Mouse");
    if (getenv("OVERBAR_TEST_FAKEPAD")) ob_sides_add(sides, 50, "Xbox Controller");   /* (a test's stand-in for a pad) */
    if (!P.hk.no_pads)
        for (i = 0; i < OB_PLAT_SLOTS; i++)
            if (P.ctrl[i]) ob_sides_add(sides, 1 + i, SDL_GameControllerName(P.ctrl[i]));
}

void ob_plat_room_draw(uint32_t *px, int w, int h, int pitch)
{
    if (R_sides) ob_sides_draw(R_sides, px, w, h, pitch);
    if (R_chat) ob_chat_draw(R_chat, px, w, h, pitch);
}

static int sides_drag;                                       /* the left button is down on the select-sides screen */
static int grab_local = -1;                                  /* the device taken by the mouse on that screen (its local id), -1 none */
static int sd_ox, sd_oy, sd_t = 1, sd_mw, sd_mh, sd_tw, sd_th;             /* where the screen was drawn: the buffer's place in the window, its scale and size */
static int sides_device_under(int wx, int wy)
{
    if (!R_sides || !R_sides->open || sd_tw < 1 || sd_th < 1 || sd_mw < 1) return -1;
    if (wx < sd_ox || wy < sd_oy || wx >= sd_ox + sd_tw || wy >= sd_oy + sd_th) return -1;
    return ob_sides_device_at(R_sides, sd_mw, sd_mh, (wx - sd_ox) * sd_mw / sd_tw, (wy - sd_oy) * sd_mh / sd_th);
}

/* 1 when the screen or chat used the event */
static int room_event(const SDL_Event *e)
{
    int down = e->type == SDL_KEYDOWN;
    if (P.host->overlay_open) return 0;
    if (R_chat && R_chat->typing && (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP))
        return ob_chat_key(R_chat, e->key.keysym.scancode, down);
    if (R_sides && R_sides->open) {
        int in = -1;
        if (e->type == SDL_CONTROLLERBUTTONDOWN && !P.hk.no_pads) {
            int b = e->cbutton.button, slot = ob_plat_controller_slot(e->cbutton.which);
            in = b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? MDRT_MENU_LEFT : b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? MDRT_MENU_RIGHT
               : b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START ? MDRT_MENU_OK
               : b == SDL_CONTROLLER_BUTTON_B ? MDRT_MENU_BACK : -1;
            if (in < 0 || slot < 0) return 1;
            return ob_sides_input(R_sides, 1 + slot, in);
        }
        if (e->type == SDL_MOUSEMOTION) {                           /* a hand over a device that can be taken */
            int wx = e->motion.x, wy = e->motion.y, dv = sides_device_under(wx, wy);
            ob_pointer_hand_set(2, grab_local >= 0 || (dv >= 0 && R_sides->d[dv].owner == R_sides->me));
        }
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {   /* take any device of this machine, the mouse's own too */
            int dv = sides_device_under(e->button.x, e->button.y);
            if (dv >= 0 && R_sides->d[dv].owner == R_sides->me) { grab_local = R_sides->d[dv].local; sides_drag = 1; return 1; }
        }
        if (grab_local >= 0 && e->type == SDL_MOUSEMOTION) {         /* dragging it: one section at a time as the pointer crosses the thirds */
            int ww = 0, wh = 0, gi = ob_sides_find(R_sides, R_sides->me, grab_local);
            SDL_GetWindowSize(P.win, &ww, &wh);
            if (!(e->motion.state & SDL_BUTTON_LMASK) || gi < 0 || ww <= 0) { grab_local = -1; sides_drag = 0; return 1; }
            {   int px = e->motion.x, tm = R_sides->d[gi].team, want = px * 3 < ww ? 0 : px * 3 > ww * 2 ? 2 : 1;
                int cur = R_sides->g.teams == 1 ? (tm == OB_SIDES_MIDDLE ? 0 : 1) : tm == OB_SIDES_MIDDLE ? 1 : tm == 0 ? 0 : 2;
                if (R_sides->g.teams == 1) want = want > 0;
                if (want != cur) ob_sides_input(R_sides, grab_local, want < cur ? MDRT_MENU_LEFT : MDRT_MENU_RIGHT);
            }
            return 1;
        }
        if (e->type == SDL_MOUSEBUTTONUP && grab_local >= 0) { grab_local = -1; sides_drag = 0; return 1; }
        if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEMOTION) && ob_sides_find(R_sides, 0, 100) >= 0) {
            /* the mouse: hold the left button and drag across the screen to move its device (a third of the width each: away, middle, home);
             * a left click where the device already is says ready; the Start button (the mouse's button 4) begins; right click is Back */
            int ww = 0, wh = 0, d = ob_sides_find(R_sides, 0, 100), px, step = 0;
            SDL_GetWindowSize(P.win, &ww, &wh);
            if (ww <= 0) return 1;
            if (e->type == SDL_MOUSEBUTTONDOWN) {
                px = e->button.x;
                if (e->button.button == SDL_BUTTON_RIGHT) return ob_sides_input(R_sides, 100, MDRT_MENU_BACK);
                if (e->button.button == SDL_BUTTON_X1) return R_sides->d[d].ready ? ob_sides_input(R_sides, 100, MDRT_MENU_OK) : 1;
                if (e->button.button != SDL_BUTTON_LEFT) return 1;
                sides_drag = 1;
                step = 2;                                           /* (a click: move there, or ready when already there) */
            } else {
                if (!sides_drag || !(e->motion.state & SDL_BUTTON_LMASK)) { sides_drag = 0; return 1; }
                px = e->motion.x;
                step = 1;                                           /* (a drag: move only) */
            }
            {
                int want = px * 3 < ww ? 0 : px * 3 > ww * 2 ? 2 : 1, tm = R_sides->d[d].team;
                int cur = R_sides->g.teams == 1 ? (tm == OB_SIDES_MIDDLE ? 0 : 1) : tm == OB_SIDES_MIDDLE ? 1 : tm == 0 ? 0 : 2;
                if (R_sides->g.teams == 1) want = want > 0;
                if (want != cur) return ob_sides_input(R_sides, 100, want < cur ? MDRT_MENU_LEFT : MDRT_MENU_RIGHT);
                if (step == 2 && tm != OB_SIDES_MIDDLE && !R_sides->d[d].ready) return ob_sides_input(R_sides, 100, MDRT_MENU_OK);
            }
            return 1;
        }
        if (e->type == SDL_MOUSEBUTTONUP) sides_drag = 0;
        if ((e->type == SDL_CONTROLLERBUTTONUP || e->type == SDL_CONTROLLERAXISMOTION) && !P.hk.no_pads) return 1;
        if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
            int k = e->key.keysym.scancode;
            in = k == SDL_SCANCODE_LEFT ? MDRT_MENU_LEFT : k == SDL_SCANCODE_RIGHT ? MDRT_MENU_RIGHT
               : k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER ? MDRT_MENU_OK : k == SDL_SCANCODE_ESCAPE ? MDRT_MENU_BACK : -1;
            if (in >= 0) {
                if (!down) return 1;
                if (e->key.repeat) return 1;
                return ob_sides_input(R_sides, 0, in);   /* 0: Esc with the keyboard in the middle, the port's */
            }
        }
    }
    if (R_chat && (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)))
        return ob_chat_key(R_chat, e->key.keysym.scancode, down);
    return 0;
}

static void sync_text_input(void)
{
    int want = P.host->overlay_open && ob_overlay_editing(P.host);
    if (want == P.text_on) return;
    if (want) SDL_StartTextInput();
    else SDL_StopTextInput();
    P.text_on = want;
}

static void cursor_update(void)
{
    if (P.hk.window_only) return;                         /* the port's: it knows when the game draws a pointer of its own */
    /* a full-window screen (the box) keeps the pointer; on auto the pointer shows while it moves and hides after 3 s of nothing */
    int moving = P.mouse_ms && (uint32_t)(SDL_GetTicks() - P.mouse_ms) < 3000u;
    int hide = !(P.host->overlay_open || ob_screen_up() || (P.hk.captures && P.hk.captures()) || P.host->pointer == 1 || (P.host->pointer == 0 && moving));
    if (P.host->pointer == 2 && !P.host->overlay_open) hide = 1;
    if (ob_pads_mouse_cursor() && !P.host->overlay_open && !ob_pads_sides_active()) hide = 1;   /* the green cursor is the pointer then */
    if (hide != P.cursor_hidden) { SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE); P.cursor_hidden = hide; }
}

/* the arrow, or a hand while the mouse is over something clickable in the overlay or in a full-window screen (the box) */
static void cursor_shape(void)
{
    static SDL_Cursor *hand, *arrow;
    static int is = -1;
    if (!(P.host && P.host->overlay_open)) ob_pointer_hand_set(0, 0);       /* nothing is under the mouse in a closed overlay */
    int want = (P.host && P.host->overlay_open && ob_pointer_hand(0)) || (!(P.host && P.host->overlay_open) && ((ob_screen_up() && ob_pointer_hand(1)) || ob_pointer_hand(2)));
    if (want == is) return;
    if (!hand) hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    if (!arrow) arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    if (want ? hand : arrow) SDL_SetCursor(want ? hand : arrow);
    is = want;
}

static int overlay_key_to_input(SDL_Scancode k)
{
    if (k == SDL_SCANCODE_UP) return MDRT_MENU_UP;
    if (k == SDL_SCANCODE_DOWN) return MDRT_MENU_DOWN;
    if (k == SDL_SCANCODE_LEFT) return MDRT_MENU_LEFT;
    if (k == SDL_SCANCODE_RIGHT) return MDRT_MENU_RIGHT;
    if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) return MDRT_MENU_OK;
    if (k == SDL_SCANCODE_BACKSPACE) return MDRT_MENU_BACK;   /* Back is Backspace; Esc closes the overlay (and opens it on the consoles) */
    return -1;
}

static int editing_key_to_input(SDL_Scancode k)
{
    if (k == SDL_SCANCODE_BACKSPACE) return MDRT_MENU_LEFT;
    if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) return MDRT_MENU_OK;
    if (k == SDL_SCANCODE_ESCAPE) return MDRT_MENU_BACK;
    return -1;
}

static void do_quit(void)
{
    if (P.hk.quit_flag) { *P.hk.quit_flag = 1; return; }  /* the port leaves by itself, from its own loop */
    if (P.hk.quit) P.hk.quit();
    ob_plat_close();
    SDL_Quit();
    exit(0);
}

static void after_change(void)
{
    if (P.hk.changed) P.hk.changed();
    ob_filemenu_sync(P.host);
    sync_text_input();
    if (P.host->request_quit) do_quit();
}

/* the overlay's navigation sounds: a move only when the selection moved, in for a choice, back for Back */
static void menu_sound(int kind) { if (P.host->menu_sounds && !P.host->mute) ob_sfx_play(kind, P.host->volume); }

static void overlay_handle(int mdrt_input)
{
    int was = P.host->overlay.sel, r;
    r = ob_overlay_input(P.host, mdrt_input);
    if (mdrt_input == MDRT_MENU_BACK) menu_sound(OB_SFX_BACK);
    else if (mdrt_input == MDRT_MENU_OK) menu_sound(OB_SFX_SELECT);
    else if (P.host->overlay.sel != was || mdrt_input == MDRT_MENU_LEFT || mdrt_input == MDRT_MENU_RIGHT) menu_sound(OB_SFX_MOVE);
    if (r == OB_ID_CONTROLLERS && P.hk.action) P.hk.action(r);
    if (r == OB_ID_SIDES) ob_pads_sides_open();
    after_change();
}

static void pad_back(void)
{
    if (P.host->overlay_open) ob_overlay_close(P.host);
    else {
        osk_init_once();
        ob_osk_close(&osk);
        ob_overlay_open(P.host);
    }
}

static void overlay_mouse(int wx, int wy, int what)
{
    int ww, wh, winw, winh, r, was;
    if (!P.host->overlay_open || what < 0 || !ob_plat_window_size(&ww, &wh)) { P.mvalid = 0; return; }
    SDL_GetWindowSize(P.win, &winw, &winh);
    if (winw > 0 && winh > 0 && (ww != winw || wh != winh)) { wx = wx * ww / winw; wy = wy * wh / winh; }
    if (P.over_w <= 0 || P.over_h <= 0 || wx < P.over_x || wy < P.over_y || wx >= P.over_x + P.over_w || wy >= P.over_y + P.over_h) { P.mvalid = 0; return; }
    P.mx = (wx - P.over_x) * P.over_mw / P.over_w;
    P.my = (wy - P.over_y) * P.over_mh / P.over_h;
    P.mvalid = 1;
    was = P.host->overlay.sel;
    r = ob_overlay_pointer(P.host, (wx - P.over_x) * P.over_mw / P.over_w, (wy - P.over_y) * P.over_mh / P.over_h,
                           P.over_mw, P.over_mh, what);
    if (what == 0) { if (P.host->overlay.sel != was) menu_sound(OB_SFX_MOVE); return; }
    menu_sound(what == 2 ? OB_SFX_BACK : what == 1 ? OB_SFX_SELECT : OB_SFX_MOVE);
    if (r == OB_ID_CONTROLLERS && P.hk.action) P.hk.action(r);
    if (r == OB_ID_SIDES) ob_pads_sides_open();
    after_change();
}

/* does the layer use the event itself (the overlay, its keys, the bar, text input), so the game does not get it? */
static int event_used(const SDL_Event *e, int captured)
{
    int open = P.host->overlay_open || captured;
    switch (e->type) {
    case SDL_QUIT: case SDL_TEXTINPUT: case SDL_SYSWMEVENT: return 1;
    case SDL_KEYDOWN: case SDL_KEYUP: {
        int k = e->key.keysym.scancode;
        return open || (P.hk.esc_opens && k == SDL_SCANCODE_ESCAPE) || k == SDL_SCANCODE_GRAVE || k == SDL_SCANCODE_F9 || k == SDL_SCANCODE_F12;
    }
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL: return P.host->overlay_open && !P.hk.window_only;
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: return open && !P.hk.no_pads;
    default: return 0;                                    /* controllers coming and going, window events, joystick: the game's too */
    }
}

/* ---- test hooks (no real input needed): OVERBAR_TEST_KEYS="3000:alt+return 6000:f9 7000:down" pushes those key presses at those times (ms after the
 * first pump) as SDL events, so they take the same road as real keys; each is followed by a "TEST" line on stderr (and in OVERBAR_TEST_LOG) with the
 * window's place, size, fullscreen, overlay and keyboard state. OVERBAR_TEST_DUMP=dir (windows the layer presents itself) writes the composed
 * window as a PPM about every 500 ms and after each key. ---- */
static struct { uint32_t at; int sc, mod, done, down; uint32_t up_at; } tk[64];
static int tk_n, tk_init;
static uint32_t tk_t0;

static void test_log(const char *what)
{
    int x = 0, y = 0, w = 0, h = 0;
    const char *lp = getenv("OVERBAR_TEST_LOG");
    char line[256];
    if (P.win) { SDL_GetWindowPosition(P.win, &x, &y); SDL_GetWindowSize(P.win, &w, &h); }
    snprintf(line, sizeof line, "TEST t=%u %s window=%d,%d,%d,%d full=%d overlay=%d osk=%d\n", (unsigned)(SDL_GetTicks() - tk_t0), what, x, y, w, h,
             P.win ? !!(SDL_GetWindowFlags(P.win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) : 0, P.host ? P.host->overlay_open : 0,
             ob_plat_osk_is_open());
    fputs(line, stderr);
    if (lp) { FILE *f = fopen(lp, "ab"); if (f) { fputs(line, f); fclose(f); } }
}

static void test_parse(void)
{
    const char *s = getenv("OVERBAR_TEST_KEYS");
    tk_init = 1;
    tk_t0 = SDL_GetTicks();
    while (s && *s && tk_n < 64) {
        char name[48];
        int n = 0;
        unsigned at = (unsigned)strtoul(s, (char **)&s, 10);
        if (*s != ':') break;
        s++;
        while (*s && *s != ' ' && n < 47) name[n++] = *s++;
        name[n] = 0;
        while (*s == ' ') s++;
        int mod = 0, sc = 0;
        char *tok = name, *plus;
        for (;;) {
            plus = strchr(tok, '+');
            if (plus) *plus = 0;
            if (!SDL_strcasecmp(tok, "alt")) mod |= KMOD_LALT;
            else if (!SDL_strcasecmp(tok, "ctrl")) mod |= KMOD_LCTRL;
            else if (!SDL_strcasecmp(tok, "shift")) mod |= KMOD_LSHIFT;
            else sc = SDL_GetScancodeFromName(tok);
            if (!plus) break;
            tok = plus + 1;
        }
        if (sc) { tk[tk_n].at = at; tk[tk_n].sc = sc; tk[tk_n].mod = mod; tk_n++; }
    }
}

static void test_dump(void)
{
    static uint32_t last;
    static int n;
    const char *dir = getenv("OVERBAR_TEST_DUMP");
    int w, h;
    uint32_t now = SDL_GetTicks();
    if (!dir || !P.ren || now - last < 500) return;
    last = now;
    SDL_GetRendererOutputSize(P.ren, &w, &h);
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return;
    if (SDL_RenderReadPixels(P.ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
        char name[600];
        FILE *f;
        snprintf(name, sizeof name, "%s/win_%05d_%07ums.ppm", dir, n++, (unsigned)(now - tk_t0));
        if ((f = fopen(name, "wb"))) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; i++) { unsigned char c[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(c, 1, 3, f); }
            fclose(f);
        }
    }
    free(px);
}

static void test_keys(void)
{
    uint32_t now;
    if (!tk_init) test_parse();
    now = SDL_GetTicks() - tk_t0;
    {   /* OVERBAR_TEST_SIDES=ms: opens the select-sides screen then */
        static int sides_done;
        const char *at = getenv("OVERBAR_TEST_SIDES");
        if (at && !sides_done && now >= (uint32_t)atoi(at)) { sides_done = 1; ob_pads_sides_open();
            if (getenv("OVERBAR_TEST_SEATS") && R_sides) {          /* a test's seating: the keyboard away, the pad and the mouse home */
                ob_sides_input(R_sides, 0, MDRT_MENU_LEFT);
                ob_sides_input(R_sides, 50, MDRT_MENU_RIGHT);
                ob_sides_input(R_sides, 100, MDRT_MENU_RIGHT);
            }
        }
    }
    for (int i = 0; i < tk_n; i++) {
        SDL_Event e;
        if (!tk[i].done && now >= tk[i].at) {
            SDL_zero(e);
            e.type = SDL_KEYDOWN;
            e.key.keysym.scancode = (SDL_Scancode)tk[i].sc;
            e.key.keysym.sym = SDL_GetKeyFromScancode((SDL_Scancode)tk[i].sc);
            e.key.keysym.mod = (Uint16)tk[i].mod;
            e.key.state = SDL_PRESSED;
            SDL_PushEvent(&e);
            tk[i].done = 1;
            tk[i].up_at = now + 100;
        } else if (tk[i].done == 1 && now >= tk[i].up_at) {
            SDL_zero(e);
            e.type = SDL_KEYUP;
            e.key.keysym.scancode = (SDL_Scancode)tk[i].sc;
            e.key.keysym.sym = SDL_GetKeyFromScancode((SDL_Scancode)tk[i].sc);
            e.key.keysym.mod = (Uint16)tk[i].mod;
            SDL_PushEvent(&e);
            tk[i].done = 2;
            test_log(SDL_GetScancodeName((SDL_Scancode)tk[i].sc));
        }
    }
}


/* a full-window screen (screen.h) is up and the overlay closed: it takes the keys, the mouse and the controllers' menu
 * buttons. The overlay's keys, Alt+Enter and closing the window still work. */
static int screen_event(const SDL_Event *e)
{
    ob_screen_ev ev;
    if (!ob_screen_up() || P.host->overlay_open || ob_plat_osk_is_open()) return 0;
    memset(&ev, 0, sizeof ev);
    switch (e->type) {
    case SDL_KEYDOWN: {
        SDL_Scancode k = e->key.keysym.scancode;
        if (ob_map_key(k) != OB_HOSTKEY_NONE || ((e->key.keysym.mod & KMOD_ALT) && k == SDL_SCANCODE_RETURN)) return 0;
        ev.kind = k == SDL_SCANCODE_UP ? OB_SCREEN_UP : k == SDL_SCANCODE_DOWN ? OB_SCREEN_DOWN : k == SDL_SCANCODE_LEFT ? OB_SCREEN_LEFT :
                  k == SDL_SCANCODE_RIGHT ? OB_SCREEN_RIGHT : (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE) ? OB_SCREEN_OK :
                  (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE) ? OB_SCREEN_BACK : 0;
        break;
    }
    case SDL_KEYUP: case SDL_TEXTINPUT: return 1;
    case SDL_MOUSEMOTION: ev.kind = OB_SCREEN_MOVE; ev.x = e->motion.x; ev.y = e->motion.y; break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_MIDDLE) ev.kind = e->type == SDL_MOUSEBUTTONDOWN ? OB_SCREEN_MPRESS : OB_SCREEN_MRELEASE;
        else if (e->button.button == SDL_BUTTON_LEFT) ev.kind = e->type == SDL_MOUSEBUTTONDOWN ? OB_SCREEN_PRESS : OB_SCREEN_RELEASE;
        else return 1;
        ev.x = e->button.x; ev.y = e->button.y; break;
    case SDL_MOUSEWHEEL: {
        /* a mouse wheel turns in whole notches; a touchpad's two-finger scroll comes in fractions (and sideways), and its pinch as
           Ctrl + scroll (Windows). Once a touchpad scroll is going, all of it pans (a whole step now and then is not a wheel) */
        static uint32_t pan_at;
        float px = e->wheel.preciseX, py = e->wheel.preciseY;
        int ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
#ifdef _WIN32
        if (GetKeyState(VK_CONTROL) & 0x8000) ctrl = 1;
#endif
        if (px == 0 && py == 0) return 1;
        uint32_t now = SDL_GetTicks();
        int frac = px != 0 || fabsf(py - roundf(py)) > 1e-3f;
        if (ctrl) { ev.kind = OB_SCREEN_ZOOM; ev.amount = py * 0.055f; ev.x = (int)(ev.amount * 1000); }
        else if (frac || now - pan_at < 300) { ev.kind = OB_SCREEN_PAN; ev.dx = px; ev.dy = py; pan_at = now; }
        else { ev.kind = OB_SCREEN_WHEEL; ev.y = py > 0 ? 1 : -1; }
        break;
    }
    case SDL_MULTIGESTURE:
        if (e->mgesture.numFingers < 2) return 1;
        ev.kind = OB_SCREEN_ZOOM; ev.amount = e->mgesture.dDist; ev.x = (int)(ev.amount * 1000);
        if (ev.amount == 0) return 1;
        break;
    case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION: return 1;
    case SDL_CONTROLLERBUTTONDOWN: {
        int b = e->cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_BACK || b == SDL_CONTROLLER_BUTTON_GUIDE) return 0;
        ev.pad = 1;
        ev.kind = b == SDL_CONTROLLER_BUTTON_DPAD_UP ? OB_SCREEN_UP : b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? OB_SCREEN_DOWN : b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? OB_SCREEN_LEFT :
                  b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? OB_SCREEN_RIGHT : (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START) ? OB_SCREEN_OK :
                  b == SDL_CONTROLLER_BUTTON_B ? OB_SCREEN_BACK : 0;
        break;
    }
    case SDL_CONTROLLERBUTTONUP: case SDL_CONTROLLERAXISMOTION: case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP: case SDL_JOYAXISMOTION: case SDL_JOYHATMOTION: return 1;
    default: return 0;
    }
    if (ev.kind) {
        if (e->type == SDL_MOUSEMOTION || e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
            int ww = 0, wh = 0, sw = 0, sh = 0;                     /* window points to the pixels draw() gets */
            SDL_GetWindowSize(P.win, &sw, &sh);
            ob_plat_window_size(&ww, &wh);
            if (sw > 0 && sh > 0 && ww > 0 && wh > 0) { ev.x = ev.x * ww / sw; ev.y = ev.y * wh / sh; }
        }
        ob_screen_input(&ev);
    }
    return 1;
}

void ob_plat_pump(void)
{
    SDL_Event e;
    int captured, pass;
    cursor_update();
    cursor_shape();
    if (dev_dirty) { dev_dirty = 0; ob_plat_devices_refresh(); }
    if (getenv("OVERBAR_TEST_KEYS") || getenv("OVERBAR_TEST_SIDES")) test_keys();
    while (SDL_PollEvent(&e)) {
        note_input(&e);
        if (e.type == SDL_MOUSEMOTION) ob_pads_mouse_motion(e.motion.xrel, e.motion.yrel);
        else if (e.type == SDL_MOUSEWHEEL) ob_pads_mouse_wheel(e.wheel.y);
        else if (e.type == SDL_MOUSEBUTTONDOWN) ob_pads_mouse_press(e.button.button);
        if (P.hk.event && P.hk.event(&e)) continue;
        if (osk_event(&e)) continue;
        if (room_event(&e)) continue;
        if (screen_event(&e)) continue;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
            static int win_w, win_h, win_x, win_y;            /* Alt+Enter; the windowed size and place, which the bar's coming back would move */
            int full = !!(SDL_GetWindowFlags(P.win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
            if (!full) { SDL_GetWindowSize(P.win, &win_w, &win_h); SDL_GetWindowPosition(P.win, &win_x, &win_y); }
            SDL_SetWindowFullscreen(P.win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            if (full && win_w > 0) { bar_for_window(); SDL_SetWindowSize(P.win, win_w, win_h); SDL_SetWindowPosition(P.win, win_x, win_y); }
            continue;
        }
        captured = P.hk.captures && P.hk.captures();
        if (P.hk.unused && !event_used(&e, captured)) pass = 1;
        else pass = 0;
        /* (closing the window quits even while another, hidden one is open: the box's GPU context has one, and SDL only sends
           SDL_QUIT when the last window closes) */
        if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) do_quit();
        else if (e.type == SDL_TEXTINPUT) {
            if (P.host->overlay_open && ob_overlay_editing(P.host)) ob_overlay_text(P.host, e.text.text);
        } else if (e.type == SDL_KEYDOWN && P.host->overlay_open && ob_overlay_editing(P.host)) {
            SDL_Scancode k = e.key.keysym.scancode;
            if (!e.key.repeat && ob_map_key(k) == OB_HOSTKEY_OSK) {
                osk_toggle();
                after_change();
            } else if (k == SDL_SCANCODE_V && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) {
                char *clip = SDL_GetClipboardText();
                if (clip) { ob_overlay_text(P.host, clip); SDL_free(clip); }
            } else {
                int in = editing_key_to_input(k);
                if (in >= 0) overlay_handle(in);
            }
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int sc = e.key.keysym.scancode;
            if (captured) { if (P.hk.raw_key) P.hk.raw_key(sc); }
            else if (ob_overlay_toggle_key(P.host, sc) || ob_stats_toggle_key(P.host, sc) ||
                     (sc == SDL_SCANCODE_ESCAPE && (P.hk.esc_opens || P.host->overlay_open) && (P.host->overlay_open ? (ob_overlay_close(P.host), menu_sound(OB_SFX_BACK)) : ob_overlay_open(P.host), 1))) {
                if (P.host->overlay_open) { osk_init_once(); ob_osk_close(&osk); }
                after_change();
                P.fps_frames = P.fps_all = 0;
                P.fps_since = SDL_GetTicks();
            } else if (ob_map_key(sc) == OB_HOSTKEY_OSK) {
                osk_toggle();
                after_change();
            } else if (P.host->overlay_open) {
                int in = overlay_key_to_input(sc);
                if (in >= 0) overlay_handle(in);
            }
            sync_text_input();
        } else if (!P.hk.no_pads && e.type == SDL_CONTROLLERBUTTONDOWN && (captured || P.host->overlay_open)) {
            int slot = ob_plat_controller_slot(e.cbutton.which), b = e.cbutton.button;
            if (slot >= 0 && captured) { if (P.hk.raw_button) P.hk.raw_button(slot, b); }
            else if (slot >= 0) {
                if (b == SDL_CONTROLLER_BUTTON_BACK) { pad_back(); sync_text_input(); }
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) overlay_handle(MDRT_MENU_UP);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) overlay_handle(MDRT_MENU_DOWN);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) overlay_handle(MDRT_MENU_LEFT);
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) overlay_handle(MDRT_MENU_RIGHT);
                else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START) overlay_handle(MDRT_MENU_OK);
            }
        } else if (!P.hk.no_pads && e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            pad_back();
            sync_text_input();
        } else if (!P.hk.no_pads && e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int slot = ob_plat_controller_slot(e.caxis.which);
            if (slot >= 0) {
                int v = e.caxis.value;
                int d = v < -16384 ? -1 : v > 16384 ? 1 : v > -8192 && v < 8192 ? 0 : P.stick_dir[slot];
                if (d && d != P.stick_dir[slot] && P.host->overlay_open && !captured) overlay_handle(d < 0 ? MDRT_MENU_UP : MDRT_MENU_DOWN);
                P.stick_dir[slot] = d;
            }
        } else if (!P.hk.no_pads && e.type == SDL_CONTROLLERAXISMOTION &&
                   (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
            int slot = ob_plat_controller_slot(e.caxis.which), k = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            if (slot >= 0 && e.caxis.value > 16384 && !P.trigger_down[slot][k]) {
                P.trigger_down[slot][k] = 1;
                if (captured && P.hk.raw_button) P.hk.raw_button(slot, k ? -2 : -1);
            } else if (slot >= 0 && e.caxis.value < 8192) P.trigger_down[slot][k] = 0;
        } else if (!P.hk.no_pads && e.type == SDL_CONTROLLERDEVICEADDED) ctrl_added(e.cdevice.which);
        else if (!P.hk.no_pads && e.type == SDL_CONTROLLERDEVICEREMOVED) ctrl_removed(e.cdevice.which);
        else if (e.type == SDL_SYSWMEVENT && e.syswm.msg) {
#ifdef _WIN32
            SDL_SysWMmsg *m = (SDL_SysWMmsg *)e.syswm.msg;
            if (m->subsystem == SDL_SYSWM_WINDOWS) {
                ob_wm wm;
                wm.msg = m->msg.win.msg;
                wm.wparam = (uintptr_t)m->msg.win.wParam;
                ob_filemenu_event(&wm);
            }
#endif
            sync_text_input();
            if (P.host->request_quit) do_quit();
        } else if (!P.hk.window_only && P.host->overlay_open && e.type == SDL_MOUSEMOTION) overlay_mouse(e.motion.x, e.motion.y, 0);
        else if (!P.hk.window_only && P.host->overlay_open && e.type == SDL_MOUSEBUTTONDOWN)
            overlay_mouse(e.button.x, e.button.y, e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : -1);
        else if (!P.hk.window_only && P.host->overlay_open && e.type == SDL_MOUSEWHEEL) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            overlay_mouse(mx, my, e.wheel.y > 0 ? 3 : e.wheel.y < 0 ? 4 : -1);
        } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_SHOWN))
            bar_for_window();
        if (pass) P.hk.unused(&e);
    }
    /* the mouse resting at the bottom (or top) edge of the list scrolls it, a step every 80 ms */
    if (P.host->overlay_open && P.mvalid && !ob_plat_osk_is_open()) {
        int was = P.host->overlay.sel;
        if (ob_overlay_autoscroll(P.host, P.mx, P.my, P.over_mw, P.over_mh, SDL_GetTicks()) && P.host->overlay.sel != was) menu_sound(OB_SFX_MOVE);
    } else if (!P.host->overlay_open) P.mvalid = 0;
}

/* ---- the picture ---- */

void ob_plat_frame_done(int is_new)
{
    uint32_t now = SDL_GetTicks(), ms = now - P.fps_since;
    if (R_chat) ob_chat_tick(R_chat);
    P.fps_all++;
    if (is_new) P.fps_frames++;
    if (ms < 1000) return;
    P.fps_shown = (int)((P.fps_all * 1000u + ms / 2) / ms);
    P.fps_new = (int)((P.fps_frames * 1000u + ms / 2) / ms);
    P.fps_frames = P.fps_all = 0;
    P.fps_since = now;
}

static void fps_draw(void)
{
    char l1[40], l2[40], l3[40];
    ob_host *h = P.host;
    SDL_Rect src, to;
    snprintf(l1, sizeof l1, "FPS %d  NEW %d", P.fps_shown, P.fps_new);
    if (h->refresh == OB_REFRESH_70) snprintf(l2, sizeof l2, "70HZ VSYNC %s", h->vsync ? "ON" : "OFF");
    else if (h->refresh == OB_REFRESH_60) snprintf(l2, sizeof l2, "60HZ VSYNC %s", h->vsync ? "ON" : "OFF");
    else snprintf(l2, sizeof l2, "VRR VSYNC %s", h->vsync ? "ON" : "OFF");
    snprintf(l3, sizeof l3, "CPU -- GPU --");
    {
        const char *const line[3] = { l1, l2, l3 };
        int bw = 0, bh = 0, ww = 0, wh = 0;
        ob_stats_draw(line, P.fps_px, FPS_W, FPS_W, FPS_H, &bw, &bh);
        SDL_UpdateTexture(P.fps_tex, NULL, P.fps_px, FPS_W * 4);
        ob_plat_window_size(&ww, &wh);
        src = (SDL_Rect){ 0, 0, bw, bh };
        ob_stats_place(ww, wh, bw, bh, &to.x, &to.y, &to.w, &to.h);
    }
    SDL_RenderCopy(P.ren, P.fps_tex, &src, &to);
}

/* the black laid over the picture behind the overlay: 153 of 255 leaves it 40% bright */
#define OB_OVERLAY_DIM 153

/* the vector overlay: window-sized, drawn by ob_vmenu_draw, shown 1:1 (smoothly) */
static void overlay_draw_vec(const SDL_Rect *dst)
{
    static uint32_t *vpx;
    static int vcap;
    int ww = 0, wh = 0;
    if (!ob_plat_window_size(&ww, &wh) || ww < 64 || wh < 64) return;
    if (ww * wh > vcap) {
        uint32_t *n = (uint32_t *)realloc(vpx, (size_t)ww * wh * 4);
        if (!n) return;
        vpx = n;
        vcap = ww * wh;
    }
    ob_overlay_draw(P.host, vpx, ww, wh, ww);
    if (P.over_tex && (P.over_mw != ww || P.over_mh != wh)) { SDL_DestroyTexture(P.over_tex); P.over_tex = NULL; }
    if (!P.over_tex && (P.over_tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ww, wh))) {
        SDL_SetTextureBlendMode(P.over_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(P.over_tex, SDL_ScaleModeLinear);
    }
    P.over_mw = ww;
    P.over_mh = wh;
    if (!P.over_tex) return;
    SDL_UpdateTexture(P.over_tex, NULL, vpx, ww * 4);
    SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, OB_OVERLAY_DIM);
    SDL_RenderFillRect(P.ren, dst);
    SDL_RenderCopy(P.ren, P.over_tex, NULL, NULL);
    P.over_x = 0; P.over_y = 0; P.over_w = ww; P.over_h = wh;
}

static void overlay_draw(const SDL_Rect *dst)
{
    int mw, mh, t = (int)(dst->h * 0.8 / (P.cur_h > 0 ? P.cur_h : P.hk.src_h) + 0.5);
    P.host->overlay_vec = P.hk.game_name && !ob_plat_osk_is_open();
    P.host->overlay_name = P.hk.game_name;
    P.host->overlay_platform = P.hk.game_platform;
    if (P.host->overlay_vec) { overlay_draw_vec(dst); return; }
    if (t < 1) t = 1;
    if (ob_plat_osk_is_open()) t *= 2;                 /* the keyboard is drawn at twice the size of the overlay's text (its own scale stays 1 here) */
    mw = dst->w / t;
    mh = dst->h / t;
    while (ob_plat_osk_is_open() && t > 1 && (mw < 208 || mh < 90)) { t--; mw = dst->w / t; mh = dst->h / t; }   /* (the keyboard's size in these pixels) */
    while ((mw > OVER_W || mh > OVER_H) && t < 64) { t++; mw = dst->w / t; mh = dst->h / t; }
    for (int i = 0; i < mw * mh; i++) P.over_px[i] = 0x00040404u;
    ob_overlay_draw(P.host, P.over_px, mw, mh, mw);
    if (P.over_tex && (P.over_mw != mw || P.over_mh != mh)) { SDL_DestroyTexture(P.over_tex); P.over_tex = NULL; }
    if (!P.over_tex && (P.over_tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, mw, mh))) {
        SDL_SetTextureBlendMode(P.over_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(P.over_tex, SDL_ScaleModeNearest);
    }
    P.over_mw = mw;
    P.over_mh = mh;
    if (!P.over_tex) return;
    SDL_Rect to = { dst->x + (dst->w - mw * t) / 2, dst->y + (dst->h - mh * t) / 2, mw * t, mh * t };
    SDL_UpdateTexture(P.over_tex, NULL, P.over_px, mw * 4);
    SDL_SetRenderDrawBlendMode(P.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, OB_OVERLAY_DIM);
    SDL_RenderFillRect(P.ren, dst);
    SDL_RenderCopy(P.ren, P.over_tex, NULL, &to);
    P.over_x = to.x; P.over_y = to.y; P.over_w = to.w; P.over_h = to.h;
}

/* the on-screen keyboard, or the select-sides screen and chat, over the picture at about the picture's own pixel size */
static void osk_draw_screen(const SDL_Rect *dst)
{
    int t = (int)(dst->h * 0.8 / (P.cur_h > 0 ? P.cur_h : P.hk.src_h) + 0.5), mw, mh;
    static uint32_t px[OVER_W * OVER_H];
    static SDL_Texture *tex;
    static int tw, th;
    int sides = R_sides && R_sides->open && !ob_plat_osk_is_open();
    if (sides) {   /* the select-sides screen is the game's own picture: 400 x 224 (16:9) or 320 x 224 (4:3), whatever the window's shape */
        mw = P.cur_w >= 400 || dst->w * 224 >= dst->h * 360 ? 400 : 320;
        mh = 224;
        t = 1;
    } else {
    if (t < 1) t = 1;
    mw = dst->w / t;
    mh = dst->h / t;
    while ((mw > OVER_W || mh > OVER_H) && t < 64) { t++; mw = dst->w / t; mh = dst->h / t; }
    }
    if (tex && (tw != mw || th != mh)) { SDL_DestroyTexture(tex); tex = NULL; }
    if (!tex && (tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, mw, mh))) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    }
    tw = mw;
    th = mh;
    if (!tex) return;
    memset(px, 0, (size_t)mw * mh * 4);
    if (ob_plat_osk_is_open()) {
        if (R_chat) ob_chat_draw(R_chat, px, mw, mh - 5 * 14 - 16 - 12, mw);   /* chat's line above the keyboard */
        ob_osk_draw_at(&osk, px, mw, mh, mw, 1);
    } else ob_plat_room_draw(px, mw, mh, mw);
    SDL_Rect to = { dst->x + (dst->w - mw * t) / 2, dst->y + (dst->h - mh * t) / 2, mw * t, mh * t };
    if (sides) to = *dst;
    sd_ox = to.x; sd_oy = to.y; sd_t = t; sd_mw = mw; sd_mh = mh; sd_tw = to.w; sd_th = to.h;
    SDL_SetTextureScaleMode(tex, to.w % mw == 0 && to.h % mh == 0 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_UpdateTexture(tex, NULL, px, mw * 4);
    SDL_RenderCopy(P.ren, tex, NULL, &to);
}


int ob_window_hz(SDL_Window *win)
{
    SDL_DisplayMode m;
    int d = win ? SDL_GetWindowDisplayIndex(win) : 0;
    if (d < 0) d = 0;
    if (SDL_GetCurrentDisplayMode(d, &m) == 0 && m.refresh_rate >= 24 && m.refresh_rate <= 1000) return m.refresh_rate;
    return 60;
}

/* a full-window screen instead of the picture, at the window's own pixels; the overlay over it when open */
static int screen_present(void)
{
    static uint32_t *px;
    static long cap;
    static SDL_Texture *tex;
    static int tw, th;
    int ww = 0, wh = 0;
    if (!ob_plat_window_size(&ww, &wh) || ww < 16 || wh < 16) return 0;
    if ((long)ww * wh > cap) {
        uint32_t *n = (uint32_t *)realloc(px, (size_t)ww * wh * 4);
        if (!n) return 0;
        px = n;
        cap = (long)ww * wh;
    }
    int pw, ph, lines;
    const uint32_t *pic = ob_screen_picture(&pw, &ph, &lines);
    if (pic) {                                             /* a console-sized picture (a movie): drawn like the game's, through the look */
        /* the look: one the port set, else the movie's own (Scaling (Movie)), else the game's */
        ob_class vcls = P.host->has_movie && P.host->movie_own ? OB_CLASS_MOVIE : P.hk.cls;
        const ob_look *look = ob_screen_video_look();
        if (!look) look = ob_host_look_c(P.host, vcls);
        static uint32_t *half, *ntsc;
        static long half_cap, ntsc_cap;
        int f = lines > 0 && ph % lines == 0 ? ph / lines : 1, sw = pw, sh = ph;
        if (f == 2 && look->mode != OB_MODE_SHARP && pw % 2 == 0) {   /* (a 2x picture: the scanline looks want its lines) */
            if ((long)pw * ph / 4 > half_cap) { free(half); half = (uint32_t *)malloc((size_t)pw * ph); half_cap = half ? (long)pw * ph / 4 : 0; }
            if (half) {
                sw = pw / 2; sh = ph / 2;
                for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
                    const uint32_t *q = pic + (size_t)2 * y * pw + 2 * x;
                    uint32_t o = 0xFF000000u;
                    for (int k = 0; k < 24; k += 8) o |= (((q[0] >> k & 255) + (q[1] >> k & 255) + (q[pw] >> k & 255) + (q[pw + 1] >> k & 255) + 2) / 4) << k;
                    half[y * sw + x] = o;
                }
                pic = half;
            }
        }
        if (look->ntsc) {
            if ((long)sw * sh > ntsc_cap) { free(ntsc); ntsc = (uint32_t *)malloc((size_t)sw * sh * 4); ntsc_cap = ntsc ? (long)sw * sh : 0; }
            if (ntsc) { ob_ntsc_apply(ntsc, pic, sw, sh, sw); pic = ntsc; }
        }
        ob_view_in in;
        ob_view_out out;
        ob_view_from_host(P.host, vcls, pw / f, ph / f, ww, wh, &in);   /* (the console's size: its shape on screen) */
        ob_view(&in, &out);
        for (long i = 0; i < (long)ww * wh; i++) px[i] = 0xFF000000u;
        uint32_t *lp = NULL;
        int dw = 0, dh = 0;
        if (out.blit_w > 0 && out.blit_h > 0 && ob_render_cpu(look, vcls, pic, sw, sh, sw, out.blit_w, out.blit_h, &lp, &dw, &dh) && lp)
            for (int y = 0; y < out.blit_h; y++) {
                int dy = out.blit_y + y, sy = y * dh / out.blit_h;
                if (dy < 0 || dy >= wh) continue;
                for (int x = 0; x < out.blit_w; x++) {
                    int dx = out.blit_x + x;
                    if (dx >= 0 && dx < ww) px[(long)dy * ww + dx] = lp[(long)sy * dw + x * dw / out.blit_w];
                }
            }
        ob_screen_draw_over(px, ww, wh, ww);
    } else if (!ob_screen_draw(px, ww, wh, ww)) return 0;
    if (tex && (tw != ww || th != wh)) { SDL_DestroyTexture(tex); tex = NULL; }
    if (!tex && (tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ww, wh))) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    tw = ww; th = wh;
    if (!tex) return 0;
    SDL_UpdateTexture(tex, NULL, px, ww * 4);
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
    SDL_RenderClear(P.ren);
    SDL_RenderCopy(P.ren, tex, NULL, NULL);
    {
        SDL_Rect all = { 0, 0, ww, wh };
        if (P.host->overlay_open) overlay_draw(&all);
    }
    test_dump();
    SDL_RenderPresent(P.ren);
    return 1;
}

void ob_plat_present(void)
{
    int ww, wh, sw = P.hk.src_w, sh = P.hk.src_h;
    ob_view_in in;
    ob_view_out out;
    const uint32_t *picture;
    SDL_Rect dst;
    SDL_Texture *show;
    const void *pix;
    int pitch;
    if (P.hk.window_only) { if (P.hk.present) P.hk.present(); return; }   /* the port presents */
    if (!P.ren) return;
    {
        /* the box (a full-window screen) waits for the display (vsync) whatever the game's setting, so the main loop draws it once
           per refresh, evenly; the setting comes back when it closes */
        static int box_vsync;
        int up = ob_screen_up();
        if (up != box_vsync) { box_vsync = up; SDL_RenderSetVSync(P.ren, up || P.vsync0); }
    }
    if (ob_screen_up() && screen_present()) return;
    picture = P.hk.frame ? P.hk.frame(&sw, &sh) : P.hk.picture();
    if (!picture || sw < 1 || sh < 1) return;
    if (sw != P.cur_w || sh != P.cur_h) {                 /* the game changed the picture's size */
        if (P.tex) SDL_DestroyTexture(P.tex);
        P.tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
        P.cur_w = sw;
        P.cur_h = sh;
        if (!P.tex) return;
    }
    show = P.tex;
    pix = picture;
    pitch = sw * 4;
    ob_plat_window_size(&ww, &wh);
    ob_view_from_host(P.host, P.hk.cls, sw, sh, ww, wh, &in);
    if (P.hk.wide) P.hk.wide(&in.wide_num, &in.wide_den);
    ob_view(&in, &out);
    dst = (SDL_Rect){ out.blit_x, out.blit_y, out.blit_w, out.blit_h };
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
    SDL_RenderClear(P.ren);
    if (P.host->lores.ntsc) {
        static uint32_t *ntsc_px;
        static long ntsc_cap;
        if ((long)sw * sh > ntsc_cap) {
            free(ntsc_px);
            ntsc_px = (uint32_t *)malloc((size_t)sw * sh * 4);
            ntsc_cap = ntsc_px ? (long)sw * sh : 0;
        }
        if (ntsc_px) {
            ob_ntsc_apply(ntsc_px, picture, sw, sh, sw);
            pix = ntsc_px;
            picture = ntsc_px;
        }
    }
    {   /* the one rendering path (ob_render_plan_get): every look, Sharp included, drawn at the picture's size in window pixels */
        uint32_t *px = NULL;
        int dw = 0, dh = 0;
        if (ob_render_cpu(&P.host->lores, P.hk.cls, picture, sw, sh, sw, dst.w, dst.h, &px, &dw, &dh) && px && dw > 0 && dh > 0) {
            if (!P.look_tex || P.look_w != dw || P.look_h != dh) {
                if (P.look_tex) SDL_DestroyTexture(P.look_tex);
                P.look_tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, dw, dh);
                P.look_w = dw;
                P.look_h = dh;
                if (P.look_tex) SDL_SetTextureScaleMode(P.look_tex, SDL_ScaleModeLinear);   /* 1:1 unless the picture is above ~2.1M pixels */
            }
            if (P.look_tex) { show = P.look_tex; pix = px; pitch = dw * 4; }
        }
    }
    SDL_UpdateTexture(show, NULL, pix, pitch);
    SDL_RenderCopy(P.ren, show, NULL, &dst);
    if (P.host->stats) fps_draw();
    if (P.hk.draw_over) P.hk.draw_over(&dst);
    if (P.hk.captures && P.hk.captures()) { /* the port's setup screen is drawn over the picture by draw_over */ }
    else if (P.host->overlay_open) overlay_draw(&dst);
    if (ob_plat_osk_is_open() || (!P.host->overlay_open && ((R_sides && R_sides->open) || (R_chat && ob_chat_shows(R_chat))))) osk_draw_screen(&dst);
    test_dump();
    SDL_RenderPresent(P.ren);
}

#endif /* OVERBAR_SDL */
