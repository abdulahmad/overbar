/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Files between the machines of a room (overbar/files.h). Messages, little-endian, the first byte says which:
 *   1 OFFER  id u32, flags u8, tag (length u8, bytes), count u16, each file: name (length u8, bytes), size u32, hash u64
 *   2 WANT   id u32, count u16, each: the file's index u16
 *   3 DATA   id u32, index u16, the file's bytes
 *   4 DONE   id u32                       (every file asked for was sent)
 *   5 ACK    id u32, ok u8, why (bytes)   (the receiver: in place, or not)
 *   6 CANCEL id u32, from_sender u8
 * id is the sender's number for the transfer. */
#include "overbar/files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsdir.h"

enum { OFFER = 1, WANT, DATA, DONE, ACK, CANCEL };

typedef struct wr { uint8_t *b; size_t n, cap; int bad; } wr;
static void put(wr *w, const void *p, size_t n)
{
    if (w->bad) return;
    if (w->n + n > w->cap) {
        size_t c = w->cap ? w->cap * 2 : 256;
        uint8_t *nb;
        while (c < w->n + n) c *= 2;
        if (!(nb = (uint8_t *)realloc(w->b, c))) { w->bad = 1; return; }
        w->b = nb;
        w->cap = c;
    }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void put8(wr *w, unsigned v) { uint8_t b = (uint8_t)v; put(w, &b, 1); }
static void put16(wr *w, unsigned v) { uint8_t b[2]; b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); put(w, b, 2); }
static void put32(wr *w, uint32_t v) { put16(w, v & 0xFFFF); put16(w, v >> 16); }
static void put64(wr *w, uint64_t v) { put32(w, (uint32_t)v); put32(w, (uint32_t)(v >> 32)); }
static void putstr(wr *w, const char *s) { size_t n = strlen(s); if (n > 255) n = 255; put8(w, (unsigned)n); put(w, s, n); }

typedef struct rd { const uint8_t *p; size_t n; int bad; } rd;
static const uint8_t *get(rd *r, size_t n) { const uint8_t *q; if (r->bad || r->n < n) { r->bad = 1; return NULL; } q = r->p; r->p += n; r->n -= n; return q; }
static unsigned get8(rd *r) { const uint8_t *q = get(r, 1); return q ? q[0] : 0; }
static unsigned get16(rd *r) { const uint8_t *q = get(r, 2); return q ? (unsigned)(q[0] | q[1] << 8) : 0; }
static uint32_t get32(rd *r) { uint32_t lo = get16(r); return lo | (uint32_t)get16(r) << 16; }
static uint64_t get64(rd *r) { uint64_t lo = get32(r); return lo | (uint64_t)get32(r) << 32; }
static void getstr(rd *r, char *s, size_t cap)
{
    size_t n = get8(r);
    const uint8_t *q = get(r, n);
    if (!q || n >= cap) { s[0] = 0; r->bad = 1; return; }
    memcpy(s, q, n);
    s[n] = 0;
}

void ob_files_init(ob_files *f, void (*send)(void *, int, const void *, size_t), void *ud)
{
    memset(f, 0, sizeof *f);
    f->send = send;
    f->send_ud = ud;
    f->next_id = 1;
}

void ob_files_hooks(ob_files *f, int (*where)(void *, int, const char *, char *, int),
                    void (*done)(void *, int, int, const char *, int, const char *), void *ud)
{
    f->where = where;
    f->done = done;
    f->hook_ud = ud;
}

static void drop(ob_files_xfer *x)
{
    free(x->item);
    free(x->have);
    memset(x, 0, sizeof *x);
}

void ob_files_free(ob_files *f)
{
    int i;
    for (i = 0; i < OB_FILES_XFERS; i++) drop(&f->x[i]);
}

static void emit(ob_files *f, int to, wr *w)
{
    if (!w->bad && f->send) f->send(f->send_ud, to, w->b, w->n);
    free(w->b);
    memset(w, 0, sizeof *w);
}

static void small(ob_files *f, int to, int op, uint32_t id, int a, const char *why)
{
    wr w;
    memset(&w, 0, sizeof w);
    put8(&w, (unsigned)op);
    put32(&w, id);
    if (op == ACK || op == CANCEL) put8(&w, (unsigned)a);
    if (why) put(&w, why, strlen(why));
    emit(f, to, &w);
}

