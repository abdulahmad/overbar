/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * First-launch defaults (L15), ini, overlay keys (L13).
 */
#include "overbar/overbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_name[OB_MODE_COUNT] = {
    "sharp", "crt", "mister", "retroarch",
    "scale2x", "scale3x", "mmpx2x", "mmpx4x", "xbr2x", "xbr3x", "xbr4x", "scale4x"
};
static const char *mode_label[OB_MODE_COUNT] = {
    "Sharp", "CRT Monitor", "MiSTer", "RetroArch",
    "Scale2x 2x", "Scale2x 3x", "MMPX 2x", "MMPX 4x", "xBR 2x", "xBR 3x", "xBR 4x", "Scale2x 4x"
};
static const char *refresh_name[3] = { "60", "70", "vrr" };
static const char *pointer_name[3] = { "auto", "show", "hide" };
static const char *net_role_name[3] = { "host", "join", "both" };
static const char *height_name[4] = { "fit", "integer", "overscan", "auto-crop" };
static const char *height_label[4] = {
    "Fit", "Integer", "Integer+Overscan", "Auto Crop"
};

static int name_index(const char *v, const char **names, int n, int def)
{
    int i;
    for (i = 0; i < n; i++) if (v && !strcmp(v, names[i])) return i;
    return def;
}

static void caps_from_port(ob_host *h)
{
    h->has_hires = (h->port == OB_PORT_PC95 || h->port == OB_PORT_PC96);
    h->has_sound = 1;
    h->console_dar = (h->port == OB_PORT_GENESIS || h->port == OB_PORT_SNES
                      || h->port == OB_PORT_GENERIC);
}

/* The 95 PC SDL port's MiSTer defaults. Paths are under mister/; an empty one is none. */
void ob_mister_defaults(ob_look *l)
{
    memset(l->mister_path, 0, sizeof l->mister_path);
    snprintf(l->mister_path[0], sizeof l->mister_path[0], "%s",
             "Filters/Upscaling - Recommended/GS_Sharpness_060.txt");
    snprintf(l->mister_path[1], sizeof l->mister_path[1], "%s",
             "Filters/Scanlines - Adaptive/SLA_Dk_030_Br_070.txt");
    snprintf(l->mister_path[2], sizeof l->mister_path[2], "%s", "Gamma/Pure_Gamma/gamma_113.txt");
    l->mask_scale = 1;
}

static void look_init(ob_look *l, int console, int lores)
{
    memset(l, 0, sizeof *l);
    l->mode = OB_MODE_SHARP;
    l->crt = OB_CRT_LIGHT;
    l->height = OB_HEIGHT_FIT;
    l->khz31 = 0;
    l->mask_scale = 1;
    l->bright = 100;
    l->c_dark = 0.30f;
    l->c_gamma = 1.13f;
    l->c_mask = 0.12f;
    l->c_sharp = 0.60f;
    l->sharp = OB_SHARP_3X;
    ob_mister_defaults(l);
    (void)console;
    (void)lores;
}

void ob_defaults_apply(ob_host *h, ob_profile profile)
{
    int console = h->console_dar;
    h->profile = profile;
    look_init(&h->lores, console, 1);
    look_init(&h->hires, 0, 0);
    h->stats = 0;
    h->volume = 80;
    h->mute = 0;
    h->replay_secs_index = 2;   /* 20 s, PulseMan README default */
    memset(&h->online, 0, sizeof h->online);
    h->online.port = OB_NET_PORT_DEFAULT;
    h->online.delay = 2;
    h->online.here = 1;
    h->online.all = 2;
    h->gif_scale = 0;
    h->recording = 0;
    h->live_preview = 1;
    if (profile == OB_PROFILE_HANDHELD) {
        h->refresh = OB_REFRESH_60;
        h->vsync = 1;
    } else {
        h->refresh = OB_REFRESH_VRR;
        h->vsync = 0;
    }
}

void ob_host_init(ob_host *h, ob_port port, ob_profile profile)
{
    memset(h, 0, sizeof *h);
    h->port = port;
    caps_from_port(h);
    ob_defaults_apply(h, profile);
    h->present = ob_present_pick(profile, 0, profile != OB_PROFILE_HANDHELD);
    h->fallback_note = NULL;
    mdrt_menu_init(&h->overlay);
}

