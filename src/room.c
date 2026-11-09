/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The room's glue and page (overbar/room.h). */
#include "overbar/room.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mdrt/menu.h"

void ob_room_init(ob_room *r, ob_net *net, ob_sides *sides, ob_chat *chat)
{
    memset(r, 0, sizeof *r);
    r->net = net;
    r->sides = sides;
    r->chat = chat;
}

void ob_room_modules(ob_room *r, ob_files *files, ob_season *season)
{
    r->files = files;
    r->season = season;
}

void ob_room_serial(ob_room *r, ob_serial *serial) { r->serial = serial; }

void ob_room_serial_send(void *net, const void *data, size_t size)
{
    ob_net_send((ob_net *)net, OB_NET_ALL, OB_NET_TYPE_SERIAL, data, size);
}

void ob_room_files_send(void *net, int to, const void *data, size_t size)
{
    ob_net_send((ob_net *)net, to, OB_NET_TYPE_FILES, data, size);
}

void ob_room_season_send(void *net, int to, const void *data, size_t size)
{
    ob_net_send((ob_net *)net, to, OB_NET_TYPE_SEASON, data, size);
}

void ob_room_sides_send(void *net, int to, const void *data, size_t size)
{
    ob_net_send((ob_net *)net, to < 0 ? OB_NET_ALL : to, OB_NET_TYPE_SIDES, data, size);
}

void ob_room_chat_send(void *net, const void *data, size_t size)
{
    ob_net_send((ob_net *)net, OB_NET_ALL, OB_NET_TYPE_CHAT, data, size);
}

static void news(ob_room *r, const char *who, const char *what)
{
    char b[OB_NET_NAME + 16];
    if (!r->chat) return;
    snprintf(b, sizeof b, "%s %s", who[0] ? who : "Someone", what);
    ob_chat_add(r->chat, "", b);
}

void ob_room_poll(ob_room *r, void (*game)(void *, int, int, const void *, size_t), void *ud)
{
    unsigned now = ob_net_members(r->net), me = 1u << ob_net_me(r->net);
    int k, from, type;
    uint8_t small[2048];
    size_t size;
    /* who came and who went (this machine itself is never news) */
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) {
        unsigned bit = 1u << k;
        if ((now & bit) && !(r->members & bit)) {
            snprintf(r->name[k], sizeof r->name[k], "%s", ob_net_name_of(r->net, k));
            if (!(bit & me)) {
                if (r->sides) ob_sides_machine_came(r->sides, k);
                if (r->members) news(r, r->name[k], "joined");
            }
        } else if (!(now & bit) && (r->members & bit)) {
            if (r->sides) ob_sides_machine_gone(r->sides, k);
            if (r->files) ob_files_machine_gone(r->files, k);
            if (r->season) ob_season_machine_gone(r->season, k);
            news(r, r->name[k], "left");
        }
    }
    r->members = now;
    if (r->serial) ob_serial_peer(r->serial, (now & ~me) != 0);
    for (;;) {
        int got = ob_net_recv(r->net, &from, &type, small, sizeof small, &size);
        uint8_t *big = NULL;
        const void *data = small;
        if (got == 0) break;
        if (got < 0) {                                  /* larger than the buffer here (a game's file, say) */
            big = (uint8_t *)malloc(size ? size : 1);
            if (!big || ob_net_recv(r->net, &from, &type, big, size, &size) != 1) { free(big); break; }
            data = big;
        }
        if (type == OB_NET_TYPE_SIDES) { if (r->sides) ob_sides_message(r->sides, from, data, size); }
        else if (type == OB_NET_TYPE_CHAT) { if (r->chat) ob_chat_message(r->chat, data, size); }
        else if (type == OB_NET_TYPE_FILES) { if (r->files) ob_files_message(r->files, from, data, size); }
        else if (type == OB_NET_TYPE_SEASON) { if (r->season) ob_season_message(r->season, from, data, size); }
        else if (type == OB_NET_TYPE_SERIAL) { if (r->serial) ob_serial_message(r->serial, data, size); }
        else if (game) game(ud, from, type, data, size);
        free(big);
    }
}

