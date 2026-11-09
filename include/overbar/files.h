/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Files between the machines of a room: a folder (its plain files) sent across the room's message
 * lane, for every platform. The sender offers the folder (each file's name, size and hash); the
 * receiver asks only for those it lacks, so a folder sent again after a drop sends only what had not
 * come yet (what came is kept in a staging folder beside the target, "<dir>.obpart"). Once every file
 * is there and its hash checks, the files go into the target folder together; with OB_FILES_MIRROR the
 * target then holds exactly the folder sent (files not in it go). Each side is told when a transfer
 * ends, either way.
 *
 * Messages are of type OB_NET_TYPE_FILES; ob_room_poll gives them to ob_files_message
 * (ob_room_modules). Calls run on one thread (under the port's net lock). No SDL. */
#ifndef OVERBAR_FILES_H
#define OVERBAR_FILES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_FILES_NAME 64                /* a file's name or a transfer's tag, its 0 included */
#define OB_FILES_MAX 200                /* files in a folder sent */
#define OB_FILES_XFERS 16               /* transfers going on at once, both ways */
#define OB_FILES_PATH 260
enum { OB_FILES_MIRROR = 1 };

typedef struct ob_files_item {
    char name[OB_FILES_NAME];
    uint32_t size;
    uint64_t hash;
} ob_files_item;

typedef struct ob_files_xfer {
    int used, out;                      /* out: this machine sends */
    uint32_t id;                        /* the sender's */
    int peer, flags;
    char tag[OB_FILES_NAME];
    char dir[OB_FILES_PATH];
    ob_files_item *item;
    uint8_t *have;                      /* (receiving) by file: 0 wanted, 1 in the target already, 2 staged */
    int n;
    int asked, came;                    /* files asked for, and come (receiving) or sent (sending) */
} ob_files_xfer;

typedef struct ob_files {
    void (*send)(void *ud, int to, const void *data, size_t size);
    void *send_ud;
    /* (receiving) the folder a transfer with this tag from machine `from` goes to (into dir): 1, or 0 refused.
     * NULL: every transfer refused */
    int (*where)(void *ud, int from, const char *tag, char *dir, int size);
    /* a transfer ended: out (1 this machine sent it), peer, tag, ok, and why not ("" ok) */
    void (*done)(void *ud, int out, int peer, const char *tag, int ok, const char *why);
    void *hook_ud;
    uint32_t next_id;
    ob_files_xfer x[OB_FILES_XFERS];
} ob_files;

void ob_files_init(ob_files *f, void (*send)(void *ud, int to, const void *data, size_t size), void *ud);
void ob_files_hooks(ob_files *f, int (*where)(void *, int, const char *, char *, int),
                    void (*done)(void *, int, int, const char *, int, const char *), void *ud);
void ob_files_free(ob_files *f);
/* the plain files of dir, sorted by name ignoring case, with sizes and hashes: their count, -1 no folder */
int ob_files_scan(const char *dir, ob_files_item *item, int max);
/* offers folder dir to machine `to` under tag: 0, or -1 (no folder, too many files, too many transfers) */
int ob_files_send(ob_files *f, int to, const char *tag, const char *dir, int flags);
/* a message of type OB_NET_TYPE_FILES from machine `from` */
void ob_files_message(ob_files *f, int from, const void *data, size_t size);
/* a machine left: its transfers end (not ok; what came of a folder stays staged for the next time) */
void ob_files_machine_gone(ob_files *f, int machine);
/* transfers going on with machine peer (-1: any) */
int ob_files_busy(const ob_files *f, int peer);
/* one line about the transfers ("Sending league: 3 of 9 files"; "" none) */
void ob_files_status(const ob_files *f, char *line, int size);

#ifdef __cplusplus
}
#endif

#endif