void ob_host_set_present(ob_host *h, int have_gles2, int have_desktop_gl)
{
    const ob_look *look;
    h->present = ob_present_pick(h->profile, have_gles2, have_desktop_gl);
    look = (h->video_class == OB_CLASS_HIRES) ? &h->hires : &h->lores;
    h->fallback_note = ob_look_fallback_note(look->mode, h->present);
}

void ob_host_free(ob_host *h)
{
    mdrt_menu_free(&h->overlay);
    memset(h, 0, sizeof *h);
}

static int str_ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

#ifndef _WIN32
static int str_has_i(const char *s, const char *sub)
{
    int n, m, i, j;
    if (!s || !sub || !sub[0]) return 0;
    n = (int)strlen(s);
    m = (int)strlen(sub);
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m; j++) {
            int ca = (s[i + j] >= 'A' && s[i + j] <= 'Z') ? s[i + j] + 32 : s[i + j];
            int cb = (sub[j] >= 'A' && sub[j] <= 'Z') ? sub[j] + 32 : sub[j];
            if (ca != cb) break;
        }
        if (j == m) return 1;
    }
    return 0;
}
#endif

static int driver_handheld(const char *d)
{
    if (!d || !d[0]) return 0;
    return str_ieq(d, "kmsdrm") || str_ieq(d, "rpi") || str_ieq(d, "mali")
        || str_ieq(d, "vivante");
}

int ob_handheld_env(void)
{
    const char *v;
    if ((v = getenv("OVERBAR_HANDHELD")) && v[0] && strcmp(v, "0") != 0) return 1;
#ifdef _WIN32
    /* PORTMASTER / CFW_NAME leftovers from packing a zip must not switch a
     * Windows desktop exe onto the handheld present path. */
    return 0;
#else
    if ((v = getenv("PORTMASTER")) && v[0]) return 1;
    if ((v = getenv("CFW_NAME")) && v[0]) return 1;
    v = getenv("DEVICE_NAME");
    if (v && (str_has_i(v, "RG35") || str_has_i(v, "RG353") || str_has_i(v, "RG351")
              || str_has_i(v, "RGB30") || str_has_i(v, "RK2023")))
        return 1;
    return driver_handheld(getenv("SDL_VIDEODRIVER"));
#endif
}

ob_profile ob_profile_from_env(void)
{
    return ob_profile_detect(getenv("SDL_VIDEODRIVER"), ob_handheld_env());
}

ob_profile ob_profile_detect(const char *sdl_video_driver, int env_handheld)
{
    if (env_handheld) return OB_PROFILE_HANDHELD;
    if (ob_handheld_env()) return OB_PROFILE_HANDHELD;
    if (driver_handheld(sdl_video_driver)) return OB_PROFILE_HANDHELD;
    return OB_PROFILE_DESKTOP;
}

const char *ob_mode_label(ob_scale_mode m)
{
    return (m >= 0 && m < OB_MODE_COUNT) ? mode_label[m] : "Sharp";
}

int ob_sharp_mul(int index)
{
    static const int mul[OB_SHARP_COUNT] = { 2, 3, 4, 5, 6, 9 };
    if (index < 0) index = 0;
    if (index >= OB_SHARP_COUNT) index = OB_SHARP_COUNT - 1;
    return mul[index];
}

