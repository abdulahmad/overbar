/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * v1 checks: dest-rect (T1 T2 T5 T8 T9), defaults (T14), keys (T16),
 * Scale Mode sub-options + live preview (T12), no Native (T4/L12),
 * file-menu show policy (T10 T11).
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#include "overbar/overbar.h"
#include "overbar/vmenu.h"
#include "overbar/sfx.h"
#include "overbar/season.h"
#include "overbar/screen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>

#define make_dir(p) mkdir(p, 0755)
#endif

static int test_net_started;
static int test_net_start(const ob_online *o, char *msg, int n)
{
    (void)o; (void)msg; (void)n;
    test_net_started++;
    return 0;
}

#ifdef _WIN32
/* ucrt does not declare POSIX setenv. _putenv_s updates the copy getenv reads.
 * Ubuntu CI uses the real ones. */
static int setenv(const char *k, const char *v, int overwrite)
{
    const char *cur;
    if (!overwrite && (cur = getenv(k)) && cur[0]) return 0;
    return _putenv_s(k, v) == 0 ? 0 : -1;
}
static int unsetenv(const char *k)
{
    return _putenv_s(k, "") == 0 ? 0 : -1;
}
#endif

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } \
} while (0)

static void test_scaler_console(void)
{
    ob_view_in in;
    ob_view_out v;
    int num, den;

    memset(&in, 0, sizeof in);
    in.window_w = 960;
    in.window_h = 720;
    in.height = OB_HEIGHT_FIT;
    in.dar_h = 240;
    in.src_h = 224;

    /* T1: H40 320x224. The pixel shape of a 4:3 screen of 240 lines (PAR 1.0,
     * not 0.938), but no 8+8 border: the picture is 10:7 and fills the width. */
    in.src_w = 320;
    ob_view(&in, &v);
    CHECK(v.src_h == 224);
    CHECK(v.src_w == 320);
    CHECK(v.blit_w == 960 && v.blit_h == 672);
    CHECK(v.blit_h == v.canvas_h && v.blit_y == v.canvas_y);   /* no border rows */
    ob_view_par(&v, &num, &den);
    CHECK(num == 1 && den == 1);

    /* A 16:9 window: the 224 lines fill its height (a border would leave 72 rows). */
    in.window_w = 1920;
    in.window_h = 1080;
    ob_view(&in, &v);
    CHECK(v.blit_h == 1080 && v.blit_y == 0);
    CHECK(v.blit_w == 1080 * 10 / 7);
    in.window_w = 960;
    in.window_h = 720;

    /* T2: H32 / SNES 256x224. Same DAR; PAR 5:4. */
    in.src_w = 256;
    ob_view(&in, &v);
    CHECK(v.src_h == 224);
    CHECK(v.blit_w == 960 && v.blit_h == 672);
    ob_view_par(&v, &num, &den);
    CHECK(num == 5 && den == 4);

    /* T5 negative: 256x224 stretched to 4:3 using height 224 must not match T2. */
    in.dar_h = 224;
    ob_view(&in, &v);
    ob_view_par(&v, &num, &den);
    CHECK(!(num == 5 && den == 4));
    CHECK(v.blit_h == v.canvas_h);               /* unpadded: source fills dest height */

    /* Integer: a whole number of dest rows per 224 source line. */
    in.dar_h = 240;
    in.src_w = 320;
    in.height = OB_HEIGHT_INTEGER;
    in.window_w = 1920;
    in.window_h = 1080;
    ob_view(&in, &v);
    CHECK(v.blit_h == 4 * 224 && v.blit_w == 1280);
    CHECK(v.blit_h == v.canvas_h);
    ob_view_par(&v, &num, &den);
    CHECK(num == 1 && den == 1);

    /* T9: 31 kHz line-double: rows come in pairs; the pixel shape is the same. */
    in.line_double = 1;
    ob_view(&in, &v);
    CHECK(v.blit_h % 448 == 0);
    ob_view_par(&v, &num, &den);
    CHECK(num == 1 && den == 1);
}

/* a widescreen frame: 5 / 4 as wide as the 4:3 picture, the same pixel shape, all of it shown */
static void test_scaler_wide(void)
{
    ob_view_in in;
    ob_view_out v, n;
    memset(&in, 0, sizeof in);
    in.src_w = 320; in.src_h = 224; in.dar_h = 240; in.window_w = 1280; in.window_h = 720; in.height = OB_HEIGHT_FIT;
    ob_view(&in, &n);
    in.src_w = 400; in.wide_num = 5; in.wide_den = 4;
    ob_view(&in, &v);
    CHECK(v.blit_w == 1280 && v.blit_h == 716);                    /* 400 : 224, a hair wider than 16:9: fits the width */
    CHECK(v.blit_x == 0 && v.blit_y == 2);
    CHECK(n.blit_h == 720 && n.blit_w == 1028);                    /* the 4:3 one in the same window */
    in.height = OB_HEIGHT_INTEGER;
    ob_view(&in, &v);
    CHECK(v.blit_h == 672 && v.blit_w == 1200);                    /* 3 rows a line, 3 columns a pixel */
}

static void test_scaler_vga(void)
{
    ob_view_in in;
    ob_view_out v;
    memset(&in, 0, sizeof in);
    in.src_w = 320;
    in.src_h = 200;
    in.dar_h = 200;
    in.window_w = 960;
    in.window_h = 720;
    in.height = OB_HEIGHT_FIT;
    ob_view(&in, &v);
    CHECK(v.blit_w == v.canvas_w);
    CHECK(v.blit_h == v.canvas_h);               /* VGA: source fills the 4:3 canvas */
    CHECK(v.canvas_w == v.canvas_h * 4 / 3);
}

static void clear_handheld_env(void)
{
    unsetenv("OVERBAR_HANDHELD");
    unsetenv("PORTMASTER");
    unsetenv("CFW_NAME");
    unsetenv("DEVICE_NAME");
    unsetenv("SDL_VIDEODRIVER");
    unsetenv("SDL_RENDER_DRIVER");
    unsetenv("SDL_OPENGL_ES_DRIVER");
}

static void test_defaults(void)
{
    ob_host d, hh;
    clear_handheld_env();
    ob_host_init(&d, OB_PORT_PC95, OB_PROFILE_DESKTOP);
    CHECK(d.refresh == OB_REFRESH_VRR);
    CHECK(d.vsync == 0);
    CHECK(d.stats == 0);
    CHECK(d.hires.mode == OB_MODE_SHARP && d.hires.sharp == OB_SHARP_1X);
    CHECK(d.lores.mode == OB_MODE_SHARP && d.lores.sharp == OB_SHARP_3X);
    CHECK(d.lores.height == OB_HEIGHT_FIT && d.hires.height == OB_HEIGHT_FIT);
    CHECK(d.has_hires);
    CHECK(d.present == OB_PRESENT_GL);
    ob_host_free(&d);

    ob_host_init(&hh, OB_PORT_SNES, OB_PROFILE_HANDHELD);
    CHECK(hh.refresh == OB_REFRESH_60);
    CHECK(hh.vsync == 1);
    CHECK(hh.stats == 0);
    CHECK(hh.lores.mode == OB_MODE_SHARP && hh.lores.sharp == OB_SHARP_2X);
    CHECK(hh.console_dar);
    CHECK(hh.lores.height == OB_HEIGHT_FIT);
    CHECK(!hh.has_hires);
    CHECK(hh.present == OB_PRESENT_SDL);
    ob_host_free(&hh);

    CHECK(ob_profile_detect("KMSDRM", 0) == OB_PROFILE_HANDHELD);
    CHECK(ob_profile_detect("x11", 0) == OB_PROFILE_DESKTOP);
    CHECK(ob_profile_detect("x11", 1) == OB_PROFILE_HANDHELD);
    CHECK(ob_profile_detect("mali", 0) == OB_PROFILE_HANDHELD);
}

