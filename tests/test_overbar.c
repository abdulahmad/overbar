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
    CHECK(d.hires.mode == OB_MODE_SHARP && d.hires.sharp == OB_SHARP_3X);
    CHECK(d.lores.mode == OB_MODE_SHARP && d.lores.sharp == OB_SHARP_3X);
    CHECK(d.lores.height == OB_HEIGHT_FIT && d.hires.height == OB_HEIGHT_FIT);
    CHECK(d.has_hires);
    CHECK(d.present == OB_PRESENT_GL);
    ob_host_free(&d);

    ob_host_init(&hh, OB_PORT_SNES, OB_PROFILE_HANDHELD);
    CHECK(hh.refresh == OB_REFRESH_60);
    CHECK(hh.vsync == 1);
    CHECK(hh.stats == 0);
    CHECK(hh.lores.mode == OB_MODE_SHARP && hh.lores.sharp == OB_SHARP_3X);
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
    CHECK(!ob_look_supported(OB_MODE_MISTER, OB_PRESENT_SDL));
    CHECK(ob_look_supported(OB_MODE_MISTER, OB_PRESENT_GLES2));
    CHECK(ob_look_fallback_note(OB_MODE_CRT, OB_PRESENT_SDL) == NULL);
    CHECK(ob_look_fallback_note(OB_MODE_MISTER, OB_PRESENT_SDL) != NULL);
    CHECK(ob_look_fallback_note(OB_MODE_SHARP, OB_PRESENT_SDL) == NULL);

    ob_host_init(&h, OB_PORT_SNES, OB_PROFILE_HANDHELD);
    CHECK(h.refresh == OB_REFRESH_60);
    CHECK(h.vsync == 1);
    CHECK(h.present == OB_PRESENT_SDL);
    CHECK(ob_filemenu_wanted(1, 0, 1) == 0);
    ob_host_apply(&h, OB_CLASS_LORES, OB_ID_SCALE_MODE, OB_MODE_MISTER);
    CHECK(h.fallback_note != NULL);
    n = ob_page_items(&h, OB_PAGE_OPTS_LO, items, OB_ITEMS_MAX);
    CHECK(ob_items_contain(items, n, OB_ID_PRESENT_NOTE));
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
    remove(path);
}

static void test_keys(void)
{
    ob_host h;
    ob_host_init(&h, OB_PORT_GENERIC, OB_PROFILE_DESKTOP);
    CHECK(ob_map_key(OB_SCANCODE_GRAVE) == OB_HOSTKEY_OVERLAY);
    CHECK(ob_map_key(OB_SCANCODE_F11) == OB_HOSTKEY_OVERLAY);
    CHECK(ob_map_key(OB_SCANCODE_F12) == OB_HOSTKEY_STATS);
    CHECK(ob_map_pad_back() == OB_HOSTKEY_OVERLAY);
    CHECK(ob_map_key(41) == OB_HOSTKEY_NONE);     /* Esc is not overlay */
    CHECK(ob_overlay_toggle_key(&h, OB_SCANCODE_GRAVE));
    CHECK(h.overlay_open);
    CHECK(ob_overlay_toggle_key(&h, OB_SCANCODE_F11));
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

int main(void)
{
    test_scaler_console();
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
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    puts("ok");
    return 0;
}
