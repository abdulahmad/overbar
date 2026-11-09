/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The macOS menu bar: filemenu.c's bar_* calls on NSMenu. The menus are added after the application menu in [NSApp mainMenu] (the menu bar is
 * the screen's, not the window's), and a bar item that stands alone ("Online Play...") is wrapped in a menu of its own because the macOS menu
 * bar holds only menus. A pick comes back as ob_filemenu_command; opening a menu pauses the game (ob_filemenu_tracking) as the Windows bar does.
 * Build: clang -fobjc-arc -c src/bar_mac.m, link -framework Cocoa. NOT YET RUN: it needs a Mac. */
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "overbar/overbar.h"
#include "bar_backend.h"

@interface OBBarTarget : NSObject <NSMenuDelegate>
- (void)pick:(id)sender;
@end

@implementation OBBarTarget
- (void)pick:(id)sender { ob_filemenu_command((int)[(NSMenuItem *)sender tag]); }
- (void)menuWillOpen:(NSMenu *)menu { (void)menu; ob_filemenu_tracking(2); }
- (void)menuDidClose:(NSMenu *)menu { (void)menu; ob_filemenu_tracking(0); }
@end

static OBBarTarget *target;
static void *root_ptr;                          /* the bar being built, to tell it from a submenu */
static NSMutableArray<NSMenuItem *> *installed;  /* the items now in the main menu */

static NSString *title_of(const char *label)
{
    NSString *s = [NSString stringWithUTF8String:label ? label : ""];
    return s ? [s stringByReplacingOccurrencesOfString:@"\t" withString:@"  "] : @"";
}

int bar_init(void *native_window)
{
    (void)native_window;
    if (!target) target = [[OBBarTarget alloc] init];
    if (!installed) installed = [[NSMutableArray alloc] init];
    return 1;
}

void *bar_root(void)
{
    NSMenu *m = [[NSMenu alloc] initWithTitle:@""];
    m.delegate = target;
    root_ptr = (void *)CFBridgingRetain(m);
    return root_ptr;
}

void *bar_popup(void)
{
    NSMenu *m = [[NSMenu alloc] initWithTitle:@""];
    m.delegate = target;
    return (void *)CFBridgingRetain(m);
}

static void add_item(NSMenu *m, const char *label, int cmd, int disabled, int checked)
{
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title_of(label) action:(disabled || !cmd ? NULL : @selector(pick:)) keyEquivalent:@""];
    it.target = target;
    it.tag = cmd;
    it.enabled = !(disabled || !cmd);
    it.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    [m addItem:it];
}

void bar_item(void *menu, const char *label, int cmd, int disabled, int checked)
{
    NSMenu *m = (__bridge NSMenu *)menu;
    if (menu == root_ptr) {                      /* alone on the bar: a menu of its own with this one item */
        NSMenuItem *top = [[NSMenuItem alloc] initWithTitle:title_of(label) action:NULL keyEquivalent:@""];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:title_of(label)];
        sub.delegate = target;
        add_item(sub, label, cmd, disabled, checked);
        top.submenu = sub;
        [m addItem:top];
    } else
        add_item(m, label, cmd, disabled, checked);
}

int bar_sub(void *menu, const char *label, void *submenu)
{
    NSMenu *m = (__bridge NSMenu *)menu;
    NSMenu *sub = (NSMenu *)CFBridgingRelease(submenu);      /* the item owns it now */
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title_of(label) action:NULL keyEquivalent:@""];
    sub.title = title_of(label);
    it.submenu = sub;
    [m addItem:it];
    return (int)[m numberOfItems] - 1;
}

void bar_sep(void *menu) { [(__bridge NSMenu *)menu addItem:[NSMenuItem separatorItem]]; }

void bar_check_pos(void *menu, int pos)
{
    NSMenu *m = (__bridge NSMenu *)menu;
    if (pos >= 0 && pos < [m numberOfItems]) [[m itemAtIndex:pos] setState:NSControlStateValueOn];
}

static int check_in(NSMenu *m, int cmd, int on)
{
    for (NSMenuItem *it in [m itemArray]) {
        if (it.tag == cmd && cmd != 0 && !it.hasSubmenu) { it.state = on ? NSControlStateValueOn : NSControlStateValueOff; return 1; }
        if (it.hasSubmenu && check_in(it.submenu, cmd, on)) return 1;
    }
    return 0;
}

void bar_check_cmd(void *bar, int cmd, int on)
{
    if (check_in((__bridge NSMenu *)bar, cmd, on)) return;
    for (NSMenuItem *it in installed)
        if (it.submenu && check_in(it.submenu, cmd, on)) return;
}

/* Put the bar's menus into the main menu after the application menu (NULL: take them out again). A menu can belong to one item only, so the
 * items move into the main menu, and move back into the bar when it is taken out (it can be shown again later). */
static NSMenu *installed_from;

void bar_install(void *bar)
{
    NSMenu *main = [NSApp mainMenu];
    if (!main) return;
    for (NSMenuItem *c in installed) {
        NSMenu *sub = c.submenu;
        [main removeItem:c];
        if (installed_from && sub) {
            NSMenuItem *back = [[NSMenuItem alloc] initWithTitle:c.title action:NULL keyEquivalent:@""];
            c.submenu = nil;
            back.submenu = sub;
            [installed_from addItem:back];
        }
    }
    [installed removeAllObjects];
    installed_from = nil;
    if (bar) {
        NSMenu *b = (__bridge NSMenu *)bar;
        for (NSMenuItem *it in [b itemArray]) {
            NSMenuItem *c = [[NSMenuItem alloc] initWithTitle:it.title action:NULL keyEquivalent:@""];
            NSMenu *sub = it.submenu;
            [b removeItem:it];
            it.submenu = nil;
            c.submenu = sub;
            [main addItem:c];
            [installed addObject:c];
        }
        installed_from = b;
    }
}

void bar_destroy(void *bar)
{
    if (!bar) return;
    if (installed_from == (__bridge NSMenu *)bar) installed_from = nil;
    if (bar == root_ptr) root_ptr = NULL;
    CFRelease(bar);
}
int bar_client_size(int *w, int *h) { (void)w; (void)h; return 0; }
#endif