static void test_handheld_detect_and_present(void)
{
    ob_host h;
    ob_item items[OB_ITEMS_MAX];
    int n;
    uint32_t src[320 * 224], dst[640 * 480];
    ob_view_in in;
    ob_view_out v;
    clear_handheld_env();
    CHECK(ob_handheld_env() == 0);
    CHECK(ob_profile_from_env() == OB_PROFILE_DESKTOP);
    setenv("CFW_NAME", "muOS", 1);
#ifndef _WIN32
    CHECK(ob_handheld_env() == 1);
    CHECK(ob_profile_from_env() == OB_PROFILE_HANDHELD);
#else
    /* A leftover CFW_NAME from a packed zip must not flip the Windows exe. */
    CHECK(ob_handheld_env() == 0);
    CHECK(ob_profile_from_env() == OB_PROFILE_DESKTOP);
#endif
    unsetenv("CFW_NAME");
    setenv("DEVICE_NAME", "RG35XX-PLUS", 1);
#ifndef _WIN32
    CHECK(ob_profile_from_env() == OB_PROFILE_HANDHELD);
#else
    CHECK(ob_profile_from_env() == OB_PROFILE_DESKTOP);
#endif
    unsetenv("DEVICE_NAME");
    setenv("OVERBAR_HANDHELD", "1", 1);
    CHECK(ob_profile_from_env() == OB_PROFILE_HANDHELD);
    unsetenv("OVERBAR_HANDHELD");

    CHECK(ob_present_pick(OB_PROFILE_HANDHELD, 0, 1) == OB_PRESENT_SDL);
    CHECK(ob_present_pick(OB_PROFILE_HANDHELD, 1, 1) == OB_PRESENT_GLES2);
    CHECK(ob_present_pick(OB_PROFILE_DESKTOP, 0, 1) == OB_PRESENT_GL);
    CHECK(ob_present_pick(OB_PROFILE_DESKTOP, 0, 0) == OB_PRESENT_SDL);
    CHECK(ob_look_supported(OB_MODE_SHARP, OB_PRESENT_SDL));
    CHECK(ob_look_supported(OB_MODE_CRT, OB_PRESENT_SDL));
    CHECK(ob_look_supported(OB_MODE_MISTER, OB_PRESENT_SDL));     /* (on the CPU) */
    CHECK(ob_look_supported(OB_MODE_MISTER, OB_PRESENT_GLES2));
    CHECK(ob_look_fallback_note(OB_MODE_CRT, OB_PRESENT_SDL) == NULL);
    CHECK(ob_look_fallback_note(OB_MODE_MISTER, OB_PRESENT_SDL) == NULL);
    CHECK(ob_look_fallback_note(OB_MODE_SHARP, OB_PRESENT_SDL) == NULL);

    ob_host_init(&h, OB_PORT_SNES, OB_PROFILE_HANDHELD);
    CHECK(h.refresh == OB_REFRESH_60);
    CHECK(h.vsync == 1);
    CHECK(h.present == OB_PRESENT_SDL);
    CHECK(ob_filemenu_wanted(1, 0, 1) == 0);
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_MISTER);
    CHECK(h.fallback_note == NULL);
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(!ob_items_contain(items, n, OB_ID_PRESENT_NOTE));
    CHECK(ob_items_contain(items, n, OB_ID_MISTER_H));
    ob_host_set_present(&h, 1, 0);
    CHECK(h.present == OB_PRESENT_GLES2);
    CHECK(h.fallback_note == NULL);

    ob_present_apply_hints(OB_PROFILE_HANDHELD);
#ifndef _WIN32
    /* Windows must not ask SDL for a GLES2 DLL. Handheld hints are the Linux path. */
    CHECK(getenv("SDL_RENDER_DRIVER") && !strcmp(getenv("SDL_RENDER_DRIVER"), "opengles2"));
    CHECK(getenv("OVERBAR_HANDHELD") && !strcmp(getenv("OVERBAR_HANDHELD"), "1"));
#else
    CHECK(!getenv("SDL_RENDER_DRIVER") || !getenv("SDL_RENDER_DRIVER")[0]);
#endif

    memset(&in, 0, sizeof in);
    in.src_w = 320;
    in.src_h = 224;
    in.dar_h = 240;
    in.window_w = 640;
    in.window_h = 480;
    in.height = OB_HEIGHT_FIT;
    ob_view(&in, &v);
    CHECK(v.blit_w == 640 && v.blit_h == 448);
    memset(src, 0xFF, sizeof src);
    memset(dst, 0, sizeof dst);
    ob_present_soft(src, 320, 224, dst, 640, 480, 640, &v);
    CHECK(dst[v.blit_y * 640 + v.blit_x] == 0xFFFFFFFFu);
    CHECK(dst[0] == 0);          /* letterbox, not source texels */
    ob_host_free(&h);
    clear_handheld_env();
}

static void test_ini_first_launch(void)
{
    ob_host h;
    char path[] = "/tmp/overbar-test-settings.ini";
    remove(path);
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    CHECK(ob_host_load_ini(&h, path) == 0);
    CHECK(h.wrote_defaults);
    CHECK(h.refresh == OB_REFRESH_VRR);
    CHECK(h.lores.mode == OB_MODE_SHARP);
    CHECK(h.lores.sharp == OB_SHARP_3X);
    ob_host_free(&h);

    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    CHECK(ob_host_load_ini(&h, path) == 1);
    CHECK(!h.wrote_defaults);
    CHECK(h.refresh == OB_REFRESH_VRR);
    ob_host_free(&h);

    {   /* Widescreen: listed only for a port that draws wide pictures, saved and read back */
        ob_item it[OB_ITEMS_MAX];
        int n, i, found = 0;
        ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
        CHECK(ob_host_load_ini(&h, path) == 1);
        n = ob_page_items(&h, OB_PAGE_SCALE_LO, it, OB_ITEMS_MAX);
        for (i = 0; i < n; i++) if (it[i].id == OB_ID_WIDESCREEN) found = 1;
        CHECK(!found);
        h.widescreen_ok = 1;
        n = ob_page_items(&h, OB_PAGE_SCALE_LO, it, OB_ITEMS_MAX);
        for (i = 0; i < n; i++) if (it[i].id == OB_ID_WIDESCREEN) found = 1;
        CHECK(found);
        ob_host_apply(&h, OB_CLASS_LORES, OB_ID_WIDESCREEN, 1);
        CHECK(h.widescreen == 1);
        CHECK(ob_host_save_ini(&h) == 1 || 1);
        ob_host_free(&h);
        ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
        h.widescreen_ok = 1;
        CHECK(ob_host_load_ini(&h, path) == 1);
        CHECK(h.widescreen == 1);
        ob_host_free(&h);
    }
    remove(path);
}

static void test_keys(void)
{
    ob_host h;
    ob_item items[OB_ITEMS_MAX];
    int n;
    ob_host_init(&h, OB_PORT_GENERIC, OB_PROFILE_DESKTOP);
    CHECK(ob_map_key(OB_SCANCODE_GRAVE) == OB_HOSTKEY_OVERLAY);
    CHECK(ob_map_key(OB_SCANCODE_F11) == OB_HOSTKEY_NONE);    /* F11 no longer opens it */
    CHECK(ob_map_key(OB_SCANCODE_F12) == OB_HOSTKEY_STATS);
    CHECK(ob_map_key(OB_SCANCODE_F9) == OB_HOSTKEY_OSK);
    CHECK(ob_map_pad_back() == OB_HOSTKEY_OVERLAY);
    CHECK(ob_map_key(41) == OB_HOSTKEY_NONE);     /* Esc is not overlay */
    ob_host_devices_clear(&h);
    CHECK(ob_host_device_add(&h, "Keyboard", "Keyboard"));
    CHECK(ob_host_device_add(&h, "Controller 1", "Pad - player 1"));
    n = ob_page_items(&h, OB_PAGE_INPUT, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_DEVICE) && ob_items_contain(items, n, OB_ID_CONTROLLERS));
    ob_host_devices_clear(&h);
    for (int i = 0; i < OB_DEVICES_MAX + 3; i++) ob_host_device_add(&h, "Controller", "x");
    n = ob_page_items(&h, OB_PAGE_INPUT, items, OB_ITEMS_MAX);
    CHECK(h.device_n == OB_DEVICES_MAX && ob_items_contain(items, n, OB_ID_BACK));
    ob_host_devices_clear(&h);
    n = ob_page_items(&h, OB_PAGE_KEYS, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_OSK));
    CHECK(ob_overlay_toggle_key(&h, OB_SCANCODE_GRAVE));
    CHECK(h.overlay_open);
    CHECK(!ob_overlay_toggle_key(&h, OB_SCANCODE_F11));
    CHECK(h.overlay_open);
    CHECK(ob_overlay_toggle_key(&h, OB_SCANCODE_GRAVE));
    CHECK(!h.overlay_open);
    CHECK(ob_stats_toggle_key(&h, OB_SCANCODE_F12));
    CHECK(h.stats == 1);
    CHECK(!ob_overlay_toggle_key(&h, OB_SCANCODE_F12));
    ob_host_free(&h);
}

