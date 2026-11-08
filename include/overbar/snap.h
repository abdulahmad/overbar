/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * States of a game's memory, for rollback (net.h). A state is a set of memory regions as they were, plus a small blob of the
 * caller's (a CPU's registers, a thread's stack, a device's variables). Only the pages that differ from a base copy are kept:
 * the base is a full copy of the regions taken with the first state (and again when the regions change, which makes the older
 * states unusable). Most of a game's memory is its assets and never changes, so a state costs the pages the game has written
 * since the base. A caller whose big blocks are write-watched (Windows: VirtualAlloc MEM_WRITE_WATCH, GetWriteWatch) passes a
 * watch function, and then only the pages written since the base are compared. Single threaded: the game's thread takes and puts
 * states, with nothing else changing the regions meanwhile. */
#ifndef OVERBAR_SNAP_H
#define OVERBAR_SNAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct ob_snap_region { void *p; size_t n; } ob_snap_region;

/* The pages of the block [addr, addr + size) written since the last reset: 0, with the pages' addresses in pages[] (room for *n of
 * them) and their count in *n, or -1 (not a watched block: every page is compared). pages == NULL: forget what was written (the
 * base was just taken). reset: with the list, forget them too (unused by the store). */
typedef int (*ob_snap_watch_fn)(void *addr, size_t size, void **pages, size_t *n, int reset);

typedef struct ob_snap_store ob_snap_store;
ob_snap_store *ob_snap_store_new(ob_snap_watch_fn watch);   /* watch may be NULL */
void ob_snap_store_free(ob_snap_store *s);

typedef struct ob_snap {
    uint8_t *data;              /* the blob, then the changed pages (address, length, bytes) */
    size_t size;                /* all of it */
    size_t blob;                /* the blob's size */
    size_t ndiff;               /* pages that differ from the base */
    unsigned base_id;           /* the base this state is a difference to */
    ob_snap_region *r;          /* the regions as they were (copied) */
    int nregs;
    int valid;
} ob_snap;

/* take a state of the regions r[0..n) with blob (blob_n bytes, may be none). 0, or -1 (out of memory). `out` may hold an older
 * state: it is freed first. */
int ob_snap_take(ob_snap_store *s, ob_snap *out, const ob_snap_region *r, int n, const void *blob, size_t blob_n);
/* the regions now are the state's (no block came or went, the base is still its base): 0, else -1 */
int ob_snap_fits(const ob_snap_store *s, const ob_snap *snap, const ob_snap_region *r, int n);
/* put the regions' bytes back: 0, or -1 (the state is not usable any more) */
int ob_snap_put(const ob_snap_store *s, const ob_snap *snap);
const void *ob_snap_blob(const ob_snap *snap, size_t *n);
void ob_snap_free(ob_snap *snap);
/* where the time of ob_snap_take goes, a line of text for logs */
const char *ob_snap_stats(const ob_snap_store *s);

#endif
