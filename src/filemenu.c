/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Native file-menu shell. L3: menu bar only in windowed desktop. Overlay always
 * available. Handheld / KMSDRM: overlay only. Win32 CreateMenu from the same
 * item tree as the overlay (bar_win.c); macOS: NSMenu (bar_mac.m); Linux has no bar.
 *
 * This file is the portable half: it builds the bar from the item tree through a few bar_* calls (bar_backend.h) and handles what a pick does.
 *
 * The bar does not link SDL or the game host. The client passes an HWND and
 * copies window messages into ob_wm. Picture, pause, and quit go through
 * ob_shell, which the library test leaves unset.
 *
 * Opening the Win32 bar runs Windows' own loop in the main thread, so the
 * client is asked to pause until the menu closes.
 */
#include "overbar/overbar.h"

#include <stdio.h>
#include <string.h>


static ob_shell g_shell;
static int g_shown;
static ob_host *g_host;

void ob_shell_set(const ob_shell *hooks)
{
    if (hooks) g_shell = *hooks;
    else memset(&g_shell, 0, sizeof g_shell);
}

void ob_picture_apply(const ob_host *h)
{
    if (h && g_shell.picture) g_shell.picture(h);
}

void ob_capture_apply(const ob_host *h, int id)
{
    if (h && g_shell.capture) g_shell.capture(h, id);
}

#include "bar_backend.h"

#define NCMDS 8192
#define NMF 4096

static int g_have_window;                       /* a native window was given to ob_filemenu_init */
static void *g_bar;                             /* the bar: the backend's root menu */
static struct { int id, value, cls; } cmds[NCMDS];
static int ncmds;
static int g_in_bar;
static int g_dirty;

static int add_cmd(int id, int value, int cls)
{
    int c;
    if (ncmds >= NCMDS) return 0;
    c = CMD0 + ncmds;
    cmds[ncmds].id = id;
    cmds[ncmds].value = value;
    cmds[ncmds].cls = cls;
    ncmds++;
    return c;
}

static int page_of(int id)
{
    switch (id) {
    case OB_ID_VIDEO: return OB_PAGE_VIDEO;
    case OB_ID_SOUND: return OB_PAGE_SOUND;
    case OB_ID_INPUT: return OB_PAGE_INPUT;
    case OB_ID_CAPTURE: return OB_PAGE_CAPTURE;
    case OB_ID_ONLINE: return OB_PAGE_ONLINE;
    case OB_ID_KEYS: return OB_PAGE_KEYS;
    case OB_ID_SCALE_HI: return OB_PAGE_SCALE_HI;
    case OB_ID_SCALE_LO: return OB_PAGE_SCALE_LO;
    case OB_ID_MODE_MENU_HI: return OB_PAGE_MODE_HI;
    case OB_ID_MODE_MENU_LO: return OB_PAGE_MODE_LO;
    case OB_ID_OPTS_MENU_HI: return OB_PAGE_OPTS_HI;
    case OB_ID_OPTS_MENU_LO: return OB_PAGE_OPTS_LO;
    default: return -1;
    }
}

static struct { int kind, cls; char rel[192]; } mfiles[NMF];
static int nmfiles;

static int add_mfile(int kind, int cls, const char *rel)
{
    int c;
    if (nmfiles >= NMF) return 0;
    c = MF0 + nmfiles;
    mfiles[nmfiles].kind = kind;
    mfiles[nmfiles].cls = cls;
    snprintf(mfiles[nmfiles].rel, sizeof mfiles[nmfiles].rel, "%s", rel ? rel : "");
    nmfiles++;
    return c;
}