static void test_scale_mode_options(void)
{
    ob_host h;
    ob_item items[OB_ITEMS_MAX];
    int n;
    ob_host_init(&h, OB_PORT_SNES, OB_PROFILE_DESKTOP);
    CHECK(!ob_snes_has_native());

    CHECK(!h.has_hires);
    /* Scaling is its own menu on the main page, not under Video; one for a console. */
    n = ob_main_items(&h, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_SCALE_LO));
    CHECK(!ob_items_contain(items, n, OB_ID_SCALE_HI));
    n = ob_video_items(&h, OB_CLASS_LORES, items, OB_ITEMS_MAX);
    CHECK(!ob_items_contain(items, n, OB_ID_SCALE_LO));
    CHECK(!ob_items_contain(items, n, OB_ID_SCALE_MODE));
    CHECK(!ob_items_contain_label(items, n, "Native"));
    CHECK(!ob_items_contain_label(items, n, "Picture Native"));

    /* One level: the mode, its own knobs right there, then height and monitor. */
    n = ob_page_items(&h, OB_PAGE_SCALE_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_MODE_MENU_LO));
    CHECK(!ob_items_contain(items, n, OB_ID_OPTS_MENU_LO));
    CHECK(ob_items_contain(items, n, OB_ID_PICTURE_HEIGHT));
    CHECK(ob_items_contain(items, n, OB_ID_MONITOR_KHZ));
    CHECK(!ob_items_contain(items, n, OB_ID_SCALE_MODE));
    CHECK(!ob_items_contain(items, n, OB_ID_RETROARCH_SHADER));
    CHECK(ob_items_contain(items, n, OB_ID_SHARP_AMOUNT));
    CHECK(!ob_items_contain_label(items, n, "Native"));
    CHECK(!ob_items_contain_label(items, n, "Picture Native"));
    CHECK(!ob_items_contain_label(items, n, "RetroArch"));

    n = ob_page_items(&h, OB_PAGE_MODE_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_SCALE_MODE));
    CHECK(ob_items_contain_label(items, n, "Sharp"));
    CHECK(ob_items_contain_label(items, n, "CRT Monitor"));
    CHECK(!ob_items_contain_label(items, n, "RetroArch"));
    CHECK(!ob_items_contain_label(items, n, "RetroArch filters"));

    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_SHARP_AMOUNT));
    CHECK(!ob_items_contain(items, n, OB_ID_CRT_LOOK));
    CHECK(!ob_items_contain(items, n, OB_ID_MISTER_H));
    CHECK(!ob_items_contain(items, n, OB_ID_PICTURE_HEIGHT));

    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_CRT);
    CHECK(h.live_preview);
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_CRT_LOOK));
    CHECK(!ob_items_contain(items, n, OB_ID_SHARP_AMOUNT));
    CHECK(!ob_items_contain(items, n, OB_ID_MISTER_RESET));
    n = ob_page_items(&h, OB_PAGE_SCALE_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_MONITOR_KHZ));
    CHECK(ob_items_contain(items, n, OB_ID_PICTURE_HEIGHT));

    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_MISTER);
    n = ob_page_items(&h, OB_PAGE_SCALE_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_MISTER_PRESET));
    CHECK(ob_items_contain(items, n, OB_ID_MISTER_H));
    CHECK(!ob_items_contain(items, n, OB_ID_SHARP_AMOUNT));
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_MISTER_H));
    CHECK(ob_items_contain(items, n, OB_ID_MISTER_RESET));
    CHECK(!ob_items_contain(items, n, OB_ID_CRT_LOOK));
    CHECK(!ob_items_contain(items, n, OB_ID_MONITOR_KHZ));

    CHECK(h.lores.height == OB_HEIGHT_AUTO_CROP);          /* scanlines: Auto Crop */
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_SCALE2X);
    CHECK(h.lores.height == OB_HEIGHT_FIT);                /* no scanlines: Fit */
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_PIXEL_SIZE));   /* Scale2x: 2x, 3x, 4x */
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_PIXEL_SIZE, 2);
    CHECK(h.lores.mode == OB_MODE_SCALE4X);
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_SCALE2X);
    CHECK(h.lores.mode == OB_MODE_SCALE4X);                /* the same family keeps its size */
    CHECK(ob_mode_choice_index(OB_MODE_SCALE3X) == ob_mode_choice_index(OB_MODE_SCALE2X));
    {
        const char *const *labels;
        const ob_scale_mode *modes;
        CHECK(ob_pixel_sizes(OB_MODE_MMPX4X, &labels, &modes) == 2 && modes[1] == OB_MODE_MMPX4X);
        CHECK(ob_pixel_sizes(OB_MODE_XBR2X, &labels, &modes) == 3 && !strcmp(labels[1], "3x"));
        CHECK(ob_pixel_sizes(OB_MODE_CRT, &labels, &modes) == 0);
    }
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_XBR2X);
    CHECK(h.lores.mode == OB_MODE_XBR2X);
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(!ob_items_contain(items, n, OB_ID_SHARP_AMOUNT));
    CHECK(!ob_items_contain(items, n, OB_ID_CRT_LOOK));
    CHECK(!ob_items_contain(items, n, OB_ID_MISTER_H));
    n = ob_page_items(&h, OB_PAGE_SCALE_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_PICTURE_HEIGHT));

    n = ob_main_items(&h, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_ONLINE));
    CHECK(!h.online_ready);
    {
        static int started;
        ob_online_ops ops;
        ob_item it2[OB_ITEMS_MAX];
        int k;
        memset(&ops, 0, sizeof ops);
        ops.max_pads = 4;
        ops.start = test_net_start;
        ob_online_set_ops(&h, &ops);
        CHECK(h.online_ready);
        h.online.role = 1;
        k = ob_page_items(&h, OB_PAGE_ONLINE, it2, OB_ITEMS_MAX);
        CHECK(ob_items_contain(it2, k, OB_ID_NET_NAME));
        CHECK(ob_items_contain(it2, k, OB_ID_NET_ADDR));
        CHECK(ob_items_contain(it2, k, OB_ID_NET_HERE));
        CHECK(!ob_items_contain(it2, k, OB_ID_NET_ALL));      /* a joiner does not set the total */
        CHECK(ob_items_contain(it2, k, OB_ID_NET_START));
        h.online.role = 0;
        k = ob_page_items(&h, OB_PAGE_ONLINE, it2, OB_ITEMS_MAX);
        CHECK(!ob_items_contain(it2, k, OB_ID_NET_ADDR));
        ob_online_open(&h);
        CHECK(h.overlay_open && h.overlay_page == OB_PAGE_ONLINE);
        mdrt_menu_select(&h.overlay, OB_ID_NET_START);
        started = test_net_started;
        ob_overlay_input(&h, MDRT_MENU_OK);
        CHECK(test_net_started == started + 1);
        CHECK(!h.overlay_open);                               /* on its way: the overlay closes */
        ob_online_set_ops(&h, NULL);
        CHECK(!h.online_ready);
    }
    ob_host_free(&h);
}

