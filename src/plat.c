/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The shared SDL2 platform layer (docs/PLATFORM.md). Lifted from the SNES C port's window code, which
 * the other ports duplicated, so one copy runs on a desktop and on a Linux handheld.
 */
#include "overbar/plat.h"

#ifdef OVERBAR_SDL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <SDL_syswm.h>
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
    uint32_t over_px[OVER_W * OVER_H];
    uint32_t fps_px[FPS_W * FPS_H];
    int over_mw, over_mh, over_x, over_y, over_w, over_h;
    int fps_frames, fps_shown, fps_all, fps_new;
    uint32_t fps_since;
} P = { .text_on = -1, .cursor_hidden = -1 };

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

static void ctrl_added(int index)
{
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (P.ctrl[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(P.ctrl[i])) == id) return;
    for (int i = 0; i < OB_PLAT_SLOTS; i++)
        if (!P.ctrl[i]) {
            P.ctrl[i] = SDL_GameControllerOpen(index);
            if (P.hk.controller) P.hk.controller(i, 1);
            break;
        }
}

static void ctrl_removed(SDL_JoystickID id)
{
    int s = ob_plat_controller_slot(id);
    if (s < 0) return;
    SDL_GameControllerClose(P.ctrl[s]);
    P.ctrl[s] = NULL;
    P.stick_dir[s] = 0;
    if (P.hk.controller) P.hk.controller(s, 0);
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

int ob_plat_open(const ob_plat_hooks *hooks, ob_host *h)
{
    Uint32 wflags = SDL_WINDOW_RESIZABLE, rflags = SDL_RENDERER_ACCELERATED;
    SDL_RendererInfo rinfo;
    if (!hooks || !h || (!hooks->picture && !hooks->frame && !hooks->window_only)) return 0;
    P.hk = *hooks;
    P.host = h;
    if (h->profile == OB_PROFILE_HANDHELD) {
        wflags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (h->vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;
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
    if (!P.tex || !P.fps_tex) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); ob_plat_close(); return 0; }
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
    for (int i = 0; i < SDL_NumJoysticks(); i++) ctrl_added(i);
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
}

/* ---- text input, the cursor, the overlay's keys ---- */

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
    int hide = !(P.host->overlay_open || (P.hk.captures && P.hk.captures()) || P.host->pointer == 1);
    if (hide != P.cursor_hidden) { SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE); P.cursor_hidden = hide; }
}

static int overlay_key_to_input(SDL_Scancode k)
{
    if (k == SDL_SCANCODE_UP) return MDRT_MENU_UP;
    if (k == SDL_SCANCODE_DOWN) return MDRT_MENU_DOWN;
    if (k == SDL_SCANCODE_LEFT) return MDRT_MENU_LEFT;
    if (k == SDL_SCANCODE_RIGHT) return MDRT_MENU_RIGHT;
    if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) return MDRT_MENU_OK;
    if (k == SDL_SCANCODE_ESCAPE) return MDRT_MENU_BACK;
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

static void overlay_handle(int mdrt_input)
{
    int r = ob_overlay_input(P.host, mdrt_input);
    if (r == OB_ID_CONTROLLERS && P.hk.action) P.hk.action(r);
    after_change();
}

static void pad_back(void)
{
    if (P.host->overlay_open) ob_overlay_close(P.host);
    else ob_overlay_open(P.host);
}

static void overlay_mouse(int wx, int wy, int what)
{
    int ww, wh, winw, winh, r;
    if (!P.host->overlay_open || what < 0 || !ob_plat_window_size(&ww, &wh)) return;
    SDL_GetWindowSize(P.win, &winw, &winh);
    if (winw > 0 && winh > 0 && (ww != winw || wh != winh)) { wx = wx * ww / winw; wy = wy * wh / winh; }
    if (P.over_w <= 0 || P.over_h <= 0 || wx < P.over_x || wy < P.over_y || wx >= P.over_x + P.over_w || wy >= P.over_y + P.over_h) return;
    r = ob_overlay_pointer(P.host, (wx - P.over_x) * P.over_mw / P.over_w, (wy - P.over_y) * P.over_mh / P.over_h,
                           P.over_mw, P.over_mh, what);
    if (what == 0) return;
    if (r == OB_ID_CONTROLLERS && P.hk.action) P.hk.action(r);
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
        return open || k == SDL_SCANCODE_GRAVE || k == SDL_SCANCODE_F11 || k == SDL_SCANCODE_F12;
    }
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL: return P.host->overlay_open && !P.hk.window_only;
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: return open;
    default: return 0;                                    /* controllers coming and going, window events, joystick: the game's too */
    }
}

