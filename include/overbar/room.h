/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The room's glue and page: what ties an ob_net room to the select-sides screen (overbar/sides.h)
 * and chat (overbar/chat.h), and the page that shows who is in, their pings, the seats, and the
 * host's status line. No SDL; call it under the port's net lock (ob_netgame_lock).
 *
 * ob_room_poll, once a frame: the messages that came go to sides and chat (the rest to the game's
 * hook, in order), and each machine that joins or leaves is told to sides (a guest's devices go when
 * it leaves; one that joins is sent the screen) and said in chat ("Bea joined"); files and the season
 * (ob_room_modules) get theirs too, and the page shows a line about a season sync. */
#ifndef OVERBAR_ROOM_H
#define OVERBAR_ROOM_H

#include "overbar/net.h"
#include "overbar/sides.h"
#include "overbar/chat.h"
#include "overbar/files.h"
#include "overbar/season.h"
#include "overbar/serial.h"
#include "overbar/console.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_ROOM_LINE 48             /* characters of a line of the page */
#define OB_ROOM_LINES 20

typedef struct ob_room {
    ob_net *net;
    ob_sides *sides;                /* NULL: none */
    ob_chat *chat;                  /* NULL: none */
    ob_files *files;                /* NULL: none (ob_room_modules) */
    ob_season *season;              /* NULL: none */
    ob_serial *serial;              /* NULL: none (ob_room_serial) */
    ob_console *console;            /* NULL: none (ob_room_console) */
    unsigned members;               /* as last seen */
    char name[OB_NET_MAX_MACHINES][OB_NET_NAME + 1];   /* (kept, to say who left) */
} ob_room;

void ob_room_init(ob_room *r, ob_net *net, ob_sides *sides, ob_chat *chat);
/* the room's files and season (either NULL): their messages go to them, and each machine that leaves */
void ob_room_modules(ob_room *r, ob_files *files, ob_season *season);
/* the room carries a serial port (a DOS game's modem or cable play, two machines): its messages go to it, it is told
 * when the other machine comes and goes, and the page shows its line */
void ob_room_serial(ob_room *r, ob_serial *serial);
/* a console's states for everyone (overbar/console.h): its messages go to it */
void ob_room_console(ob_room *r, ob_console *console);
void ob_room_serial_send(void *net, const void *data, size_t size);     /* the sender for ob_serial_init (ud: the ob_net) */
/* the senders for ob_files_init and ob_season_init (ud: the ob_net) */
void ob_room_files_send(void *net, int to, const void *data, size_t size);
void ob_room_season_send(void *net, int to, const void *data, size_t size);
/* the senders for ob_sides_init and ob_chat_init (ud: the ob_net) */
void ob_room_sides_send(void *net, int to, const void *data, size_t size);
void ob_room_chat_send(void *net, const void *data, size_t size);
/* once a frame: messages and who is in. game: the game's own messages (NULL: dropped) */
void ob_room_poll(ob_room *r, void (*game)(void *ud, int from, int type, const void *data, size_t size), void *ud);
/* the page's lines (line[k], OB_ROOM_LINE + 1 bytes each): the status, each machine, the seats. Their count */
int ob_room_lines(ob_room *r, char (*line)[OB_ROOM_LINE + 1], int max);
/* the page over a w x h ARGB picture */
void ob_room_draw(ob_room *r, uint32_t *px, int w, int h, int pitch);

#ifdef __cplusplus
}
#endif

#endif