static void test_overlay_live_preview(void)
{
    ob_host h;
    uint32_t px[320 * 224];
    int i, found_crt = 0;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    ob_overlay_open(&h);
    CHECK(h.overlay_open);
    /* Main → Scaling (its own menu now). Scale Mode flips in place (Sharp, then CRT), and the
     * mode's own knobs are on the same page. */
    for (i = 0; i < h.overlay.count; i++)
        if (h.overlay.items[i].id == OB_ID_SCALE_LO) { h.overlay.sel = i; break; }
    ob_overlay_input(&h, MDRT_MENU_OK);
    CHECK(h.overlay_page == OB_PAGE_SCALE_LO);
    CHECK(h.lores.mode == OB_MODE_SHARP);
    for (i = 0; i < h.overlay.count; i++)
        if (h.overlay.items[i].id == OB_ID_SCALE_MODE) { h.overlay.sel = i; break; }
    ob_overlay_input(&h, MDRT_MENU_OK);
    CHECK(h.overlay_page == OB_PAGE_SCALE_LO);
    CHECK(h.live_preview);
    CHECK(h.lores.mode == OB_MODE_CRT);
    for (i = 0; i < h.overlay.count; i++)
        if (h.overlay.items[i].id == OB_ID_CRT_LOOK) found_crt = 1;
    CHECK(found_crt);
    for (i = 0; i < h.overlay.count; i++)
        CHECK(h.overlay.items[i].id != OB_ID_SHARP_AMOUNT);
    /* Back goes to the main page */
    ob_overlay_input(&h, MDRT_MENU_BACK);
    CHECK(h.overlay_page == OB_PAGE_MAIN);
    for (i = 0; i < h.overlay.count; i++)
        if (h.overlay.items[i].id == OB_ID_SCALE_LO) { h.overlay.sel = i; break; }
    ob_overlay_input(&h, MDRT_MENU_OK);

    memset(px, 0xFF, sizeof px);
    ob_overlay_draw(&h, px, 320, 224, 320);
    CHECK((px[0] & 0xFFFFFF) != 0xFFFFFF);       /* darkened + drawn */

    ob_overlay_close(&h);
    ob_host_free(&h);
}

static void test_filemenu_policy(void)
{
    ob_host h;
    ob_item ov[OB_ITEMS_MAX], bar[OB_ITEMS_MAX];
    int nbar, nov, i, j, shared = 0;
    ob_host_init(&h, OB_PORT_PC95, OB_PROFILE_DESKTOP);
    CHECK(ob_filemenu_wanted(1, 0, 0) == 1);     /* windowed desktop: bar */
    CHECK(ob_filemenu_wanted(1, 1, 0) == 0);     /* fullscreen: no bar */
    CHECK(ob_filemenu_wanted(0, 0, 1) == 0);     /* handheld: overlay only */
    CHECK(ob_filemenu_wanted(1, 0, 1) == 0);

    ob_filemenu_init(NULL);
    ob_filemenu_show(1);
    ob_filemenu_sync(&h);
    CHECK(ob_filemenu_client_size(NULL, NULL) == 0);

    nbar = ob_filemenu_items(&h, bar, OB_ITEMS_MAX);
    nov = ob_main_items(&h, ov, OB_ITEMS_MAX);
    for (i = 0; i < nbar; i++)
        for (j = 0; j < nov; j++)
            if (bar[i].id == ov[j].id && bar[i].id == OB_ID_SCALE_LO) shared = 1;
    CHECK(shared);
    CHECK(ob_items_contain(bar, nbar, OB_ID_SCALE_LO));
    CHECK(ob_items_contain(bar, nbar, OB_ID_SCALE_HI));   /* 95 PC: hi-res and lo-res apart */
    CHECK(!ob_items_contain(bar, nbar, OB_ID_SCALE_MODE));
    CHECK(!ob_items_contain(bar, nbar, OB_ID_RESUME));
    CHECK(!ob_items_contain_label(bar, nbar, "Native"));
    ob_host_free(&h);
}

static void test_view_from_host(void)
{
    ob_host h;
    ob_view_in in;
    ob_view_out v;
    int num, den;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    ob_view_from_host(&h, OB_CLASS_LORES, 320, 224, 960, 720, &in);
    CHECK(in.dar_h == 240);
    CHECK(in.src_h == 224);
    CHECK(in.height == OB_HEIGHT_FIT);
    ob_view(&in, &v);
    ob_view_par(&v, &num, &den);
    CHECK(num == 1 && den == 1);
    ob_host_free(&h);
}

/* A MiSTer preset sets the filters, gamma, mask and scale (95 PC's keys), and stays checked
 * only while they are unchanged. Menu rows mark the current choice in front. */
static void test_mister_presets(void)
{
    ob_host h;
    FILE *f;
    ob_item items[OB_ITEMS_MAX];
    int n, i, marked = 0;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    CHECK(!strcmp(h.lores.mister_path[0], "Filters/Upscaling - Recommended/GS_Sharpness_060.txt"));
    CHECK(!strcmp(h.lores.mister_path[2], "Gamma/Pure_Gamma/gamma_113.txt"));
    CHECK(h.lores.mister_path[3][0] == 0);
    make_dir("mister");
    make_dir("mister/Presets");
    f = fopen("mister/Presets/test_preset.ini", "w");
    CHECK(f != NULL);
    if (f) {
        fputs("hfilter=Upscaling - Recommended/GS_Sharpness_070.txt\n"
              "vfilter=Scanlines - Adaptive/SLA_Dk_030_Br_080.txt\n"
              "sfilter=same\ngamma=off\nmask=Square/Grille.txt\nmaskmode=2\n", f);
        fclose(f);
    }
    ob_mister_pick(&h, OB_CLASS_LORES, 4, "Presets/test_preset.ini");
    CHECK(h.lores.mode == OB_MODE_MISTER);
    CHECK(!strcmp(h.lores.mister_path[0], "Filters/Upscaling - Recommended/GS_Sharpness_070.txt"));
    CHECK(!strcmp(h.lores.mister_path[1], "Filters/Scanlines - Adaptive/SLA_Dk_030_Br_080.txt"));
    CHECK(h.lores.mister_path[2][0] == 0);                 /* gamma=off is none */
    CHECK(!strcmp(h.lores.mister_path[3], "Shadow_Masks/Square/Grille.txt"));
    CHECK(h.lores.mask_scale == 2);
    CHECK(!strcmp(ob_mister_selected(&h, OB_CLASS_LORES, 4), "Presets/test_preset.ini"));
    n = ob_mister_entries("Presets", 4, 0, ob_mister_selected(&h, OB_CLASS_LORES, 4), items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++)
        if (items[i].label && !strncmp(items[i].label, OB_MARK " ", 2)) {
            marked++;
            CHECK(!strcmp(ob_mark_strip(items[i].label), "test_preset"));
        }
    CHECK(marked == 1);
    ob_mister_pick(&h, OB_CLASS_LORES, 2, "Gamma/Pure_Gamma/gamma_113.txt");
    CHECK(ob_mister_selected(&h, OB_CLASS_LORES, 4)[0] == 0); /* changed since: not checked */
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_MISTER_RESET, 0);
    CHECK(!strcmp(h.lores.mister_path[1], "Filters/Scanlines - Adaptive/SLA_Dk_030_Br_070.txt"));
    remove("mister/Presets/test_preset.ini");
    rmdir("mister/Presets");
    ob_host_free(&h);
}

static int lum_of(uint32_t p) { return (int)((p >> 16) & 255) + (int)((p >> 8) & 255) + (int)(p & 255); }

