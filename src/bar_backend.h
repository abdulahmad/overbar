/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The native menu bar's backend: what filemenu.c needs from the OS. bar_win.c (HMENU), bar_mac.m (NSMenu); other systems have none (stubs
 * in filemenu.c). A command number is what a picked item reports (ob_filemenu_command). */
#ifndef OVERBAR_BAR_BACKEND_H
#define OVERBAR_BAR_BACKEND_H

#define CMD0 0x1100                       /* command numbers: CMD0.. an action, MF0.. a MiSTer file */
#define MF0 0x4000

int bar_init(void *native_window);        /* 1 when there is a window to hold a bar */
void *bar_root(void);                     /* a new, empty bar */
void *bar_popup(void);                    /* a new, empty submenu */
void bar_item(void *menu, const char *label, int cmd, int disabled, int checked);   /* on the bar itself an item stands alone (macOS wraps it in a menu) */
int bar_sub(void *menu, const char *label, void *submenu);                         /* returns the item's position */
void bar_sep(void *menu);
void bar_check_pos(void *menu, int pos);  /* check a submenu item (by position) */
void bar_check_cmd(void *bar, int cmd, int on);
void bar_install(void *bar);              /* show this bar (NULL: none) */
void bar_destroy(void *bar);
int bar_client_size(int *w, int *h);      /* the window's client area, 0 when the bar does not change it */

/* what the backend calls back */
void ob_filemenu_command(int cmd);
void ob_filemenu_tracking(int what);      /* 0 ended, 1 started, 2 a menu is about to show */

#endif