static void fill_mister(void *menu, const char *rel, int kind, int cls, int at_root)
{
    ob_item items[OB_ITEMS_MAX];
    const char *sel = ob_mister_selected(g_host, cls ? OB_CLASS_HIRES : OB_CLASS_LORES, kind);
    int n = ob_mister_entries(rel, kind, at_root, sel, items, OB_ITEMS_MAX);
    int i;
    for (i = 0; i < n; i++) {
        if (items[i].id == OB_ID_MISTER_DIR) {
            void *sm = bar_popup();
            int pos;
            fill_mister(sm, items[i].detail, kind, cls, 0);
            pos = bar_sub(menu, ob_mark_strip(items[i].label), sm);
            if (sel && sel[0] && items[i].detail[0]
                && !strncmp(sel, items[i].detail, strlen(items[i].detail))
                && (sel[strlen(items[i].detail)] == '/' || sel[strlen(items[i].detail)] == 0))
                bar_check_pos(menu, pos);
        } else if (items[i].id == OB_ID_MISTER_FILE) {
            int cmd = add_mfile(kind, cls, items[i].detail);
            int on = sel && !strcmp(sel, items[i].detail);
            if (kind == 4) on = sel && sel[0] && !strcmp(sel, items[i].detail);
            else if (!items[i].detail[0]) on = !sel || !sel[0];       /* None */
            bar_item(menu, ob_mark_strip(items[i].label), cmd, 0, on);
        } else if (items[i].kind == MDRT_ITEM_TEXT) {
            bar_item(menu, items[i].label ? items[i].label : "", 0, 1, 0);
        }
    }
}

static void append_items(void *m, const ob_item *items, int n, int cls)
{
    int i;
    for (i = 0; i < n; i++) {
        const ob_item *it = &items[i];
        char buf[320];
        int subpage;
        if (it->id == OB_ID_BACK) continue;
        if (it->kind == OB_ITEM_SEP) {
            bar_sep(m);
            continue;
        }
        subpage = page_of(it->id);
        if (subpage >= 0 && subpage != OB_PAGE_VIDEO) {
            ob_item sub[OB_ITEMS_MAX];
            int ns = ob_page_items(g_host, subpage, sub, OB_ITEMS_MAX);
            void *sm = bar_popup();
            int subcls = (subpage == OB_PAGE_SCALE_HI || subpage == OB_PAGE_MODE_HI
                          || subpage == OB_PAGE_OPTS_HI) ? OB_CLASS_HIRES : OB_CLASS_LORES;
            append_items(sm, sub, ns, subcls);
            bar_sub(m, it->label ? it->label : "", sm);
            continue;
        }
        if (it->id == OB_ID_MISTER_H || it->id == OB_ID_MISTER_V || it->id == OB_ID_MISTER_GAMMA
            || it->id == OB_ID_MISTER_MASK || it->id == OB_ID_MISTER_PRESET) {
            int kind = it->id == OB_ID_MISTER_PRESET ? 4 : it->id == OB_ID_MISTER_V ? 1
                : it->id == OB_ID_MISTER_GAMMA ? 2 : it->id == OB_ID_MISTER_MASK ? 3 : 0;
            const char *root = kind == 4 ? "Presets" : kind == 2 ? "Gamma" : kind == 3 ? "Shadow_Masks" : "Filters";
            void *sm = bar_popup();
            fill_mister(sm, root, kind, cls, 1);
            bar_sub(m, it->label ? it->label : "", sm);
            continue;
        }
        if (it->kind == MDRT_ITEM_CHOICE && it->nchoices > 0 && it->choices
            && (it->id == OB_ID_MISTER_MASK_SCALE || it->id == OB_ID_MISTER_BRIGHT)) {
            int j;
            void *sm = bar_popup();
            for (j = 0; j < it->nchoices; j++) {
                const char *choice = it->choices[j] ? it->choices[j] : "";
                bar_item(sm, choice, add_cmd(it->id, j, cls), 0, 0);
            }
            bar_sub(m, it->label ? it->label : "", sm);
            continue;
        }
        if (it->kind == MDRT_ITEM_CHOICE && it->nchoices > 0 && it->choices) {
            int j;
            /* "Vsync Off" / "Vsync On" sit on this menu, with a check on the one in use. */
            for (j = 0; j < it->nchoices; j++) {
                const char *choice = it->choices[j] ? it->choices[j] : "";
                snprintf(buf, sizeof buf, "%s %s", it->label ? it->label : "", choice);
                bar_item(m, buf, add_cmd(it->id, j, cls), 0, 0);
            }
        } else if (it->kind == MDRT_ITEM_TEXT) {
            snprintf(buf, sizeof buf, "%s%s%s", it->label ? it->label : "",
                     it->detail[0] ? " — " : "", it->detail);
            bar_item(m, buf, 0, 1, 0);
        } else if (it->kind == MDRT_ITEM_NUMBER) {
            snprintf(buf, sizeof buf, "%s  %d", it->label ? it->label : "", it->value);
            bar_item(m, buf, 0, 1, 0);
        } else {
            snprintf(buf, sizeof buf, "%s%s%s", it->label ? it->label : "",
                     it->detail[0] ? "\t" : "", it->detail);
            bar_item(m, buf, add_cmd(it->id, it->value, cls), 0, 0);
        }
    }
}