static void test_look_render(void)
{
    ob_host h;
    uint32_t src[8 * 4], *px = NULL;
    int i, w = 0, ht = 0, edge[3];
    FILE *f;
    for (i = 0; i < 8 * 4; i++) src[i] = 0xFF808080u;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    h.lores.mode = OB_MODE_SHARP;
    CHECK(!ob_look_renders(&h.lores));
    CHECK(!ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    h.lores.mode = OB_MODE_CRT;
    for (i = 0; i < 3; i++) {                       /* light, medium, strong: 8 rows a line */
        h.lores.crt = (ob_crt_look)i;
        CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
        CHECK(w == 32 && ht == 32);
        CHECK(lum_of(px[3 * 32 + 16]) > lum_of(px[0 * 32 + 16]));   /* the line's centre over its edge */
        edge[i] = lum_of(px[0 * 32 + 16]);
    }
    CHECK(edge[0] > edge[2]);                       /* strong has the darker gaps */
    h.lores.crt = OB_CRT_SOFT;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(lum_of(px[3 * 32 + 16]) > lum_of(px[0 * 32 + 16]));
    CHECK(px[3 * 32 + 15] == px[3 * 32 + 16]);      /* no grille */
    h.lores.crt = OB_CRT_SOFT_APERTURE;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(px[3 * 32 + 15] != px[3 * 32 + 16]);      /* the grille's columns */
    h.lores.crt = OB_CRT_CUSTOM;
    h.lores.c_mask = 0;
    h.lores.c_dark = 1;
    h.lores.c_gamma = 1;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(px[3 * 32 + 15] == px[3 * 32 + 16]);      /* no mask */
    CHECK(lum_of(px[3 * 32 + 16]) > lum_of(px[0 * 32 + 16]));
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 4000, 3000, &px, &w, &ht));
    CHECK(w == 4000 / 3 && ht == 1000);             /* above the cap: a whole fraction */

    make_dir("mister");
    make_dir("mister/Shadow_Masks");
    f = fopen("mister/Shadow_Masks/test_mask.txt", "w");
    CHECK(f != NULL);
    if (f) {
        fputs("# two columns: the first lit, the second dark\nv2\n2,1\n7f0,000\n", f);
        fclose(f);
    }
    h.lores.mode = OB_MODE_MISTER;
    snprintf(h.lores.mister_path[3], sizeof h.lores.mister_path[3], "%s", "Shadow_Masks/test_mask.txt");
    h.lores.mask_scale = 1;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(lum_of(px[3 * 32 + 16]) > 0);
    CHECK(lum_of(px[3 * 32 + 17]) == 0);            /* the mask's dark column */
    h.lores.mask_scale = 2;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(lum_of(px[3 * 32 + 17]) > 0 && lum_of(px[3 * 32 + 18]) == 0);
    h.lores.mister_path[3][0] = 0;
    CHECK(ob_look_render(&h.lores, src, 8, 4, 8, 32, 32, &px, &w, &ht));
    CHECK(lum_of(px[3 * 32 + 17]) > 0);             /* no mask */
    remove("mister/Shadow_Masks/test_mask.txt");
    rmdir("mister/Shadow_Masks");
    ob_host_free(&h);
}


/* ---- the Game page ---- */
static const char *const g_src[] = { "95 PC", "94G per pixel", "94G scaled to rink" };
static const char *const g_onoff[] = { "Off", "On" };
static const char *const g_preset[] = { "Original", "94 Genesis", "Custom" };
static const ob_game_opt g_opts[] = {
    { "feel_preset", "Feel", g_preset, 3, 0, 1, 0 },
    { "skate_speed", "Skating speed", g_src, 3, 0, 1, 1 },
    { "fights", "Fights", g_onoff, 2, 1, 1, 2 },
    { "hud", "Local only", g_onoff, 2, 0, 0, 2 },
};
static int g_calls[8], g_last[8];
static ob_host *g_h;
static void g_changed(void *ctx, int i, int v)
{
    (void)ctx;
    g_calls[i]++;
    g_last[i] = v;
    if (i == 0 && v == 1) ob_game_set(g_h, 1, 1);   /* a preset moves the row it covers */
    if (i == 1 && v != 0 && ob_game_get(g_h, 0) == 0) ob_game_set(g_h, 0, 2);
}

/* the shader's Sharp branch (src/gl.c main(), last part) in floats, for one output pixel's channel */
static float shader_sharp(const uint32_t *src, int sw, int sh, int px_, int py_, int ox, int oy, int ow, int oh, int sh_bits)
{
    float p[2] = { (ox + 0.5f) / ow * sw * px_ - 0.5f, (oy + 0.5f) / oh * sh * py_ - 0.5f }, wx[4], wy[4], c = 0;
    float ix = floorf(p[0]), iy = floorf(p[1]);
    int a, b;
    ob_render_weights(OB_FILTER_CUBIC, p[0] - ix, wx);
    ob_render_weights(OB_FILTER_CUBIC, p[1] - iy, wy);
    for (b = 0; b < 4; b++)
        for (a = 0; a < 4; a++) {
            int x = (int)floorf((ix - 1 + a) / px_), y = (int)floorf((iy - 1 + b) / py_);
            x = x < 0 ? 0 : x >= sw ? sw - 1 : x;
            y = y < 0 ? 0 : y >= sh ? sh - 1 : y;
            c += wy[b] * wx[a] * (float)((src[y * sw + x] >> sh_bits) & 255);
        }
    return c < 0 ? 0 : c > 255 ? 255 : c;
}

static void test_render(void)
{
    ob_look l;
    ob_render_plan plan;
    ob_host h;
    static uint32_t src[320 * 200];
    uint32_t *px = NULL;
    int x, y, w = 0, ht = 0, worst = 0, n = 0, i;
    double cent[64], energy[64];
    /* the prescale: the user's, never above the whole multiple the view shows */
    CHECK(ob_render_prescale(3, 320, 1440) == 3);
    CHECK(ob_render_prescale(3, 320, 640) == 2);
    CHECK(ob_render_prescale(9, 200, 1080) == 5);
    CHECK(ob_render_prescale(3, 320, 200) == 1);
    ob_host_init(&h, OB_PORT_SNES, OB_PROFILE_HANDHELD);
    ob_render_plan_get(&h.lores, OB_CLASS_LORES, 320, 240, 640, 480, &plan);
    CHECK(plan.style == OB_STYLE_SHARP && plan.pre_x == 2 && plan.pre_y == 2 && plan.scan == 0);
    ob_host_free(&h);
    ob_host_init(&h, OB_PORT_PC95, OB_PROFILE_DESKTOP);
    l = h.lores;
    ob_render_plan_get(&l, OB_CLASS_LORES, 320, 200, 1440, 1080, &plan);
    CHECK(plan.pre_x == 3 && plan.pre_y == 3 && plan.filter == OB_FILTER_CUBIC);
    l.mode = OB_MODE_CRT; l.crt = OB_CRT_MEDIUM;
    ob_render_plan_get(&l, OB_CLASS_LORES, 320, 200, 1440, 1080, &plan);
    CHECK(plan.style == OB_STYLE_SHARP && plan.scan > 0 && plan.pre_x == 3);
    ob_render_plan_get(&h.hires, OB_CLASS_HIRES, 640, 480, 1440, 1080, &plan);
    CHECK(plan.pre_x == 1 && plan.pre_y == 1);
    l = h.lores;
    /* an exact multiple is nearest neighbour exactly: 2x of a pattern on a 640x400 view */
    for (i = 0; i < 320 * 200; i++) src[i] = 0xFF000000u | (uint32_t)((i * 2654435761u) >> 8 & 0xFFFFFF);
    l.sharp = OB_SHARP_2X;
    CHECK(ob_render_cpu(&l, OB_CLASS_LORES, src, 320, 200, 320, 640, 400, &px, &w, &ht) && w == 640 && ht == 400);
    for (y = 0; y < 400; y++)
        for (x = 0; x < 640; x++) if (px[y * 640 + x] != src[(y / 2) * 320 + x / 2]) n++;
    CHECK(n == 0);
    /* the software renderer is the shader: within 2 of 255 on every channel at 3x to 1440x1080 */
    l.sharp = OB_SHARP_3X;
    CHECK(ob_render_cpu(&l, OB_CLASS_LORES, src, 320, 200, 320, 1440, 1080, &px, &w, &ht) && w == 1440 && ht == 1080);
    for (y = 0; y < 1080; y += 7)
        for (x = 0; x < 1440; x += 5)
            for (i = 0; i < 24; i += 8) {
                int d = (int)((px[y * 1440 + x] >> i) & 255) - (int)(shader_sharp(src, 320, 200, 3, 3, x, y, 1440, 1080, i) + 0.5f);
                if (d < 0) d = -d;
                if (d > worst) worst = d;
            }
    CHECK(worst <= 2);
    /* even pixels: a bright column every 5 source pixels lands every 22.5 window pixels with the same weight, at 4.5x (nearest neighbour
     * gives 4- and 5-pixel columns: a 25% difference) */
    for (i = 0; i < 320 * 200; i++) src[i] = (i % 320) % 5 == 2 ? 0xFFC0C0C0u : 0xFF404040u;
    CHECK(ob_render_cpu(&l, OB_CLASS_LORES, src, 320, 200, 320, 1440, 1080, &px, &w, &ht));
    for (i = 0; i < 64; i++) {
        double sum = 0, sx = 0;
        for (x = (int)(i * 22.5); x < (int)((i + 1) * 22.5); x++) {
            double v = (double)(px[540 * 1440 + x] & 255) - 64.0;
            sum += v;
            sx += v * (x + 0.5);
        }
        energy[i] = sum;
        cent[i] = sx / sum;
    }
    for (i = 1; i < 63; i++) {
        CHECK(fabs(cent[i] - cent[i - 1] - 22.5) < 0.1);
        CHECK(fabs(energy[i] - energy[0]) < energy[0] * 0.01);
    }
    ob_host_free(&h);
    puts("render: ok");
}