/* the transfer ends: told to the hook, then gone */
static void finish(ob_files *f, ob_files_xfer *x, int ok, const char *why)
{
    int out = x->out, peer = x->peer;
    char tag[OB_FILES_NAME];
    memcpy(tag, x->tag, sizeof tag);
    drop(x);
    if (f->done) f->done(f->hook_ud, out, peer, tag, ok, why ? why : "");
}

int ob_files_scan(const char *dir, ob_files_item *item, int max)
{
    static char name[OB_FILES_MAX + 1][OB_FS_NAME];
    char path[OB_FILES_PATH + OB_FS_NAME + 2];
    int n = ob_fs_list(dir, name, OB_FILES_MAX + 1), i;
    if (n < 0) return -1;
    if (n > max) return -1;
    for (i = 0; i < n; i++) {
        uint8_t *d;
        size_t size;
        ob_fs_join(path, sizeof path, dir, name[i]);
        if (ob_fs_read(path, &d, &size)) return -1;
        memcpy(item[i].name, name[i], OB_FILES_NAME);
        item[i].size = (uint32_t)size;
        item[i].hash = ob_fs_hash(d, size);
        free(d);
    }
    return n;
}

static ob_files_xfer *slot(ob_files *f)
{
    int i;
    for (i = 0; i < OB_FILES_XFERS; i++) if (!f->x[i].used) return &f->x[i];
    return NULL;
}

static ob_files_xfer *find(ob_files *f, int out, int peer, uint32_t id)
{
    int i;
    for (i = 0; i < OB_FILES_XFERS; i++) {
        ob_files_xfer *x = &f->x[i];
        if (x->used && x->out == out && x->id == id && x->peer == peer) return x;
    }
    return NULL;
}

int ob_files_send(ob_files *f, int to, const char *tag, const char *dir, int flags)
{
    ob_files_xfer *x = slot(f);
    wr w;
    int i;
    if (!x || !tag || strlen(tag) >= OB_FILES_NAME || strlen(dir) >= OB_FILES_PATH) return -1;
    memset(x, 0, sizeof *x);
    if (!(x->item = (ob_files_item *)calloc(OB_FILES_MAX, sizeof *x->item))) return -1;
    if ((x->n = ob_files_scan(dir, x->item, OB_FILES_MAX)) < 0) { drop(x); return -1; }
    x->used = 1;
    x->out = 1;
    x->id = f->next_id++;
    x->peer = to;
    x->flags = flags;
    memcpy(x->tag, tag, strlen(tag) + 1);
    memcpy(x->dir, dir, strlen(dir) + 1);
    memset(&w, 0, sizeof w);
    put8(&w, OFFER);
    put32(&w, x->id);
    put8(&w, (unsigned)flags);
    putstr(&w, tag);
    put16(&w, (unsigned)x->n);
    for (i = 0; i < x->n; i++) { putstr(&w, x->item[i].name); put32(&w, x->item[i].size); put64(&w, x->item[i].hash); }
    if (w.bad) { free(w.b); drop(x); return -1; }
    emit(f, to, &w);
    return 0;
}

static void stage_dir(const ob_files_xfer *x, char *out, size_t size) { snprintf(out, size, "%s.obpart", x->dir); }

/* does dir hold `name` (any case) with this size and hash? */
static int holds(const char *dir, const ob_files_item *it)
{
    char real[OB_FS_NAME], path[OB_FILES_PATH + OB_FS_NAME + 16];
    uint8_t *d;
    size_t size;
    int same;
    if (!ob_fs_find(dir, it->name, real, sizeof real)) return 0;
    ob_fs_join(path, sizeof path, dir, real);
    if (ob_fs_read(path, &d, &size)) return 0;
    same = size == it->size && ob_fs_hash(d, size) == it->hash;
    free(d);
    return same;
}

static int safe_name(const char *s)
{
    return s[0] && s[0] != '.' && !strchr(s, '/') && !strchr(s, '\\') && !strchr(s, ':') && s[strlen(s) - 1] != '~';
}