int ob_room_lines(ob_room *r, char (*line)[OB_ROOM_LINE + 1], int max)
{
    int k, n = 0, me = ob_net_me(r->net);
    unsigned members = ob_net_members(r->net);
    char st[96];
    if (n < max) { ob_net_status(r->net, st, sizeof st); snprintf(line[n++], OB_ROOM_LINE + 1, "%.48s", st); }
    if (r->season && n < max) {
        ob_season_status(r->season, st, sizeof st);
        if (st[0]) snprintf(line[n++], OB_ROOM_LINE + 1, "%.48s", st);
    }
    if (r->serial && n < max) {
        ob_serial_status(r->serial, st, sizeof st);
        if (st[0]) snprintf(line[n++], OB_ROOM_LINE + 1, "%.48s", st);
    }
    for (k = 0; k < OB_NET_MAX_MACHINES && n < max; k++) {
        int ping, here;
        char pings[16];
        if (!(members & (1u << k))) continue;
        ping = ob_net_ping_of(r->net, k);
        here = ob_net_here_of(r->net, k);
        if (k == me) snprintf(pings, sizeof pings, "you");
        else if (ping < 0) snprintf(pings, sizeof pings, "-");
        else snprintf(pings, sizeof pings, "%d ms", ping);
        snprintf(line[n++], OB_ROOM_LINE + 1, "%-5s %-20.20s %-7.7s %s", k == 0 ? "host" : "guest",
                 ob_net_name_of(r->net, k), pings, here ? "" : "watching");
    }
    if (r->sides && n < max) {
        ob_sides_seat seat[OB_SIDES_MAX_SEATS];
        int s, ns = ob_sides_seats(r->sides, seat, OB_SIDES_MAX_SEATS);
        snprintf(line[n++], OB_ROOM_LINE + 1, ns ? "Seats:" : "Seats: none picked yet");
        for (s = 0; s < ns && n < max; s++)
            snprintf(line[n++], OB_ROOM_LINE + 1, " %-12.12s %-16.16s %.16s", r->sides->team[seat[s].team], seat[s].name,
                     ob_net_name_of(r->net, seat[s].owner));
    }
    return n;
}

static void fill(uint32_t *px, int w, int h, int pitch, int x, int y, int rw, int rh, uint32_t rgb)
{
    int i, j;
    for (j = 0; j < rh; j++) {
        if (y + j < 0 || y + j >= h) continue;
        for (i = 0; i < rw; i++)
            if (x + i >= 0 && x + i < w) px[(size_t)(y + j) * pitch + x + i] = rgb;
    }
}

void ob_room_draw(ob_room *r, uint32_t *px, int w, int h, int pitch)
{
    char line[OB_ROOM_LINES][OB_ROOM_LINE + 1];
    int n = ob_room_lines(r, line, OB_ROOM_LINES), k, bw, bh, x0, y0, chars;
    if (w < 120 || h < 60) return;
    bw = w - 16 > OB_ROOM_LINE * 6 + 8 ? OB_ROOM_LINE * 6 + 8 : w - 16;
    chars = (bw - 8) / 6;
    bh = 18 + n * 11;
    if (bh > h - 8) bh = h - 8;
    x0 = (w - bw) / 2;
    y0 = 4;
    fill(px, w, h, pitch, x0, y0, bw, bh, 0xF0101830u);
    mdrt_menu_text(px, w, h, pitch, x0 + 4, y0 + 4, "Online room", 0xFFE0E060u);
    for (k = 0; k < n && y0 + 18 + k * 11 + 10 <= y0 + bh; k++) {
        if ((int)strlen(line[k]) > chars) line[k][chars] = 0;
        mdrt_menu_text(px, w, h, pitch, x0 + 4, y0 + 18 + k * 11, line[k], k == 0 ? 0xFFC0C0C0u : 0xFFFFFFFFu);
    }
}