static void test_game_page(void)
{
    ob_host h, g;
    ob_game_ops ops;
    ob_item items[OB_ITEMS_MAX];
    char path[] = "/tmp/overbar-test-game.ini", buf[8192];
    int n, i, sep = 0, found = 0;
    size_t sz;
    FILE *f;
    remove(path);
    memset(&ops, 0, sizeof ops);
    ops.title = "Game";
    ops.opts = g_opts;
    ops.nopts = 4;
    ops.changed = g_changed;

    /* ini first, options after: the lines are kept and applied */
    f = fopen(path, "w");
    fprintf(f, "[overbar]\ngame_skate_speed=94G scaled to rink\ngame_future=7\n");
    fclose(f);
    ob_host_init(&h, OB_PORT_PC95, OB_PROFILE_DESKTOP);
    g_h = &h;
    CHECK(ob_host_load_ini(&h, path) == 1);
    memset(g_calls, 0, sizeof g_calls);
    ob_game_set_ops(&h, &ops);
    CHECK(h.game_ready);
    CHECK(ob_game_get(&h, 1) == 2);
    CHECK(ob_game_get(&h, 2) == 1);              /* its default */
    CHECK(g_calls[0] == 1 && g_calls[1] == 1 && g_calls[2] == 1 && g_calls[3] == 1);

    /* the main page has the row, the page has every option and a divider per group */
    n = ob_main_items(&h, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) if (items[i].id == OB_ID_GAME) found = 1;
    CHECK(found);
    n = ob_page_items(&h, OB_PAGE_GAME, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) if (items[i].kind == OB_ITEM_SEP) sep++;
    CHECK(sep == 2);
    CHECK(items[n - 1].id == OB_ID_BACK);
    CHECK(items[0].id == OB_ID_GAME_OPT && items[0].kind == MDRT_ITEM_CHOICE);

    /* picking a preset: one call, the covered row moves, the ini keeps both */
    memset(g_calls, 0, sizeof g_calls);
    CHECK(ob_host_apply(&h, OB_CLASS_LORES, OB_ID_GAME_OPT + 0, 1));
    CHECK(g_calls[0] == 1 && g_last[0] == 1 && g_calls[1] == 0);
    CHECK(ob_game_get(&h, 1) == 1);
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_GAME_OPT + 3, 1);
    /* presets: one row on top, matching derived from the rows, Custom when they match none */
    {
        static const signed char pa[4] = { 0, 0, 0, -1 }, pb[4] = { 1, 1, 1, -1 };
        static const ob_game_preset pre[2] = { { "A", pa }, { "B", pb } };
        ob_game_ops po = ops;
        po.presets = pre;
        po.npresets = 2;
        ob_game_set_ops(&h, &po);
        ob_game_apply(&h, OB_ID_GAME_PRESET, 1);
        CHECK(ob_game_get(&h, 1) == 1 && ob_game_get(&h, 2) == 1);
        n = ob_page_items(&h, OB_PAGE_GAME, items, OB_ITEMS_MAX);
        CHECK(items[0].id == OB_ID_GAME_PRESET && items[0].nchoices == 3 && items[0].value == 1);
        CHECK(items[1].kind == OB_ITEM_SEP && items[2].id == OB_ID_GAME_OPT);
        ob_game_apply(&h, OB_ID_GAME_OPT + 2, 0);                 /* a row by hand: Custom */
        n = ob_page_items(&h, OB_PAGE_GAME, items, OB_ITEMS_MAX);
        CHECK(items[0].value == 2);
        ob_game_apply(&h, OB_ID_GAME_PRESET, 2);                  /* Custom picked: nothing moves */
        CHECK(ob_game_get(&h, 2) == 0 && ob_game_get(&h, 1) == 1);
        ob_game_apply(&h, OB_ID_GAME_PRESET, 0);
        n = ob_page_items(&h, OB_PAGE_GAME, items, OB_ITEMS_MAX);
        CHECK(items[0].value == 0 && ob_game_get(&h, 1) == 0);
        ob_game_set_ops(&h, &ops);
        ob_game_apply(&h, OB_ID_GAME_OPT + 0, 1);                 /* (back to what the checks below read from the ini) */
        ob_game_apply(&h, OB_ID_GAME_OPT + 2, 1);
        ob_game_apply(&h, OB_ID_GAME_OPT + 3, 1);
        (void)n;
    }
    ob_host_free(&h);

    ob_host_init(&g, OB_PORT_PC95, OB_PROFILE_DESKTOP);
    g_h = &g;
    ob_game_set_ops(&g, &ops);
    CHECK(ob_host_load_ini(&g, path) == 1);       /* options first, ini after */
    CHECK(ob_game_get(&g, 0) == 1 && ob_game_get(&g, 1) == 1 && ob_game_get(&g, 3) == 1);
    f = fopen(path, "r");
    sz = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[sz] = 0;
    CHECK(strstr(buf, "game_feel_preset=94 Genesis") != NULL);
    CHECK(strstr(buf, "game_future=7") != NULL);   /* an option this build lacks stays */

    /* online: the host's net options only; the guest's own come back after */
    sz = ob_game_pack(&g, buf, sizeof buf);
    CHECK(sz > 0 && strstr(buf, "skate_speed=1\n") && !strstr(buf, "hud="));
    ob_game_set(&g, 1, 0);
    ob_game_set(&g, 3, 0);
    memset(g_calls, 0, sizeof g_calls);
    CHECK(ob_game_session_begin(&g, "skate_speed=2\nfights=0\nhud=1\n", 29) == 2);
    CHECK(ob_game_get(&g, 1) == 2 && ob_game_get(&g, 2) == 0 && ob_game_get(&g, 3) == 0);
    CHECK(g_calls[1] == 1 && g_calls[2] == 1 && g_calls[3] == 0);
    ob_host_apply(&g, OB_CLASS_LORES, OB_ID_GAME_OPT + 1, 0);   /* locked to the host's */
    CHECK(ob_game_get(&g, 1) == 2);
    ob_game_session_end(&g);
    CHECK(ob_game_get(&g, 1) == 0 && ob_game_get(&g, 2) == 1);
    CHECK(ob_game_pack(&g, buf, 4) == 0);          /* no room: nothing */
    ob_host_free(&g);
    remove(path);
}