static int look_id(int id)
{
    switch (id) {
    case OB_ID_SCALE_MODE:
    case OB_ID_SHARP_AMOUNT:
    case OB_ID_CRT_LOOK:
    case OB_ID_PIXEL_SIZE:
    case OB_ID_PICTURE_HEIGHT:
    case OB_ID_MONITOR_KHZ:
    case OB_ID_MISTER_PRESET:
    case OB_ID_MISTER_H:
    case OB_ID_MISTER_V:
    case OB_ID_MISTER_GAMMA:
    case OB_ID_MISTER_MASK:
    case OB_ID_MISTER_MASK_SCALE:
    case OB_ID_MISTER_BRIGHT:
        return 1;
    default:
        return 0;
    }
}

static void apply_marks(void)
{
    static const int pages[] = {
        OB_PAGE_VIDEO, OB_PAGE_SCALE_HI, OB_PAGE_SCALE_LO, OB_PAGE_MODE_HI, OB_PAGE_MODE_LO,
        OB_PAGE_OPTS_HI, OB_PAGE_OPTS_LO, OB_PAGE_SOUND, OB_PAGE_INPUT,
        OB_PAGE_CAPTURE, OB_PAGE_KEYS
    };
    ob_item items[OB_ITEMS_MAX];
    int p, n, i, c;
    if (!g_bar || !g_host) return;
    for (c = 0; c < ncmds; c++) {
        int cur = -1, found = 0;
        int want = -1;
        if (cmds[c].id == OB_ID_SCALE_MODE && g_host) {
            const ob_look *look = cmds[c].cls ? &g_host->hires : &g_host->lores;
            bar_check_cmd(g_bar, CMD0 + c, cmds[c].value == (int)ob_mode_family(look->mode));
            continue;
        }
        if (look_id(cmds[c].id)) {
            want = cmds[c].cls ? OB_PAGE_SCALE_HI : OB_PAGE_SCALE_LO;
        }
        for (p = 0; p < (int)(sizeof pages / sizeof *pages); p++) {
            if (want >= 0 && pages[p] != want) continue;
            n = ob_page_items(g_host, pages[p], items, OB_ITEMS_MAX);
            for (i = 0; i < n; i++) {
                if (items[i].id == cmds[c].id && items[i].kind == MDRT_ITEM_CHOICE) {
                    cur = items[i].value;
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found)
            bar_check_cmd(g_bar, CMD0 + c, cmds[c].value == cur);
    }
}

static void build_bar(void)
{
    ob_item main_items[OB_ITEMS_MAX];
    int n, i;
    if (!g_have_window || !g_host) return;
    ncmds = 0;
    nmfiles = 0;
    if (g_bar) {
        bar_install(NULL);
        bar_destroy(g_bar);
        g_bar = NULL;
    }
    g_bar = bar_root();
    n = ob_main_items(g_host, main_items, OB_ITEMS_MAX);
    for (i = 0; i < n; i++) {
        const ob_item *it = &main_items[i];
        int page;
        /* Resume and Quit are the overlay's. A bar has no game to resume. */
        if (it->id == OB_ID_RESUME || it->id == OB_ID_QUIT) continue;
        page = page_of(it->id);
        if (page == OB_PAGE_ONLINE) {
            /* the Online page types into fields: the bar opens it in the overlay */
            bar_item(g_bar, "Online Play...", add_cmd(OB_ID_ONLINE, 0, OB_CLASS_LORES), !g_host->online_ready, 0);
        } else if (page >= 0) {
            ob_item sub[OB_ITEMS_MAX];
            int ns = ob_page_items(g_host, page, sub, OB_ITEMS_MAX);
            void *sm = bar_popup();
            append_items(sm, sub, ns, page == OB_PAGE_SCALE_HI ? OB_CLASS_HIRES : OB_CLASS_LORES);
            bar_sub(g_bar, it->label ? it->label : "", sm);
        } else {
            bar_item(g_bar, it->label ? it->label : "", add_cmd(it->id, 0, OB_CLASS_LORES), 0, 0);
        }
    }
    apply_marks();
}

static void present_bar(void)
{
    if (!g_have_window) return;
    bar_install(g_shown ? g_bar : NULL);
}

static void pull_shared(ob_host *h)
{
    int vsync = g_shell.vsync ? g_shell.vsync(-1) : -1;
    int fps = g_shell.stats ? g_shell.stats(-1) : -1;
    if (vsync >= 0) h->vsync = vsync;
    if (fps >= 0) h->stats = fps;
}

static void apply_cmd(int id, int value)
{
    int i;
    if (!g_host) return;
    if (id == OB_ID_RESUME) {
        ob_overlay_close(g_host);
        return;
    }
    if (id == OB_ID_QUIT) {
        g_host->request_quit = 1;
        ob_overlay_close(g_host);
        if (g_shell.quit) g_shell.quit();
        return;
    }
    if (id == OB_ID_ONLINE) {
        ob_online_open(g_host);
        if (g_shell.redraw) g_shell.redraw();
        return;
    }
    if (id == OB_ID_CONTROLLERS) {
        if (g_shell.controllers) { g_shell.controllers(); return; }
        if (!g_host->overlay_open) ob_overlay_open(g_host);
        g_host->overlay_page = OB_PAGE_INPUT;
        ob_overlay_build(g_host);
        return;
    }
    for (i = 0; i < g_host->naddons; i++) {
        if (g_host->addons[i].id == id && g_host->addons[i].open) {
            g_host->addons[i].open(g_host->addon_ctx);
            break;
        }
    }
    ob_host_apply(g_host, g_host->video_class, id, value);
    ob_picture_apply(g_host);
    ob_capture_apply(g_host, id);
    ob_filemenu_after_change(id);
}

/* a bar item picked: its command number (CMD0.. an action, MF0.. a MiSTer file) */
void ob_filemenu_command(int cmd)
{
    int mf = cmd - MF0;
    int idx = cmd - CMD0;
    if (mf >= 0 && mf < nmfiles && g_host) {
        ob_mister_pick(g_host, mfiles[mf].cls ? OB_CLASS_HIRES : OB_CLASS_LORES, mfiles[mf].kind, mfiles[mf].rel);
        ob_picture_apply(g_host);
        if (g_host->overlay_open) {
            ob_overlay_build(g_host);
            if (g_shell.redraw) g_shell.redraw();
            if (g_shell.draw_now) g_shell.draw_now();
        }
        return;
    }
    if (idx >= 0 && idx < ncmds && g_host) {
        if (look_id(cmds[idx].id)) g_host->video_class = cmds[idx].cls ? OB_CLASS_HIRES : OB_CLASS_LORES;
        apply_cmd(cmds[idx].id, cmds[idx].value);
        if (g_host->overlay_open) {
            ob_overlay_build(g_host);
            if (cmds[idx].id == OB_ID_SCALE_MODE) {
                int k;
                for (k = 0; k < g_host->overlay.count; k++)
                    if (g_host->overlay.items[k].id == OB_ID_SCALE_MODE && g_host->overlay.items[k].value == cmds[idx].value) {
                        g_host->overlay.sel = k;
                        break;
                    }
            } else
                mdrt_menu_select(&g_host->overlay, cmds[idx].id);
            if (g_shell.redraw) g_shell.redraw();
            if (g_shell.draw_now) g_shell.draw_now();
        }
    }
}

/* the bar's menu tracking: 1 the loop started (the client pauses the game while the main thread is inside it), 2 a menu is about to show
 * (the loop starts if it had not; the shared settings are pulled and the marks follow), 0 the loop ended */
void ob_filemenu_tracking(int what)
{
    if (what) {
        g_in_bar = 1;
        if (g_shell.hold) g_shell.hold(1);
        if (what == 2 && g_host) {
            pull_shared(g_host);
            if (g_dirty) { g_dirty = 0; build_bar(); present_bar(); }
            else apply_marks();
        }
    } else {
        g_in_bar = 0;
        if (g_dirty) { g_dirty = 0; build_bar(); present_bar(); }
    }
}

int ob_filemenu_wanted(int windowed, int fullscreen, int handheld)
{
    return windowed && !fullscreen && !handheld;
}

int ob_filemenu_items(const ob_host *h, ob_item *out, int cap)
{
    /* The bar's own menus: the overlay's main page without Resume and Quit (build_bar). */
    ob_item tmp[OB_ITEMS_MAX];
    int n = 0, i, v;
    if (cap < 1) return 0;
    v = ob_main_items(h, tmp, OB_ITEMS_MAX);
    for (i = 0; i < v && n < cap; i++) {
        if (tmp[i].id == OB_ID_RESUME || tmp[i].id == OB_ID_QUIT || tmp[i].id == OB_ID_BACK) continue;
        out[n++] = tmp[i];
    }
    return n;
}

void ob_filemenu_init(void *native_window)
{
    g_shown = 0;
    g_bar = NULL;
    ncmds = 0;
    g_in_bar = 0;
    g_dirty = 0;
    g_have_window = bar_init(native_window);
}

void ob_filemenu_after_change(int id)
{
    if (!g_have_window) return;
    if (id == OB_ID_SCALE_MODE || id == OB_ID_CAP_RECORD)
        g_dirty = 1;
    if (g_in_bar) {
        if (g_bar) apply_marks();
        return;
    }
    if (g_dirty) {
        g_dirty = 0;
        build_bar();
        present_bar();
    } else if (g_bar)
        apply_marks();
}

void ob_filemenu_sync(const ob_host *h)
{
    g_host = (ob_host *)h;
    if (g_host) pull_shared(g_host);
    if (!g_have_window || !g_host) return;
    build_bar();
    present_bar();
}

void ob_filemenu_show(int on)
{
    g_shown = on;
    if (!g_have_window) return;
    present_bar();
}

int ob_filemenu_client_size(int *w, int *h)
{
    return g_have_window ? bar_client_size(w, h) : 0;
}

#if !defined(_WIN32) && !defined(__APPLE__)
/* no native bar here: the overlay is the menu */
void *bar_root(void) { return NULL; }
void *bar_popup(void) { return NULL; }
void bar_item(void *m, const char *l, int c, int d, int k) { (void)m; (void)l; (void)c; (void)d; (void)k; }
int bar_sub(void *m, const char *l, void *s) { (void)m; (void)l; (void)s; return 0; }
void bar_sep(void *m) { (void)m; }
void bar_check_pos(void *m, int p) { (void)m; (void)p; }
void bar_check_cmd(void *m, int c, int on) { (void)m; (void)c; (void)on; }
void bar_install(void *bar) { (void)bar; }
void bar_destroy(void *bar) { (void)bar; }
int bar_init(void *w) { (void)w; return 0; }
int bar_client_size(int *w, int *h) { (void)w; (void)h; return 0; }
#endif
#ifndef _WIN32
void ob_filemenu_event(const ob_wm *wm) { (void)wm; }       /* (Windows messages only; the macOS bar calls ob_filemenu_command / _tracking) */
#endif