static void got_offer(ob_files *f, int from, rd *r)
{
    ob_files_xfer *x, *old;
    char tag[OB_FILES_NAME], stage[OB_FILES_PATH + 16];
    uint32_t id = get32(r);
    int flags = (int)get8(r), n, i, want = 0;
    wr w;
    getstr(r, tag, sizeof tag);
    n = (int)get16(r);
    if (r->bad || n > OB_FILES_MAX) return;
    for (i = 0; i < OB_FILES_XFERS; i++) {          /* the same folder offered again: the new offer wins */
        old = &f->x[i];
        if (old->used && !old->out && old->peer == from && !strcmp(old->tag, tag)) drop(old);
    }
    if (!(x = slot(f))) { small(f, from, ACK, id, 0, "too many transfers"); return; }
    memset(x, 0, sizeof *x);
    x->item = (ob_files_item *)calloc((size_t)(n ? n : 1), sizeof *x->item);
    x->have = (uint8_t *)calloc((size_t)(n ? n : 1), 1);
    if (!x->item || !x->have) { drop(x); small(f, from, ACK, id, 0, "out of memory"); return; }
    for (i = 0; i < n; i++) {
        getstr(r, x->item[i].name, OB_FILES_NAME);
        x->item[i].size = get32(r);
        x->item[i].hash = get64(r);
        if (!r->bad && !safe_name(x->item[i].name)) r->bad = 1;
    }
    if (r->bad) { drop(x); small(f, from, ACK, id, 0, "a damaged offer"); return; }
    if (!f->where || !f->where(f->hook_ud, from, tag, x->dir, sizeof x->dir)) { drop(x); small(f, from, ACK, id, 0, "refused"); return; }
    x->used = 1;
    x->id = id;
    x->peer = from;
    x->flags = flags;
    x->n = n;
    memcpy(x->tag, tag, sizeof tag);
    stage_dir(x, stage, sizeof stage);
    if (ob_fs_mkdir(x->dir) || ob_fs_mkdir(stage)) { finish(f, x, 0, "could not make the folder"); small(f, from, ACK, id, 0, "could not make the folder"); return; }
    memset(&w, 0, sizeof w);
    put8(&w, WANT);
    put32(&w, id);
    for (i = 0; i < n; i++) {
        if (holds(x->dir, &x->item[i])) x->have[i] = 1;
        else if (holds(stage, &x->item[i])) x->have[i] = 2;
        else want++;
    }
    put16(&w, (unsigned)want);
    for (i = 0; i < n; i++) if (!x->have[i]) put16(&w, (unsigned)i);
    x->asked = want;
    emit(f, from, &w);
}

static void got_want(ob_files *f, int from, rd *r)
{
    ob_files_xfer *x = find(f, 1, from, get32(r));
    int n = (int)get16(r), k;
    char path[OB_FILES_PATH + OB_FS_NAME + 2];
    if (!x || r->bad) return;
    x->asked = n;
    for (k = 0; k < n; k++) {
        int i = (int)get16(r);
        uint8_t *d;
        size_t size;
        wr w;
        if (r->bad || i >= x->n) { small(f, from, CANCEL, x->id, 1, NULL); finish(f, x, 0, "a damaged answer"); return; }
        ob_fs_join(path, sizeof path, x->dir, x->item[i].name);
        if (ob_fs_read(path, &d, &size) || size != x->item[i].size || ob_fs_hash(d, size) != x->item[i].hash) {
            if (d) free(d);
            small(f, from, CANCEL, x->id, 1, NULL);
            finish(f, x, 0, "a file changed while it was sent");
            return;
        }
        memset(&w, 0, sizeof w);
        put8(&w, DATA);
        put32(&w, x->id);
        put16(&w, (unsigned)i);
        put(&w, d, size);
        free(d);
        if (w.bad) { free(w.b); small(f, from, CANCEL, x->id, 1, NULL); finish(f, x, 0, "out of memory"); return; }
        emit(f, from, &w);
        x->came++;
    }
    small(f, from, DONE, x->id, 0, NULL);
}

static void got_data(ob_files *f, int from, rd *r)
{
    ob_files_xfer *x = find(f, 0, from, get32(r));
    int i = (int)get16(r);
    char stage[OB_FILES_PATH + 16], path[OB_FILES_PATH + OB_FS_NAME + 20];
    if (!x || r->bad || i >= x->n) return;
    if (r->n != x->item[i].size || ob_fs_hash(r->p, r->n) != x->item[i].hash) {
        small(f, from, ACK, x->id, 0, "a file came damaged");
        finish(f, x, 0, "a file came damaged");
        return;
    }
    stage_dir(x, stage, sizeof stage);
    ob_fs_join(path, sizeof path, stage, x->item[i].name);
    if (ob_fs_write(path, r->p, r->n)) {
        small(f, from, ACK, x->id, 0, "could not write a file");
        finish(f, x, 0, "could not write a file");
        return;
    }
    if (!x->have[i]) x->came++;
    x->have[i] = 2;
}