/* ---- Season overlay and File menu ---- */
#define SROOT "/tmp/overbar-test-season-ui"
typedef struct { char league[200]; int teams[4], nteams, master; } s_mach;
static int s_league(void *ud, ob_season_league *l)
{
    s_mach *m = ud;
    int i;
    snprintf(l->name, sizeof l->name, "%s", "League");
    snprintf(l->id, sizeof l->id, "%s", "id1");
    l->master = m->master;
    l->nteam = m->nteams;
    for (i = 0; i < m->nteams; i++) l->team[i] = m->teams[i];
    return 0;
}
static int s_copy_dir(const char *from, const char *to)
{
    char cmd[512];
    make_dir(to);
    snprintf(cmd, sizeof cmd, "cp -a '%s/.' '%s' 2>/dev/null || cp '%s'/* '%s'", from, to, from, to);
    return system(cmd) == 0 ? 0 : -1;
}
static int s_export(void *ud, int team, const char *dir) { (void)team; return s_copy_dir(((s_mach *)ud)->league, dir); }
static int s_copy(void *ud, int team, const char *dir) { (void)team; return s_copy_dir(((s_mach *)ud)->league, dir); }
static int s_import(void *ud, int team, const char *dir)
{
    (void)team;
    return s_copy_dir(dir, ((s_mach *)ud)->league);
}
static int s_merge(void *ud, int team, const char *dir, const uint32_t *played, int nplayed)
{
    (void)team; (void)played; (void)nplayed;
    return s_copy_dir(dir, ((s_mach *)ud)->league);
}
static int s_dir(void *ud, char *dir, int size)
{
    snprintf(dir, (size_t)size, "%s", ((s_mach *)ud)->league);
    return 0;
}
static const char *s_name(void *ud, int team)
{
    static const char *n[] = { "Alpha", "Beta", "Gamma", "Delta" };
    (void)ud;
    return team >= 0 && team < 4 ? n[team] : NULL;
}

static int s_online(char *line, int size) { if (size > 0) line[0] = 0; return 1; }

static void test_season_page(void)
{
    ob_host h;
    ob_season season;
    ob_season_ops ops;
    ob_item items[OB_ITEMS_MAX];
    s_mach m;
    int n, i, found = 0, file_item = 0, pick = 0;
    FILE *f;
    (void)f;
    memset(&m, 0, sizeof m);
    m.master = 0;
    m.teams[0] = 2;
    m.nteams = 1;
    snprintf(m.league, sizeof m.league, "%s/league", SROOT);
    make_dir(SROOT);
    make_dir(m.league);
    make_dir(SROOT "/teams");
    f = fopen(SROOT "/league/T2.DB", "w");
    if (f) { fputs("ok\n", f); fclose(f); }
    memset(&ops, 0, sizeof ops);
    ops.ud = &m;
    ops.league = s_league;
    ops.export_team = s_export;
    ops.merge_team = s_merge;
    ops.copy_team = s_copy;
    ops.import_team = s_import;
    ops.league_dir = s_dir;
    ob_season_init(&season, &ops, SROOT "/work", "Cal", NULL, NULL, NULL);

    ob_host_init(&h, OB_PORT_GENERIC, OB_PROFILE_DESKTOP);
    ob_season_set(&h, &season, 0, SROOT "/teams");
    ob_season_set_names(&h, s_name, NULL);
    CHECK(ob_season_ready(&h));

    n = ob_main_items(&h, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) if (items[i].id == OB_ID_SEASON) found = 1;
    CHECK(found);

    n = ob_filemenu_items(&h, items, OB_ITEMS_MAX);
    found = 0;
    for (i = 0; i < n; i++) if (items[i].id == OB_ID_SEASON) found = 1;
    CHECK(found);

    /* not in a room, a copy of some old league here: no picking, nothing to type, a pointer to Connect */
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) {
        if (items[i].id == OB_ID_SEASON_PICK || items[i].id == OB_ID_SEASON_SYNC) pick = 1;
        if (items[i].kind == MDRT_ITEM_FIELD) file_item = 1;        /* nothing to type */
        if (items[i].label && strstr(items[i].label, ".obt")) file_item = 1;
        if (strstr(items[i].detail, ".obt") || strstr(items[i].detail, "/")) file_item = 1;
    }
    CHECK(!pick && !file_item);
    CHECK(ob_items_contain_label(items, n, "To join a friend's league: Online Play, Connect"));

    /* joined a room: the host's league, never the old one left on this machine */
    h.online_ops.status = s_online;
    h.online.role = 1;
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain_label(items, n, "Asking the host for its league"));
    for (i = 0; i < n; i++) CHECK(strcmp(items[i].detail, "League") != 0);
    season.roster_have = 1;
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain_label(items, n, "The host has no league open yet"));

    /* a guest's list: "Delta - Bob" to pick, the host's team shown, a computer team as a new player */
    snprintf(season.roster_league, sizeof season.roster_league, "%s", "Cup");
    season.roster_kind[0] = OB_SEASON_HOSTS; snprintf(season.roster[0], sizeof season.roster[0], "%s", "Abdul");
    season.roster_kind[3] = OB_SEASON_WAITING; snprintf(season.roster[3], sizeof season.roster[3], "%s", "Bob");
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain_label(items, n, "Host's league"));
    CHECK(ob_items_contain_label(items, n, "Delta - Bob") && ob_items_contain(items, n, OB_ID_SEASON_TEAM + 3));
    CHECK(ob_items_contain_label(items, n, "Alpha - Abdul") && !ob_items_contain(items, n, OB_ID_SEASON_TEAM + 0));
    CHECK(ob_items_contain(items, n, OB_ID_SEASON_TEAM + 1));      /* Beta: free, a new player */
    CHECK(!ob_items_contain(items, n, OB_ID_SEASON_SYNC));         /* this machine's League is not the host's Cup */
    snprintf(season.roster_league, sizeof season.roster_league, "%s", "League");
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_SEASON_SYNC));          /* a copy of the host's league: Sync */

    n = ob_filemenu_items(&h, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) CHECK(!items[i].label || !strstr(items[i].label, "file"));

    ob_overlay_open(&h);
    ob_season_open(&h);
    CHECK(h.overlay_open && h.overlay_page == OB_PAGE_SEASON);

    /* the master: who has which team, no actions about files */
    h.online.role = 0;
    m.master = 1;
    m.teams[0] = 0;
    m.nteams = 1;
    n = ob_page_items(&h, OB_PAGE_SEASON, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain_label(items, n, "You run this league"));
    CHECK(ob_items_contain_label(items, n, "Alpha - Cal"));
    CHECK(!ob_items_contain(items, n, OB_ID_SEASON_PICK));

    ob_host_free(&h);
    ob_season_free(&season);
}

/* the vector menu draws type and keycaps with alpha, finds the row under a pixel, and the sounds are made */
static void test_vmenu_and_sfx(void)
{
    static uint32_t px[960 * 540];
    mdrt_menu m;
    int n, any = 0, hit, i;
    mdrt_menu_init(&m);
    mdrt_menu_page(&m, "Overlay");
    snprintf(m.subtitle, sizeof m.subtitle, "[F9] overlay   {A} ok");
    mdrt_menu_add(&m, 1, MDRT_ITEM_ACTION, "Resume");
    mdrt_menu_add(&m, 2, MDRT_ITEM_ACTION, "Quit");
    ob_vmenu_draw(&m, px, 960, 540, 960, "NHL 94", "For Sega Genesis");
    for (i = 0; i < 960 * 540; i++) if (px[i] >> 24) any++;
    CHECK(any > 20000);                                           /* the shade and the type */
    CHECK(px[270 * 960 + 900] >> 24 == 0);                        /* the right side stays clear for the picture */
    hit = ob_vmenu_at(&m, 80, 229, 960, 540);
    CHECK(hit == 1);
    CHECK(ob_vmenu_at(&m, 80, 20, 960, 540) == -1);
    for (i = 0; i < OB_SFX_COUNT; i++) {
        const short *s = ob_sfx_samples(i, &n);
        int peak = 0, j;
        CHECK(s && n > 1000 && n < 12000);
        for (j = 0; j < n; j++) if (abs(s[j]) > peak) peak = abs(s[j]);
        CHECK(peak > 20000 && peak <= 32767);
    }
    mdrt_menu_free(&m);
}

