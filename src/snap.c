/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad and contributors */
#include "overbar/snap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>   /* (clock: ISO C) */

#define PAGE 4096u

struct ob_snap_store {
    ob_snap_watch_fn watch;
    uint8_t *base;
    ob_snap_region *br;         /* the regions the base holds */
    int bn;
    unsigned base_id;
    void **wlist;               /* the watch's pages for the region being looked at */
    size_t wcap, wnum;
    struct { uint8_t *dst; uint32_t len; } *pg;
    size_t pgcap;
    double t_scan, t_copy, t_list;
    unsigned saves;
};

static double now_s(void) { return (double)clock() / CLOCKS_PER_SEC; }

ob_snap_store *ob_snap_store_new(ob_snap_watch_fn watch)
{
    ob_snap_store *s = calloc(1, sizeof *s);
    if (s) s->watch = watch;
    return s;
}

void ob_snap_store_free(ob_snap_store *s)
{
    if (!s) return;
    free(s->base); free(s->br); free(s->wlist); free(s->pg);
    free(s);
}

static int base_matches(const ob_snap_store *s, const ob_snap_region *r, int n)
{
    int i;
    if (!s->base || n != s->bn) return 0;
    for (i = 0; i < n; i++) if (r[i].p != s->br[i].p || r[i].n != s->br[i].n) return 0;
    return 1;
}

static int base_take(ob_snap_store *s, const ob_snap_region *r, int n)
{
    size_t total = 0, at = 0;
    int i;
    ob_snap_region *nr = malloc((size_t)(n ? n : 1) * sizeof *nr);
    uint8_t *nb;
    if (!nr) return -1;
    for (i = 0; i < n; i++) total += r[i].n;
    if (s->watch) for (i = 0; i < n; i++) s->watch(r[i].p, r[i].n, NULL, NULL, 1);       /* (the watch starts again) */
    nb = malloc(total ? total : 1);
    if (!nb) { free(nr); return -1; }
    for (i = 0; i < n; i++) { memcpy(nb + at, r[i].p, r[i].n); at += r[i].n; nr[i] = r[i]; }
    free(s->base); free(s->br);
    s->base = nb; s->br = nr; s->bn = n;
    s->base_id++;
    return 0;
}

/* the pages of a watched block written since the base (s->wlist[0..wnum)), or -1 when the region is not such a block */
static int page_list(ob_snap_store *s, const ob_snap_region *r)
{
    size_t want = r->n / PAGE + 2;
    double t0 = now_s();
    int rc;
    if (!s->watch) return -1;
    if (s->wcap < want) {
        void **nl = realloc(s->wlist, want * sizeof *nl);
        if (!nl) return -1;
        s->wlist = nl; s->wcap = want;
    }
    s->wnum = s->wcap;
    rc = s->watch(r->p, r->n, s->wlist, &s->wnum, 0) ? -1 : 0;
    s->t_list += now_s() - t0;
    return rc;
}

int ob_snap_take(ob_snap_store *s, ob_snap *out, const ob_snap_region *r, int n, const void *blob, size_t blob_n)
{
    size_t at = 0, ndiff = 0, bytes = 0, o;
    int i;
    double t0 = now_s(), t1;
    ob_snap_free(out);
    if (!base_matches(s, r, n) && base_take(s, r, n)) return -1;
    out->r = malloc((size_t)(n ? n : 1) * sizeof *out->r);
    if (!out->r) return -1;
    memcpy(out->r, r, (size_t)n * sizeof *r);
    out->nregs = n;
    out->base_id = s->base_id;
    for (i = 0; i < n; i++) {                              /* the pages that differ from the base, once */
        size_t k, q = 0, nq = 0;
        int watched = page_list(s, &r[i]) >= 0;
        if (watched) nq = s->wnum;
        for (k = 0; watched ? q < nq : k < r[i].n; k += PAGE) {
            size_t len;
            if (watched) k = (size_t)((uint8_t *)s->wlist[q++] - (uint8_t *)r[i].p);
            if (k >= r[i].n) continue;
            len = r[i].n - k < PAGE ? r[i].n - k : PAGE;
            if (!memcmp((uint8_t *)r[i].p + k, s->base + at + k, len)) continue;
            if (ndiff == s->pgcap) {
                size_t nc = s->pgcap ? s->pgcap * 2 : 1024;
                void *np = realloc(s->pg, nc * sizeof *s->pg);
                if (!np) { free(out->r); out->r = NULL; return -1; }
                s->pg = np; s->pgcap = nc;
            }
            s->pg[ndiff].dst = (uint8_t *)r[i].p + k; s->pg[ndiff].len = (uint32_t)len;
            ndiff++; bytes += len;
        }
        at += r[i].n;
    }
    t1 = now_s();
    out->size = blob_n + bytes + ndiff * (sizeof(void *) + sizeof(uint32_t));
    out->data = malloc(out->size ? out->size : 1);
    if (!out->data) { free(out->r); out->r = NULL; return -1; }
    if (blob_n) memcpy(out->data, blob, blob_n);
    o = blob_n;
    for (i = 0; (size_t)i < ndiff; i++) {                  /* each page: its address, its length, its bytes */
        memcpy(out->data + o, &s->pg[i].dst, sizeof s->pg[i].dst); o += sizeof s->pg[i].dst;
        memcpy(out->data + o, &s->pg[i].len, sizeof s->pg[i].len); o += sizeof s->pg[i].len;
        memcpy(out->data + o, s->pg[i].dst, s->pg[i].len); o += s->pg[i].len;
    }
    s->t_scan += t1 - t0; s->t_copy += now_s() - t1; s->saves++;
    out->blob = blob_n;
    out->ndiff = ndiff;
    out->size = o;
    out->valid = 1;
    return 0;
}

int ob_snap_fits(const ob_snap_store *s, const ob_snap *snap, const ob_snap_region *r, int n)
{
    int i;
    if (!snap->valid || snap->base_id != s->base_id || n != snap->nregs) return -1;
    for (i = 0; i < n; i++) if (r[i].p != snap->r[i].p || r[i].n != snap->r[i].n) return -1;
    return 0;
}

int ob_snap_put(const ob_snap_store *s, const ob_snap *snap)
{
    size_t at = 0, p;
    int i;
    if (!snap->valid || snap->base_id != s->base_id) return -1;
    for (i = 0; i < snap->nregs; i++) { memcpy(snap->r[i].p, s->base + at, snap->r[i].n); at += snap->r[i].n; }
    for (p = snap->blob; p < snap->size;) {
        void *dst;
        uint32_t len;
        memcpy(&dst, snap->data + p, sizeof dst); p += sizeof dst;
        memcpy(&len, snap->data + p, sizeof len); p += sizeof len;
        memcpy(dst, snap->data + p, len); p += len;
    }
    return 0;
}

const void *ob_snap_blob(const ob_snap *snap, size_t *n)
{
    if (n) *n = snap->blob;
    return snap->data;
}

void ob_snap_free(ob_snap *snap)
{
    free(snap->data); free(snap->r);
    memset(snap, 0, sizeof *snap);
}

const char *ob_snap_stats(const ob_snap_store *s)
{
    static char b[160];
    snprintf(b, sizeof b, "snap: scan %.2f (of it the watch lists %.2f) copy %.2f ms each", s->saves ? s->t_scan * 1e3 / s->saves : 0.0,
             s->saves ? s->t_list * 1e3 / s->saves : 0.0, s->saves ? s->t_copy * 1e3 / s->saves : 0.0);
    return b;
}