static int in_set(const ob_files_xfer *x, const char *name)
{
    int i;
    for (i = 0; i < x->n; i++) if (!ob_fs_stricmp(x->item[i].name, name)) return 1;
    return 0;
}

static void got_done(ob_files *f, int from, rd *r)
{
    static char list[256][OB_FS_NAME];
    ob_files_xfer *x = find(f, 0, from, get32(r));
    char stage[OB_FILES_PATH + 16], src[OB_FILES_PATH + OB_FS_NAME + 20], dst[OB_FILES_PATH + OB_FS_NAME + 20], real[OB_FS_NAME];
    int i, n;
    if (!x) return;
    for (i = 0; i < x->n; i++)
        if (!x->have[i]) { small(f, from, ACK, x->id, 0, "a file did not come"); finish(f, x, 0, "a file did not come"); return; }
    stage_dir(x, stage, sizeof stage);
    for (i = 0; i < x->n; i++) {
        if (x->have[i] != 2) continue;
        if (ob_fs_find(x->dir, x->item[i].name, real, sizeof real)) {     /* (the old one, whatever its case) */
            ob_fs_join(dst, sizeof dst, x->dir, real);
            remove(dst);
        }
        ob_fs_join(src, sizeof src, stage, x->item[i].name);
        ob_fs_join(dst, sizeof dst, x->dir, x->item[i].name);
        if (rename(src, dst)) { small(f, from, ACK, x->id, 0, "could not put a file in place"); finish(f, x, 0, "could not put a file in place"); return; }
    }
    if (x->flags & OB_FILES_MIRROR) {
        n = ob_fs_list(x->dir, list, 256);
        for (i = 0; i < n; i++)
            if (!in_set(x, list[i])) { ob_fs_join(dst, sizeof dst, x->dir, list[i]); remove(dst); }
    }
    ob_fs_clear(stage);
    small(f, from, ACK, x->id, 1, NULL);
    finish(f, x, 1, NULL);
}

void ob_files_message(ob_files *f, int from, const void *data, size_t size)
{
    rd r;
    ob_files_xfer *x;
    r.p = (const uint8_t *)data;
    r.n = size;
    r.bad = 0;
    switch (get8(&r)) {
    case OFFER: got_offer(f, from, &r); break;
    case WANT: got_want(f, from, &r); break;
    case DATA: got_data(f, from, &r); break;
    case DONE: got_done(f, from, &r); break;
    case ACK: {
        uint32_t id = get32(&r);
        int ok = (int)get8(&r);
        char why[120];
        size_t n = r.n < sizeof why - 1 ? r.n : sizeof why - 1;
        memcpy(why, r.p, n);
        why[n] = 0;
        if ((x = find(f, 1, from, id))) finish(f, x, ok, ok ? NULL : why);
        break;
    }
    case CANCEL: {
        uint32_t id = get32(&r);
        int from_sender = (int)get8(&r);
        if ((x = find(f, from_sender ? 0 : 1, from, id))) finish(f, x, 0, "the other side gave up");
        break;
    }
    default: break;
    }
}

void ob_files_machine_gone(ob_files *f, int machine)
{
    int i;
    for (i = 0; i < OB_FILES_XFERS; i++)
        if (f->x[i].used && f->x[i].peer == machine) finish(f, &f->x[i], 0, "the other machine left");
}

int ob_files_busy(const ob_files *f, int peer)
{
    int i, n = 0;
    for (i = 0; i < OB_FILES_XFERS; i++) n += f->x[i].used && (peer < 0 || f->x[i].peer == peer);
    return n;
}

void ob_files_status(const ob_files *f, char *line, int size)
{
    int i;
    line[0] = 0;
    for (i = 0; i < OB_FILES_XFERS; i++) {
        const ob_files_xfer *x = &f->x[i];
        if (!x->used) continue;
        if (x->out) snprintf(line, (size_t)size, "Sending %s: %d of %d files", x->tag, x->came, x->n);
        else snprintf(line, (size_t)size, "Receiving %s: %d of %d files", x->tag, x->came, x->asked);
        return;
    }
}