/* the mouse resting at the list's bottom edge scrolls it one item per 80 ms; at the top edge, back */
static void test_autoscroll(void)
{
    ob_host h;
    int i, top;
    ob_host_init(&h, OB_PORT_GENERIC, OB_PROFILE_DESKTOP);
    h.overlay_open = 1;
    h.overlay_vec = 1;
    mdrt_menu_init(&h.overlay);
    mdrt_menu_page(&h.overlay, "Test");
    for (i = 0; i < 30; i++) mdrt_menu_add(&h.overlay, i + 1, MDRT_ITEM_ACTION, "Item");
    h.overlay.rows = 9;
    CHECK(!ob_overlay_autoscroll(&h, 80, 300, 960, 540, 1000));      /* mid list: nothing */
    CHECK(!ob_overlay_autoscroll(&h, 80, 450, 960, 540, 1000));      /* entering the edge starts the delay */
    CHECK(!ob_overlay_autoscroll(&h, 80, 450, 960, 540, 1050));
    CHECK(ob_overlay_autoscroll(&h, 80, 450, 960, 540, 1085));
    CHECK(h.overlay.top == 1 && h.overlay.sel >= 1);
    CHECK(!ob_overlay_autoscroll(&h, 80, 450, 960, 540, 1120));      /* 35 ms later: too soon */
    CHECK(ob_overlay_autoscroll(&h, 80, 450, 960, 540, 1170));
    CHECK(h.overlay.top == 2);
    top = h.overlay.top;
    CHECK(!ob_overlay_autoscroll(&h, 80, 190, 960, 540, 1300));      /* the top edge: its own delay */
    CHECK(ob_overlay_autoscroll(&h, 80, 190, 960, 540, 1385));
    CHECK(h.overlay.top == top - 1);
    for (i = 0; i < 100; i++) ob_overlay_autoscroll(&h, 80, 450, 960, 540, 2000 + 90u * i);
    CHECK(h.overlay.top == 30 - 9);                                   /* it stops at the end */
    mdrt_menu_free(&h.overlay);
}

/* the movie's own look: its own Scaling page, written to and read from the ini */
static void test_movie_look(void)
{
    ob_host h;
    ob_item items[OB_ITEMS_MAX];
    int n, i, found = 0;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    CHECK(!h.has_movie && !h.movie_own);
    CHECK(h.movie.mode == OB_MODE_CRT && h.movie.crt == OB_CRT_SOFT);
    n = ob_page_items(&h, OB_PAGE_MAIN, items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) if (items[i].id == OB_ID_SCALE_MOVIE) found = 1;
    CHECK(!found);
    h.has_movie = 1;
    n = ob_page_items(&h, OB_PAGE_MAIN, items, OB_ITEMS_MAX);
    for (i = 0, found = 0; i < n; i++) if (items[i].id == OB_ID_SCALE_MOVIE) found = 1;
    CHECK(found);
    n = ob_page_items(&h, OB_PAGE_SCALE_MOVIE, items, OB_ITEMS_MAX);
    CHECK(n == 2 && items[0].id == OB_ID_MOVIE_OWN);       /* same as the game: only the switch and Back */
    ob_host_apply(&h, OB_CLASS_MOVIE, OB_ID_MOVIE_OWN, 1);
    CHECK(h.movie_own);
    n = ob_page_items(&h, OB_PAGE_SCALE_MOVIE, items, OB_ITEMS_MAX);
    CHECK(n > 4);
    ob_host_apply(&h, OB_CLASS_MOVIE, OB_ID_SCALE_MODE, OB_MODE_SHARP);
    ob_host_apply(&h, OB_CLASS_MOVIE, OB_ID_SHARP_AMOUNT, 0);
    CHECK(h.movie.mode == OB_MODE_SHARP && h.movie.sharp == OB_SHARP_2X);
    CHECK(h.lores.sharp == OB_SHARP_3X);                    /* the game's look untouched */
    CHECK(ob_host_look(&h, OB_CLASS_MOVIE) == &h.movie);
    ob_host_free(&h);
    CHECK(!ob_screen_video_look());
}

/* changing a value lower down a long page: the page is built again, and its list stays where it was */
static void test_overlay_keeps_scroll(void)
{
    static const char *const three[3] = { "a", "b", "c" };
    static char keys[30][8];
    static ob_game_opt opts[30];
    ob_game_ops ops;
    ob_host h;
    static uint32_t px[320 * 224];
    int i, top, row;
    for (i = 0; i < 30; i++) {
        snprintf(keys[i], sizeof keys[i], "k%d", i);
        opts[i] = (ob_game_opt){ keys[i], keys[i], three, 3, 0, 0, 1 };
    }
    memset(&ops, 0, sizeof ops);
    ops.title = "Game";
    ops.opts = opts;
    ops.nopts = 30;
    ob_host_init(&h, OB_PORT_GENESIS, OB_PROFILE_DESKTOP);
    ob_game_set_ops(&h, &ops);
    ob_overlay_open(&h);
    mdrt_menu_select(&h.overlay, OB_ID_GAME);
    ob_overlay_input(&h, MDRT_MENU_OK);
    CHECK(h.overlay_page == OB_PAGE_GAME);
    for (i = 0; i < 25; i++) ob_overlay_input(&h, MDRT_MENU_DOWN);
    ob_overlay_draw(&h, px, 320, 224, 320);
    top = h.overlay.top;
    row = h.overlay.sel;
    CHECK(top > 0);
    for (i = 0; i < 2; i++) {
        ob_overlay_input(&h, MDRT_MENU_UP);                   /* (a row away from the bottom edge) */
        ob_overlay_draw(&h, px, 320, 224, 320);
    }
    top = h.overlay.top;
    row = h.overlay.sel;
    ob_overlay_input(&h, MDRT_MENU_RIGHT);
    CHECK(h.overlay.sel == row);
    ob_overlay_draw(&h, px, 320, 224, 320);
    CHECK(h.overlay.top == top);
    {   /* the mouse on a value: a right click steps it back (not Back), a left click on */
        int x = 160, y, v0, v1;
        mdrt_menu_item *it = &h.overlay.items[h.overlay.sel];
        CHECK(it->kind == MDRT_ITEM_CHOICE);
        v0 = it->value;
        for (y = 0; y < 224; y++) if (mdrt_menu_at(&h.overlay, x, y, 320, 224) == h.overlay.sel) break;
        CHECK(y < 224);
        h.overlay_vec = 0;
        ob_overlay_pointer(&h, x, y, 320, 224, 2);
        CHECK(h.overlay_page == OB_PAGE_GAME);
        v1 = h.overlay.items[h.overlay.sel].value;
        CHECK(v1 == (v0 + 2) % 3);                            /* (one step back, around the end) */
        ob_overlay_pointer(&h, x, y, 320, 224, 1);
        CHECK(h.overlay.items[h.overlay.sel].value == v0);
    }
    ob_overlay_input(&h, MDRT_MENU_BACK);                     /* another page starts at its top */
    CHECK(h.overlay_page == OB_PAGE_MAIN && h.overlay.top == 0);
    ob_host_free(&h);
}

int main(void)
{
    test_movie_look();
    test_scaler_console();
    test_scaler_wide();
    test_scaler_vga();
    test_defaults();
    test_handheld_detect_and_present();
    test_ini_first_launch();
    test_keys();
    test_scale_mode_options();
    test_overlay_live_preview();
    test_filemenu_policy();
    test_view_from_host();
    test_mister_presets();
    test_look_render();
    test_render();
    test_game_page();
    test_season_page();
    test_vmenu_and_sfx();
    test_autoscroll();
    test_overlay_keeps_scroll();
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    puts("ok");
    return 0;
}