int ob_sharp_index(int mul)
{
    int i, best = OB_SHARP_3X, bd = 100;
    for (i = 0; i < OB_SHARP_COUNT; i++) {
        int d = ob_sharp_mul(i) - mul;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

const char *ob_refresh_label(ob_refresh r)
{
    static const char *lab[3] = { "60 Hz", "70 Hz", "VRR" };
    return (r >= 0 && r <= OB_REFRESH_VRR) ? lab[r] : lab[0];
}

const char *ob_height_label(ob_picture_height h)
{
    return (h >= 0 && h <= OB_HEIGHT_AUTO_CROP) ? height_label[h] : height_label[0];
}

int ob_has_hires(const ob_host *h) { return h->has_hires; }
int ob_snes_has_native(void) { return 0; }

int ob_mode_has_khz(ob_scale_mode m)
{
    return m == OB_MODE_CRT || m == OB_MODE_MISTER;
}

int ob_mode_is_pixel(ob_scale_mode m)
{
    return m >= OB_MODE_SCALE2X && m < OB_MODE_COUNT;
}

int ob_mode_has_scanlines(ob_scale_mode m)
{
    return m == OB_MODE_CRT || m == OB_MODE_MISTER;
}

ob_scale_mode ob_mode_family(ob_scale_mode m)
{
    switch (m) {
    case OB_MODE_SCALE3X: case OB_MODE_SCALE4X: return OB_MODE_SCALE2X;
    case OB_MODE_MMPX4X: return OB_MODE_MMPX2X;
    case OB_MODE_XBR3X: case OB_MODE_XBR4X: return OB_MODE_XBR2X;
    default: return m;
    }
}

/* MMPX is a 2x filter (4x is two passes); it has no 3x */
int ob_pixel_sizes(ob_scale_mode m, const char *const **labels, const ob_scale_mode **modes)
{
    static const char *const three[] = { "2x", "3x", "4x" }, *const two[] = { "2x", "4x" };
    static const ob_scale_mode scale[] = { OB_MODE_SCALE2X, OB_MODE_SCALE3X, OB_MODE_SCALE4X };
    static const ob_scale_mode mmpx[] = { OB_MODE_MMPX2X, OB_MODE_MMPX4X };
    static const ob_scale_mode xbr[] = { OB_MODE_XBR2X, OB_MODE_XBR3X, OB_MODE_XBR4X };
    switch (ob_mode_family(m)) {
    case OB_MODE_SCALE2X: *labels = three; *modes = scale; return 3;
    case OB_MODE_MMPX2X: *labels = two; *modes = mmpx; return 2;
    case OB_MODE_XBR2X: *labels = three; *modes = xbr; return 3;
    default: return 0;
    }
}

ob_hostkey ob_map_key(int sdl_scancode)
{
    if (sdl_scancode == OB_SCANCODE_GRAVE || sdl_scancode == OB_SCANCODE_F11)
        return OB_HOSTKEY_OVERLAY;
    if (sdl_scancode == OB_SCANCODE_F12) return OB_HOSTKEY_STATS;
    return OB_HOSTKEY_NONE;
}

ob_hostkey ob_map_pad_back(void) { return OB_HOSTKEY_OVERLAY; }

int ob_host_add_addon(ob_host *h, int id, const char *label, int (*open)(void *ctx))
{
    if (h->naddons >= OB_ADDONS_MAX) return 0;
    h->addons[h->naddons].id = id;
    h->addons[h->naddons].label = label;
    h->addons[h->naddons].open = open;
    h->naddons++;
    return 1;
}

static void strip(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r' || e[-1] == '\t')) *--e = 0;
}

static void apply_look_key(ob_look *l, const char *key, const char *v)
{
    if (!strcmp(key, "scale_mode")) {
        l->mode = (ob_scale_mode)name_index(v, mode_name, OB_MODE_COUNT, 0);
        if (l->mode == OB_MODE_RETROARCH) l->mode = OB_MODE_SHARP;
    }
    else if (!strcmp(key, "sharp")) {
        if (!strcmp(v, "2x") || !strcmp(v, "2X") || !strcmp(v, "1x") || !strcmp(v, "1X")) l->sharp = OB_SHARP_2X;
        else if (!strcmp(v, "3x") || !strcmp(v, "3X")) l->sharp = OB_SHARP_3X;
        else if (!strcmp(v, "4x") || !strcmp(v, "4X")) l->sharp = OB_SHARP_4X;
        else if (!strcmp(v, "5x") || !strcmp(v, "5X")) l->sharp = OB_SHARP_5X;
        else if (!strcmp(v, "6x") || !strcmp(v, "6X") || !strcmp(v, "7x")) l->sharp = OB_SHARP_6X;
        else if (!strcmp(v, "9x") || !strcmp(v, "9X") || !strcmp(v, "11x")) l->sharp = OB_SHARP_9X;
        else l->sharp = ob_sharp_index(atoi(v));
    } else if (!strcmp(key, "crt")) l->crt = (ob_crt_look)atoi(v);
    else if (!strcmp(key, "picture_height")) l->height = (ob_picture_height)name_index(v, height_name, 4, 0);
    else if (!strcmp(key, "khz")) l->khz31 = atoi(v) == 31;
    else if (!strcmp(key, "mask_scale")) l->mask_scale = atoi(v) < 1 ? 1 : atoi(v) > 4 ? 4 : atoi(v);
    else if (!strcmp(key, "bright")) l->bright = atoi(v);
    else if (!strcmp(key, "custom_scanline_darkness")) l->c_dark = (float)atof(v);
    else if (!strcmp(key, "custom_gamma")) l->c_gamma = (float)atof(v);
    else if (!strcmp(key, "custom_mask_strength")) l->c_mask = (float)atof(v);
    else if (!strcmp(key, "custom_sharpness")) l->c_sharp = (float)atof(v);
    else if (!strcmp(key, "mister_h")) snprintf(l->mister_path[0], sizeof l->mister_path[0], "%s", v);
    else if (!strcmp(key, "mister_v")) snprintf(l->mister_path[1], sizeof l->mister_path[1], "%s", v);
    else if (!strcmp(key, "mister_gamma")) snprintf(l->mister_path[2], sizeof l->mister_path[2], "%s", v);
    else if (!strcmp(key, "mister_mask")) snprintf(l->mister_path[3], sizeof l->mister_path[3], "%s", v);
    else if (!strcmp(key, "mister_preset")) snprintf(l->mister_path[4], sizeof l->mister_path[4], "%s", v);
}

static void write_look(FILE *f, const char *prefix, const ob_look *l, int console, int lores)
{
    static const char *sharp_name[OB_SHARP_COUNT] = { "2x", "3x", "4x", "5x", "6x", "9x" };
    const char *sh = sharp_name[l->sharp >= 0 && l->sharp < OB_SHARP_COUNT ? l->sharp : OB_SHARP_3X];
    (void)console;
    (void)lores;
    fprintf(f, "%s_scale_mode=%s\n", prefix, mode_name[l->mode % OB_MODE_COUNT]);
    fprintf(f, "%s_sharp=%s\n", prefix, sh);
    fprintf(f, "%s_crt=%d\n", prefix, l->crt);
    fprintf(f, "%s_picture_height=%s\n", prefix, height_name[l->height % 4]);
    fprintf(f, "%s_khz=%d\n", prefix, l->khz31 ? 31 : 15);
    fprintf(f, "%s_mask_scale=%d\n", prefix, l->mask_scale);
    fprintf(f, "%s_bright=%d\n", prefix, l->bright);
    fprintf(f, "%s_custom_scanline_darkness=%.2f\n%s_custom_gamma=%.2f\n%s_custom_mask_strength=%.2f\n%s_custom_sharpness=%.2f\n",
            prefix, l->c_dark, prefix, l->c_gamma, prefix, l->c_mask, prefix, l->c_sharp);
    fprintf(f, "%s_mister_h=%s\n", prefix, l->mister_path[0]);
    fprintf(f, "%s_mister_v=%s\n", prefix, l->mister_path[1]);
    fprintf(f, "%s_mister_gamma=%s\n", prefix, l->mister_path[2]);
    fprintf(f, "%s_mister_mask=%s\n", prefix, l->mister_path[3]);
    fprintf(f, "%s_mister_preset=%s\n", prefix, l->mister_path[4]);
}

int ob_host_save_ini(const ob_host *h)
{
    FILE *f;
    if (!h->ini_path[0]) return 0;
    f = fopen(h->ini_path, "w");
    if (!f) return 0;
    fprintf(f, "; Overbar host settings. Overlay and the file menu write the same file.\n[overbar]\n");
    fprintf(f, "refresh=%s\n", refresh_name[h->refresh % 3]);
    fprintf(f, "vsync=%s\n", h->vsync ? "on" : "off");
    fprintf(f, "stats=%s\n", h->stats ? "on" : "off");
    fprintf(f, "windows_pointer=%s\n", pointer_name[h->pointer % 3]);
    write_look(f, "lores", &h->lores, h->console_dar, 1);
    if (h->has_hires) write_look(f, "hires", &h->hires, 0, 0);
    fprintf(f, "volume=%d\n", h->volume);
    fprintf(f, "mute=%s\n", h->mute ? "on" : "off");
    fprintf(f, "online_name=%s\nonline_role=%s\nonline_address=%s\nonline_port=%d\nonline_delay=%d\nonline_here=%d\nonline_players=%d\n",
            h->online.name, net_role_name[h->online.role % 3], h->online.addr, h->online.port, h->online.delay,
            h->online.here, h->online.all);
    fclose(f);
    return 1;
}

int ob_host_load_ini(ob_host *h, const char *path)
{
    FILE *f;
    char line[256];
    snprintf(h->ini_path, sizeof h->ini_path, "%s", path ? path : "");
    f = path ? fopen(path, "r") : NULL;
    if (!f) {
        ob_defaults_apply(h, h->profile);
        h->wrote_defaults = 1;
        if (path) ob_host_save_ini(h);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        char *k = line, *v;
        if (line[0] == ';' || line[0] == '[' || line[0] == '#') continue;
        v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        strip(k); strip(v);
        if (!strcmp(k, "refresh")) h->refresh = (ob_refresh)name_index(v, refresh_name, 3, OB_REFRESH_VRR);
        else if (!strcmp(k, "vsync")) h->vsync = strcmp(v, "off") != 0;
        else if (!strcmp(k, "stats")) h->stats = !strcmp(v, "on");
        else if (!strcmp(k, "windows_pointer"))
            h->pointer = !strcmp(v, "show") || !strcmp(v, "always show") ? 1
                : !strcmp(v, "hide") || !strcmp(v, "always hide") ? 2 : 0;
        else if (!strcmp(k, "volume")) h->volume = atoi(v);
        else if (!strcmp(k, "mute")) h->mute = !strcmp(v, "on");
        else if (!strcmp(k, "online_name")) snprintf(h->online.name, sizeof h->online.name, "%.*s", OB_NET_NAME, v);
        else if (!strcmp(k, "online_role")) h->online.role = name_index(v, net_role_name, 3, 0);
        else if (!strcmp(k, "online_address")) snprintf(h->online.addr, sizeof h->online.addr, "%.*s", 95, v);
        else if (!strcmp(k, "online_port")) h->online.port = atoi(v) > 0 && atoi(v) < 65536 ? atoi(v) : OB_NET_PORT_DEFAULT;
        else if (!strcmp(k, "online_delay")) h->online.delay = atoi(v) < 0 ? 0 : atoi(v) > 10 ? 10 : atoi(v);
        else if (!strcmp(k, "online_here")) h->online.here = atoi(v) < 1 ? 1 : atoi(v);
        else if (!strcmp(k, "online_players")) h->online.all = atoi(v) < 2 ? 2 : atoi(v);
        else if (!strncmp(k, "lores_", 6)) apply_look_key(&h->lores, k + 6, v);
        else if (!strncmp(k, "hires_", 6)) apply_look_key(&h->hires, k + 6, v);
    }
    fclose(f);
    /* An older settings file kept empty paths for "the defaults"; empty is none now. A look
     * with no horizontal or vertical filter at all is one of those: give it the defaults. */
    {
        ob_look *looks[2] = { &h->lores, &h->hires };
        int i;
        for (i = 0; i < 2; i++)
            if (!looks[i]->mister_path[0][0] && !looks[i]->mister_path[1][0]) {
                char mask[192];
                int ms = looks[i]->mask_scale;
                memcpy(mask, looks[i]->mister_path[3], sizeof mask);
                ob_mister_defaults(looks[i]);
                memcpy(looks[i]->mister_path[3], mask, sizeof mask);
                looks[i]->mask_scale = ms;
            }
    }
    h->wrote_defaults = 0;
    return 1;
}

const char *ob_mister_selected(const ob_host *h, ob_class cls, int kind)
{
    const ob_look *look;
    if (!h || kind < 0 || kind > 4) return "";
    look = cls == OB_CLASS_HIRES ? &h->hires : &h->lores;
    /* a preset is checked while the filters are still the ones it set */
    if (kind == 4) return ob_mister_preset_matches(look, look->mister_path[4]) ? look->mister_path[4] : "";
    return look->mister_path[kind];
}

void ob_mister_pick(ob_host *h, ob_class cls, int kind, const char *rel)
{
    ob_look *look;
    if (!h || kind < 0 || kind > 4) return;
    look = cls == OB_CLASS_HIRES ? &h->hires : &h->lores;
    snprintf(look->mister_path[kind], sizeof look->mister_path[kind], "%s", rel ? rel : "");
    if (kind == 4) ob_mister_read_preset(rel, look);   /* its filters, gamma, mask and scale */
    look->mode = OB_MODE_MISTER;
    h->fallback_note = ob_look_fallback_note(look->mode, h->present);
    h->live_preview = 1;
    if (h->ini_path[0]) ob_host_save_ini(h);
}

int ob_host_apply(ob_host *h, ob_class cls, int id, int value)
{
    ob_look *look = (cls == OB_CLASS_HIRES) ? &h->hires : &h->lores;
    switch (id) {
    case OB_ID_REFRESH:
        h->refresh = (ob_refresh)value;
        if (h->refresh == OB_REFRESH_VRR) h->vsync = 0;
        if (h->refresh == OB_REFRESH_60 && h->profile == OB_PROFILE_HANDHELD) h->vsync = 1;
        break;
    case OB_ID_VSYNC: h->vsync = value; break;
    case OB_ID_STATS: h->stats = value; break;
    case OB_ID_BLEND: h->blend = value; break;
    case OB_ID_POINTER: h->pointer = value; break;
    case OB_ID_FM_CORE: h->fm_core = value; break;
    case OB_ID_FM_CHIP: h->fm_chip = value; break;
    case OB_ID_SCALE_MODE:
        if (value == OB_MODE_RETROARCH) value = OB_MODE_SHARP;
        /* a pixel filter family picked again keeps its size */
        if (ob_mode_family((ob_scale_mode)value) != ob_mode_family(look->mode)) look->mode = (ob_scale_mode)value;
        /* scanlines want whole rows a line where that trims only a little (Auto Crop); the
         * rest fill the window (Fit) */
        look->height = ob_mode_has_scanlines(look->mode) ? OB_HEIGHT_AUTO_CROP : OB_HEIGHT_FIT;
        h->fallback_note = ob_look_fallback_note(look->mode, h->present);
        break;
    case OB_ID_PIXEL_SIZE: {
        const char *const *labels;
        const ob_scale_mode *modes;
        int n = ob_pixel_sizes(look->mode, &labels, &modes);
        if (value >= 0 && value < n) look->mode = modes[value];
        break;
    }
    case OB_ID_VIDEO_CLASS: h->video_class = value ? OB_CLASS_HIRES : OB_CLASS_LORES; break;
    case OB_ID_SHARP_AMOUNT: look->sharp = value; break;
    case OB_ID_CRT_LOOK: look->crt = (ob_crt_look)value; break;
    case OB_ID_PICTURE_HEIGHT: look->height = (ob_picture_height)value; break;
    case OB_ID_MONITOR_KHZ: look->khz31 = value; break;
    case OB_ID_MISTER_MASK_SCALE: look->mask_scale = value + 1; break;
    case OB_ID_MISTER_BRIGHT: look->bright = 70 + 10 * value; break;
    case OB_ID_MISTER_RESET:
        look->bright = 100;
        ob_mister_defaults(look);
        break;
    case OB_ID_VOLUME: h->volume = value; break;
    case OB_ID_MUTE: h->mute = value; break;
    case OB_ID_CAP_SECONDS: h->replay_secs_index = value; break;
    case OB_ID_CAP_GIFSCALE: h->gif_scale = value; break;
    case OB_ID_CAP_RECORD: h->recording = !h->recording; break;
    case OB_ID_NET_ROLE: h->online.role = value; h->online_msg[0] = 0; break;
    case OB_ID_NET_DELAY: h->online.delay = value; break;
    case OB_ID_NET_HERE: h->online.here = value; if (h->online.all <= value) h->online.all = value + 1; break;
    case OB_ID_NET_ALL: h->online.all = value; break;
    default: return 0;
    }
    h->live_preview = 1;
    if (h->ini_path[0]) ob_host_save_ini(h);
    return 1;
}

void ob_online_set_ops(ob_host *h, const ob_online_ops *ops)
{
    if (!h) return;
    if (ops) h->online_ops = *ops;
    else memset(&h->online_ops, 0, sizeof h->online_ops);
    h->online_ready = h->online_ops.start != NULL;
}

void ob_online_open(ob_host *h)
{
    if (!h) return;
    if (!h->overlay_open) ob_overlay_open(h);
    h->overlay_page = OB_PAGE_ONLINE;
    h->overlay.sel = 0;
    ob_overlay_build(h);
}
