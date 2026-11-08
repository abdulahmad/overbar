/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors */
#include "overbar/rollback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

#define NOINLINE __attribute__((noinline))

typedef struct rb_slot {
    ob_snap os;                 /* the memory, and the stack as its blob */
    char *low;                  /* where the stack copy starts */
    size_t stack;
    void *jb[5];                /* the resumable point's __builtin_setjmp buffer as it was */
    uint32_t gen, tag;
} rb_slot;

typedef struct { uint32_t slot, gen; } rb_blob;

struct ob_rollback {
    ob_rollback_game g;
    char *base;
    ob_snap_store *store;
    rb_slot *s;
    int ns;
    uint32_t next, genc;
    int target;
    void *jb[5];                /* set by ob_rollback_frame */
    char *low;                  /* below ob_rollback_frame's frame */
    ob_snap_region *r;
    int maxr;
    ob_net_states st;
    int (*step)(void *);        /* (kept here, not on the stack, for after a jump) */
    void *step_user;
    unsigned saves;
    double save_s;
    char line[200];
};

static double now_s(void) { return (double)clock() / CLOCKS_PER_SEC; }

/* an address below the caller's frame */
static NOINLINE char *below_caller(void) { return (char *)__builtin_frame_address(0) - 512; }

static int gather(ob_rollback *rb)
{
    int n = rb->g.regions(rb->g.user, rb->r, rb->maxr);
    return n < 0 || n > rb->maxr ? -1 : n;
}

static size_t rb_save(void *user, void *out, size_t cap)
{
    ob_rollback *rb = user;
    rb_slot *sl = &rb->s[rb->next % (uint32_t)rb->ns];
    rb_blob b;
    uint32_t tag = 0;
    double t0 = now_s();
    int n, bad;
    if (cap < sizeof b) return sizeof b;
    if (!rb->low || rb->low >= rb->base) return 0;
    if (rb->g.hold) tag = rb->g.hold(rb->g.user);
    sl->gen = 0;
    n = gather(rb);
    sl->stack = (size_t)(rb->base - rb->low);
    bad = n < 0 || ob_snap_take(rb->store, &sl->os, rb->r, n, rb->low, sl->stack);
    if (rb->g.release) rb->g.release(rb->g.user, tag, 0);
    if (bad) return 0;
    sl->low = rb->low;
    sl->tag = tag;
    memcpy(sl->jb, rb->jb, sizeof sl->jb);
    sl->gen = ++rb->genc;
    b.slot = rb->next % (uint32_t)rb->ns;
    b.gen = sl->gen;
    rb->next++;
    rb->saves++;
    rb->save_s += now_s() - t0;
    memcpy(out, &b, sizeof b);
    return sizeof b;
}

static int rb_load(void *user, const void *in, size_t size)
{
    ob_rollback *rb = user;
    rb_blob b;
    rb_slot *sl;
    int n;
    if (size != sizeof b) return -1;
    memcpy(&b, in, sizeof b);
    if (b.slot >= (uint32_t)rb->ns) return -1;
    sl = &rb->s[b.slot];
    if (!sl->gen || sl->gen != b.gen || !sl->os.valid) return -1;
    if (rb->g.hold) (void)rb->g.hold(rb->g.user);     /* (off until back at the state: release there) */
    n = gather(rb);
    if (n < 0 || ob_snap_fits(rb->store, &sl->os, rb->r, n) || ob_snap_put(rb->store, &sl->os)) {
        if (rb->g.release) rb->g.release(rb->g.user, sl->tag, 1);
        return -1;
    }
    rb->target = (int)b.slot;
    return 0;
}

