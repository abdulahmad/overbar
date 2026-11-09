/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The Windows menu bar: filemenu.c's bar_* calls on an HMENU, and the window messages the client copies into ob_wm. Opening the bar runs
 * Windows' own loop in the main thread, so the client is asked to pause (ob_filemenu_tracking) until it closes. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "overbar/overbar.h"
#include "bar_backend.h"

static HWND g_hwnd;

int bar_init(void *native_window)
{
    g_hwnd = (HWND)native_window;
    return g_hwnd != NULL;
}

void *bar_root(void) { return CreateMenu(); }
void *bar_popup(void) { return CreatePopupMenu(); }

void bar_item(void *menu, const char *label, int cmd, int disabled, int checked)
{
    AppendMenuA((HMENU)menu, MF_STRING | (disabled ? MF_GRAYED : 0) | (checked ? MF_CHECKED : 0), (UINT_PTR)cmd, label ? label : "");
}

int bar_sub(void *menu, const char *label, void *submenu)
{
    int pos = GetMenuItemCount((HMENU)menu);
    AppendMenuA((HMENU)menu, MF_POPUP, (UINT_PTR)submenu, label ? label : "");
    return pos;
}

void bar_sep(void *menu) { AppendMenuA((HMENU)menu, MF_SEPARATOR, 0, ""); }
void bar_check_pos(void *menu, int pos) { CheckMenuItem((HMENU)menu, (UINT)pos, MF_BYPOSITION | MF_CHECKED); }
void bar_check_cmd(void *bar, int cmd, int on) { CheckMenuItem((HMENU)bar, (UINT)cmd, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); }

void bar_install(void *bar)
{
    if (!g_hwnd) return;
    SetMenu(g_hwnd, (HMENU)bar);
    DrawMenuBar(g_hwnd);
}

void bar_destroy(void *bar) { if (bar) DestroyMenu((HMENU)bar); }

int bar_client_size(int *w, int *h)
{
    RECT r;
    if (!g_hwnd || !GetClientRect(g_hwnd, &r) || r.right <= 0 || r.bottom <= 0) return 0;
    if (w) *w = r.right;
    if (h) *h = r.bottom;
    return 1;
}

void ob_filemenu_event(const ob_wm *wm)
{
    WPARAM wp;
    if (!wm) return;
    wp = (WPARAM)wm->wparam;
    if (wm->msg == WM_EXITMENULOOP) ob_filemenu_tracking(0);
    else if (wm->msg == WM_ENTERMENULOOP) ob_filemenu_tracking(1);
    else if (wm->msg == WM_INITMENU) ob_filemenu_tracking(2);
    else if (wm->msg == WM_COMMAND && HIWORD(wp) == 0) ob_filemenu_command((int)LOWORD(wp));
}
#else
typedef int ob_bar_win_unit_not_empty;
#endif