void ob_plat_pump(void)
{
    SDL_Event e;
    int captured, pass;
    cursor_update();
    while (SDL_PollEvent(&e)) {
        if (P.hk.event && P.hk.event(&e)) continue;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
            static int win_w, win_h;                          /* Alt+Enter; the windowed size, which the bar's coming back would shrink */
            int full = !!(SDL_GetWindowFlags(P.win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
            if (!full) SDL_GetWindowSize(P.win, &win_w, &win_h);
            SDL_SetWindowFullscreen(P.win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            if (full && win_w > 0) { bar_for_window(); SDL_SetWindowSize(P.win, win_w, win_h); }
            continue;
        }
        captured = P.hk.captures && P.hk.captures();
        if (P.hk.unused && !event_used(&e, captured)) pass = 1;
        else pass = 0;
        if (e.type == SDL_QUIT) do_quit();
        else if (e.type == SDL_TEXTINPUT) {
            if (P.host->overlay_open && ob_overlay_editing(P.host)) ob_overlay_text(P.host, e.text.text);
        } else if (e.type == SDL_KEYDOWN && P.host->overlay_open && ob_overlay_editing(P.host)) {
            SDL_Scancode k = e.key.keysym.scancode;
            if (k == SDL_SCANCODE_V && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) {
                char *clip = SDL_GetClipboardText();
                if (clip) { ob_overlay_text(P.host, clip); SDL_free(clip); }
            } else {
                int in = editing_key_to_input(k);
                if (in >= 0) overlay_handle(in);
            }
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int sc = e.key.keysym.scancode;
            if (captured) { if (P.hk.raw_key) P.hk.raw_key(sc); }
            else if (ob_overlay_toggle_key(P.host, sc) || ob_stats_toggle_key(P.host, sc)) {
                after_change();
                P.fps_frames = P.fps_all = 0;
                P.fps_since = SDL_GetTicks();
            } else if (P.host->overlay_open) {
                int in = overlay_key_to_input(sc);
                if (in >= 0) overlay_handle(in);
            }
            sync_text_input();
        } else if (e.type == SDL_CONTROLLERBUTTONDOWN && (captured || P.host->overlay_open)) {
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
        } else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            pad_back();
            sync_text_input();
        } else if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int slot = ob_plat_controller_slot(e.caxis.which);
            if (slot >= 0) {
                int v = e.caxis.value;
                int d = v < -16384 ? -1 : v > 16384 ? 1 : v > -8192 && v < 8192 ? 0 : P.stick_dir[slot];
                if (d && d != P.stick_dir[slot] && P.host->overlay_open && !captured) overlay_handle(d < 0 ? MDRT_MENU_UP : MDRT_MENU_DOWN);
                P.stick_dir[slot] = d;
            }
        } else if (e.type == SDL_CONTROLLERAXISMOTION &&
                   (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
            int slot = ob_plat_controller_slot(e.caxis.which), k = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            if (slot >= 0 && e.caxis.value > 16384 && !P.trigger_down[slot][k]) {
                P.trigger_down[slot][k] = 1;
                if (captured && P.hk.raw_button) P.hk.raw_button(slot, k ? -2 : -1);
            } else if (slot >= 0 && e.caxis.value < 8192) P.trigger_down[slot][k] = 0;
        } else if (e.type == SDL_CONTROLLERDEVICEADDED) ctrl_added(e.cdevice.which);
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) ctrl_removed(e.cdevice.which);
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
}

/* ---- the picture ---- */

void ob_plat_frame_done(int is_new)
{
    uint32_t now = SDL_GetTicks(), ms = now - P.fps_since;
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

static void overlay_draw(const SDL_Rect *dst)
{
    int mw, mh, t = (int)(dst->h * 0.8 / (P.cur_h > 0 ? P.cur_h : P.hk.src_h) + 0.5);
    if (t < 1) t = 1;
    mw = dst->w / t;
    mh = dst->h / t;
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
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 96);
    SDL_RenderFillRect(P.ren, dst);
    SDL_RenderCopy(P.ren, P.over_tex, NULL, &to);
    P.over_x = to.x; P.over_y = to.y; P.over_w = to.w; P.over_h = to.h;
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
    if (!P.ren) return;                                   /* window only: the port presents */
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
    ob_view(&in, &out);
    dst = (SDL_Rect){ out.blit_x, out.blit_y, out.blit_w, out.blit_h };
    SDL_SetRenderDrawColor(P.ren, 0, 0, 0, 255);
    SDL_RenderClear(P.ren);
    if (ob_look_renders(&P.host->lores)) {
        /* the CRT looks and the MiSTer filters at the picture's size in window pixels */
        uint32_t *px = NULL;
        int dw = 0, dh = 0;
        if (ob_look_render(&P.host->lores, picture, sw, sh, sw, dst.w, dst.h, &px, &dw, &dh) && px && dw > 0 && dh > 0) {
            if (!P.look_tex || P.look_w != dw || P.look_h != dh) {
                if (P.look_tex) SDL_DestroyTexture(P.look_tex);
                P.look_tex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, dw, dh);
                P.look_w = dw;
                P.look_h = dh;
                if (P.look_tex) SDL_SetTextureScaleMode(P.look_tex, SDL_ScaleModeLinear);
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
    SDL_RenderPresent(P.ren);
}

#endif /* OVERBAR_SDL */