ob_rollback *ob_rollback_new(const ob_rollback_game *g, void *stack_base)
{
    ob_rollback *rb = calloc(1, sizeof *rb);
    if (!rb || !g || !g->regions || !stack_base) { free(rb); return NULL; }
    rb->g = *g;
    rb->ns = g->slots > 0 ? g->slots : 32;
    rb->maxr = g->max_regions > 0 ? g->max_regions : 1024;
    rb->base = (char *)stack_base;
    rb->s = calloc((size_t)rb->ns, sizeof *rb->s);
    rb->r = calloc((size_t)rb->maxr, sizeof *rb->r);
    rb->store = ob_snap_store_new(g->watch);
    if (!rb->s || !rb->r || !rb->store) { ob_rollback_free(rb); return NULL; }
    rb->st.user = rb;
    rb->st.save = rb_save;
    rb->st.load = rb_load;
    return rb;
}

void ob_rollback_reset(ob_rollback *rb)
{
    int i;
    if (!rb) return;
    for (i = 0; i < rb->ns; i++) { ob_snap_free(&rb->s[i].os); memset(&rb->s[i], 0, sizeof rb->s[i]); }
    rb->next = 0;
}

void ob_rollback_free(ob_rollback *rb)
{
    if (!rb) return;
    if (rb->s) ob_rollback_reset(rb);
    if (rb->store) ob_snap_store_free(rb->store);
    free(rb->s);
    free(rb->r);
    free(rb);
}

const ob_net_states *ob_rollback_states(ob_rollback *rb) { return rb ? &rb->st : NULL; }

/* (called below every byte the state's stack puts back, so this frame is not overwritten) */
static NOINLINE void put_back(ob_rollback *rb, const rb_slot *sl)
{
    memcpy(sl->low, ob_snap_blob(&sl->os, NULL), sl->stack);
    memcpy(rb->jb, sl->jb, sizeof rb->jb);
    __builtin_longjmp(rb->jb, 1);
}

static NOINLINE void jump(ob_rollback *rb, const rb_slot *sl)
{
    char *sp = below_caller();
    if (sp > sl->low - 8192) {
        volatile char *pad = alloca((size_t)(sp - (sl->low - 8192)));
        pad[0] = 0;
        put_back(rb, sl);
    }
    put_back(rb, sl);
    __builtin_unreachable();
}

static ob_rollback *volatile landing;   /* the ob_rollback a jump goes back into */

int ob_rollback_frame(ob_rollback *rb, int (*step)(void *user), void *user)
{
    int r;
    rb->step = step;
    rb->step_user = user;
    landing = rb;
    if (__builtin_setjmp(rb->jb)) {         /* back from a jump: the memory and this stack are the state's */
        ob_rollback *b = landing;
        if (b->g.release) b->g.release(b->g.user, b->s[b->target].tag, 1);
    }
    rb = landing;
    rb->low = below_caller();
    r = rb->step(rb->step_user);
    rb = landing;
    if (r == OB_NET_RESTART) jump(rb, &rb->s[rb->target]);
    return r;
}

const char *ob_rollback_stats(ob_rollback *rb)
{
    const rb_slot *l;
    if (!rb) return "";
    l = &rb->s[(rb->next + (uint32_t)rb->ns - 1) % (uint32_t)rb->ns];
    snprintf(rb->line, sizeof rb->line, "%u states, %.1f ms each, last %zu pages %zu bytes; %s", rb->saves,
             rb->saves ? rb->save_s * 1000.0 / rb->saves : 0.0, l->os.ndiff, l->os.size, ob_snap_stats(rb->store));
    return rb->line;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *q = p;
    size_t k;
    for (k = 0; k < n; k++) { h ^= q[k]; h *= 1099511628211ULL; }
    return h;
}

uint64_t ob_rollback_hash(ob_rollback *rb)
{
    uint64_t h = 1469598103934665603ULL;
    int n = rb ? gather(rb) : -1, i;
    for (i = 0; i < n; i++) h = fnv(h, rb->r[i].p, rb->r[i].n);
    return h;
}

int ob_rollback_region_hashes(ob_rollback *rb, uint64_t *h, int max)
{
    int n = rb ? gather(rb) : -1, i;
    for (i = 0; i < n && i < max; i++) h[i] = fnv(1469598103934665603ULL, rb->r[i].p, rb->r[i].n);
    return n < 0 ? 0 : i;
}
