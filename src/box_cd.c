/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The CD box (overbar/box_cd.h): the big box of NHL 95, 96 and 97 PC and what came in it. States: the closed box ->
 * (it opens, everything comes out) the layout -> a book (manual, tech card) | the jewel case (it opens; the disc) | a card
 * (flips) | a foldout (unfolds, turns over). The models are the mockup's cd3d.js; the install is box_cd_install.c. */
#include "overbar/box_cd.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const bx_cd_game *BX_CD;

enum { S_BOX, S_OPEN, S_LAID, S_BOOK, S_JEWEL, S_CARD };
enum { K_BOOK, K_JEWEL, K_FLIP, K_FOLD };
static int st, busy, ready, focus = -1;
static int rig, cbox, flap;

typedef struct { int root, g, leaf[64], n, blk, bc, sp; float w, t, flip; } book;
typedef struct { int root, g, p[8], n; float pw, flip; } foldout;
typedef struct { int root, g, across; float flip; } card;
typedef struct {
    int node, kind;                    /* item.<id> */
    book *b; foldout *f; card *c;
    float w, h, home[3], hrot[3], hscale;
    const char *label;
} item;
static book mB, tB;
static foldout lineF, maskF;
static card regC, newsC, couponC;
static struct { int root, lid, base, disc; float open; } J;
static item IT[12]; static int nit;
static float box_top[3];

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int opt_node(const char *n) { return r3_find(BX_S, n); }

static int find_book(book *b, const char *n, float w, float t)
{
    char k[64];
    memset(b, 0, sizeof *b);
    b->root = opt_node(n);
    if (b->root < 0) return 0;
    snprintf(k, sizeof k, "%s.g", n); b->g = bx_node(k);
    snprintf(k, sizeof k, "%s.blk", n); b->blk = bx_node(k);
    snprintf(k, sizeof k, "%s.bc", n); b->bc = bx_node(k);
    snprintf(k, sizeof k, "%s.sp", n); b->sp = bx_node(k);
    for (int i = 0; i < 64; i++) { snprintf(k, sizeof k, "%s.leaf%d", n, i); int x = r3_find(BX_S, k); if (x < 0) break; b->leaf[b->n++] = x; }
    b->w = w; b->t = t;
    return 1;
}
static void book_set(book *b)
{
    if (b->root < 0 || !b->n) return;
    float f = b->flip;
    for (int k = 0; k < b->n; k++) {
        float a = bx_ease(clamp01(f - k));
        r3_node *L = bx_n(b->leaf[k]);
        L->rot[1] = -PI_F * a;
        float zc = k == 0 ? b->t / 2 : -k * .002f, zo = k * .002f;
        L->pos[2] = zc + (zo - zc) * a + sinf(a * PI_F) * .12f;
    }
    bx_n(b->g)->pos[0] = f <= 1 ? (-b->w / 2 + (b->w / 2) * f) : (f >= b->n - 1 ? (b->w / 2) * (f - (b->n - 1)) : 0);
    bx_n(b->blk)->vis = f < .001f;
    bx_n(b->bc)->vis = f < b->n - .5f;
    bx_n(b->sp)->vis = f < .05f;
}
static int find_fold(foldout *f, const char *n)
{
    char k[64];
    memset(f, 0, sizeof *f);
    f->root = opt_node(n);
    if (f->root < 0) return 0;
    snprintf(k, sizeof k, "%s.g", n); f->g = bx_node(k);
    for (int i = 0; i < 8; i++) { snprintf(k, sizeof k, "%s.p%d", n, i); int x = r3_find(BX_S, k); if (x < 0) break; f->p[f->n++] = x; }
    f->pw = f->n > 1 ? fabsf(bx_n(f->p[1])->pos[0]) : 4;
    return 1;
}
/* 0 folded (the cover up), 1 open (the outside up), 2 open and turned over (the inside) */
static void fold_set(foldout *f)
{
    if (f->root < 0) return;
    float u = clamp01(f->flip), turn = clamp01(f->flip - 1), d = .006f;
    for (int i = 1; i < f->n; i++) {
        r3_node *P = bx_n(f->p[i]);
        P->rot[1] = (i & 1 ? -1.0f : 1.0f) * PI_F * (1 - bx_ease(u));
        P->pos[2] = (i & 1 ? -d : d) * (1 - u);
    }
    bx_n(f->g)->pos[0] = f->pw / 2 + (f->n - 1) * f->pw / 2 * bx_ease(u);
    bx_n(f->g)->rot[1] = PI_F * bx_ease(turn);
    if (turn > 0) bx_n(f->g)->pos[0] = (f->n * f->pw / 2) * cosf(PI_F * bx_ease(turn));
}
static int find_card(card *c, const char *n)
{
    char k[64];
    memset(c, 0, sizeof *c);
    c->root = opt_node(n);
    if (c->root < 0) return 0;
    snprintf(k, sizeof k, "%s.g", n); c->g = bx_node(k);
    /* a postcard printed across on the back: its back plane is turned a quarter */
    for (int i = 0; i < BX_S->nnodes; i++) if (BX_S->nodes[i].parent == c->g && fabsf(BX_S->nodes[i].rot[2]) > 1) c->across = 1;
    return 1;
}
static void card_set(card *c)
{
    if (c->root < 0) return;
    float a = bx_ease(clamp01(c->flip));
    bx_n(c->g)->rot[1] = -PI_F * a;
    bx_n(c->g)->rot[2] = c->across ? PI_F / 2 * a : 0;
}
/* the half width of the first flat part under node: a card's front */
static float front_w(int g)
{
    for (int i = 0; i < BX_S->nmeshes; i++) {
        r3_mesh *m = &BX_S->meshes[i];
        if (m->node != g && BX_S->nodes[m->node].parent != g) continue;
        float mx = 0;
        for (int v = 0; v < m->nv; v++) mx = fmaxf(mx, fabsf(m->v[v * 8]));
        return 2 * mx;
    }
    return 4;
}
static float front_h(int g)
{
    for (int i = 0; i < BX_S->nmeshes; i++) {
        r3_mesh *m = &BX_S->meshes[i];
        if (m->node != g && BX_S->nodes[m->node].parent != g) continue;
        float mx = 0;
        for (int v = 0; v < m->nv; v++) mx = fmaxf(mx, fabsf(m->v[v * 8 + 1]));
        return 2 * mx;
    }
    return 4;
}

static void add_item(const char *id, int kind, void *p, float w, float h, const char *label)
{
    char k[48]; snprintf(k, sizeof k, "item.%s", id);
    int n = opt_node(k);
    if (n < 0) return;
    item *t = &IT[nit++];
    memset(t, 0, sizeof *t);
    t->node = n; t->kind = kind; t->w = w; t->h = h; t->label = label;
    if (kind == K_BOOK) t->b = (book *)p; else if (kind == K_FOLD) t->f = (foldout *)p; else if (kind == K_FLIP) t->c = (card *)p;
}
/* the layout: the books and the jewel case along the top, the cards along the bottom; the box at the left */
static void layout(void)
{
    float gap = 1.0f, cx = 3.0f;
    float wt = 0, wb = 0;
    for (int i = 0; i < nit; i++) { if (IT[i].kind == K_BOOK || IT[i].kind == K_JEWEL) wt += IT[i].w + gap; else wb += IT[i].w + gap; }
    float xt = cx - (wt - gap) / 2, xb = cx - (wb - gap) / 2;
    for (int i = 0; i < nit; i++) {
        item *t = &IT[i];
        int top = t->kind == K_BOOK || t->kind == K_JEWEL;
        float *x = top ? &xt : &xb;
        bx_set3(t->home, *x + t->w / 2, top ? 5.0f : -5.2f, .4f);
        bx_set3(t->hrot, 0, 0, 0);
        t->hscale = 1;
        *x += t->w + gap;
    }
}
static void init(void)
{
    if (ready) return;
    rig = bx_node("rig"); cbox = bx_node("cbox"); flap = bx_node("cbox.flap");
    nit = 0;
    if (find_book(&mB, "mBook", 5.5f, .16f)) add_item("manual", K_BOOK, &mB, 5.5f, 7, "Manual");
    if (find_book(&tB, "tBook", 5.5f, .06f)) add_item("tech", K_BOOK, &tB, 5.5f, 7, "Technical reference card");
    J.root = opt_node("item.jewel");
    if (J.root >= 0) { J.lid = bx_node("jewel.lid"); J.base = bx_node("jewel.base"); J.disc = bx_node("jewel.disc"); add_item("jewel", K_JEWEL, NULL, 5.6f, 4.92f, "Jewel case"); }
    if (find_card(&regC, "regFlip")) add_item("reg", K_FLIP, &regC, front_w(regC.g), front_h(regC.g), "Registration card");
    if (find_fold(&lineF, "line")) add_item("line", K_FOLD, &lineF, lineF.pw, front_h(lineF.p[0]), "EA lineup (folded)");
    if (find_fold(&maskF, "mask")) add_item("mask", K_FOLD, &maskF, maskF.pw, front_h(maskF.p[0]), "Mini-mask pamphlet (folded)");
    if (find_card(&newsC, "newsFlip")) add_item("news", K_FLIP, &newsC, front_w(newsC.g), front_h(newsC.g), "The Hockey News card");
    if (find_card(&couponC, "couponFlip")) add_item("coupon", K_FLIP, &couponC, front_w(couponC.g), front_h(couponC.g), "Champs Sports coupon");
    layout();
    ready = 1;
}
static item *item_of(int node) { for (int i = 0; i < nit; i++) if (IT[i].node == node) return &IT[i]; return NULL; }

static void caption(void)
{
    static const char *t[] = { "The box", "The box", "Everything in the box", "", "Jewel case", "Look closer" };
    snprintf(BX.title, sizeof BX.title, "%s", t[st]);
    item *f = focus >= 0 ? item_of(focus) : NULL;
    switch (st) {
    case S_BOX: bx_say("<b>The box.</b> Drag to spin it, pick it to open it."); break;
    case S_OPEN: bx_say(""); break;
    case S_LAID: bx_say(BX.first && !BX.from_menu ? "<b>Everything that came in the box.</b> To install, pick the jewel case." : "<b>Everything that came in the box.</b> Pick an item to look at it."); break;
    case S_BOOK: snprintf(BX.title, sizeof BX.title, "%s", f ? f->label : ""); bx_say("<b>Pick the right or left page, or use the buttons, to turn pages.</b>"); break;
    case S_JEWEL:
        bx_say(J.open < .5f ? "<b>Pick the case to open it.</b>" : BX.first && !BX.from_menu ? "<b>Pick the disc</b> to put it in the CD-ROM drive." : "<b>Pick the disc to look at it,</b> or the case to close it.");
        break;
    case S_CARD:
        snprintf(BX.title, sizeof BX.title, "%s", f ? f->label : "");
        bx_say(f && f->kind == K_FOLD ? "<b>Pick it to unfold it, again to turn it over, again to fold it.</b>" : "<b>Pick the card to flip it over.</b>");
        break;
    }
}

/* ---- poses ---- */
static void items_in_box(void)
{
    for (int i = 0; i < nit; i++) {
        r3_node *m = bx_n(IT[i].node);
        m->vis = 0; bx_set3(m->pos, 0, 0, -60); bx_set3(m->scale, .2f, .2f, .2f); bx_set3(m->rot, 0, 0, 0);
    }
}
static void reset_pose(void)
{
    bx_time_clear();
    r3_node *r = bx_n(rig);
    bx_set3(r->pos, 0, 0, 0); bx_set3(r->rot, .12f, -.5f, 0); bx_set3(r->scale, 1, 1, 1); r->vis = 1;
    bx_set3(bx_n(flap)->rot, 0, 0, 0);
    items_in_box();
    mB.flip = tB.flip = 0; lineF.flip = maskF.flip = 0; regC.flip = newsC.flip = couponC.flip = 0; J.open = 0;
    bx_cam(0, 0, 30);
    busy = 0; focus = -1;
}
static void rig_aside(float ms)
{
    float p[3] = { -15.8f, -.5f, -3 }, ro[3] = { .25f, .55f, 0 };
    if (ms <= 0) { bx_set3(bx_n(rig)->pos, p[0], p[1], p[2]); bx_set3(bx_n(rig)->rot, ro[0], ro[1], ro[2]); bx_set3(bx_n(rig)->scale, .55f, .55f, .55f); bx_n(flap)->rot[0] = -2.1f; return; }
    bx_tw3(bx_n(rig)->pos, p[0], p[1], p[2], ms, 0); bx_tw3(bx_n(rig)->rot, ro[0], ro[1], ro[2], ms, 0); bx_tw3(bx_n(rig)->scale, .55f, .55f, .55f, ms, 0);
}
static void laid_now(void)
{
    reset_pose();
    rig_aside(0);
    for (int i = 0; i < nit; i++) {
        r3_node *m = bx_n(IT[i].node);
        m->vis = 1; memcpy(m->pos, IT[i].home, sizeof m->pos); bx_set3(m->scale, 1, 1, 1); bx_set3(m->rot, 0, 0, 0);
    }
    bx_cam(0, 0, 40);
    st = S_LAID;
}

static void unbusy(int a) { (void)a; busy = 0; }
static void laid_done(int a) { (void)a; st = S_LAID; caption(); busy = 0; }
/* the box opens: the flap swings up, the box tips toward you and goes aside, everything comes out over its open top */
static void open_step2(int a)
{
    (void)a;
    r3_world_pos(BX_S, flap, box_top);
    rig_aside(1100);
    bx_cam_to(0, 0, 40, 1100);
    for (int i = 0; i < nit; i++) {
        r3_node *m = bx_n(IT[i].node);
        m->vis = 1; bx_set3(m->pos, box_top[0], box_top[1] + 1, box_top[2]); bx_set3(m->scale, .15f, .15f, .15f); bx_set3(m->rot, -.6f, .4f, 0);
        float d = 250 + i * 110.0f;
        bx_tw3(m->pos, IT[i].home[0], IT[i].home[1], IT[i].home[2], 1000, d);
        bx_tw3(m->scale, 1, 1, 1, 1000, d); bx_tw3(m->rot, 0, 0, 0, 1000, d);
    }
    bx_after(1450 + nit * 110.0f, laid_done, 0);
}
static void to_open(void)
{
    busy = 1; st = S_OPEN; caption();
    bx_sfx(SFX_SLIDE, 0);
    bx_tw3(bx_n(rig)->rot, .55f, -.25f, 0, 700, 0);
    bx_tw1(&bx_n(flap)->rot[0], -2.1f, 800, 300);
    bx_after(1150, open_step2, 0);
}
static void close_step2(int a)
{
    (void)a;
    items_in_box();
    bx_tw1(&bx_n(flap)->rot[0], 0, 700, 300);
    bx_tw3(bx_n(rig)->pos, 0, 0, 0, 1000, 0); bx_tw3(bx_n(rig)->rot, .12f, -.5f, 0, 1000, 0); bx_tw3(bx_n(rig)->scale, 1, 1, 1, 1000, 0);
    bx_cam_to(0, 0, 30, 1000);
    bx_sfx(SFX_SLIDE, 0);
    bx_after(1100, unbusy, 0);
}
static void to_closed(void)
{
    busy = 1; st = S_BOX; caption();
    r3_world_pos(BX_S, flap, box_top);
    for (int i = 0; i < nit; i++) {
        r3_node *m = bx_n(IT[i].node);
        float d = i * 60.0f;
        bx_tw3(m->pos, box_top[0], box_top[1] + .5f, box_top[2], 700, d); bx_tw3(m->scale, .12f, .12f, .12f, 700, d);
    }
    mB.flip = tB.flip = 0; lineF.flip = maskF.flip = 0; regC.flip = newsC.flip = couponC.flip = 0; J.open = 0;
    bx_after(750 + nit * 60.0f, close_step2, 0);
}
static void focus_on(int node)
{
    item *t = item_of(node);
    if (!t) return;
    focus = node;
    st = t->kind == K_BOOK ? S_BOOK : t->kind == K_JEWEL ? S_JEWEL : S_CARD;
    for (int i = 0; i < nit; i++) if (IT[i].node != node) bx_n(IT[i].node)->vis = 0;
    bx_n(rig)->vis = 0;
    r3_node *m = bx_n(node);
    float s = t->kind == K_JEWEL ? 1.7f : t->kind == K_BOOK ? 1.35f : 1.5f;
    bx_tw3(m->pos, 0, t->kind == K_JEWEL ? .3f : 0, 0, 900, 0); bx_tw3(m->scale, s, s, s, 900, 0);
    bx_tw3(m->rot, t->kind == K_JEWEL ? -.25f : 0, 0, 0, 900, 0);
    bx_cam_to(0, 0, t->kind == K_BOOK ? 24 : 26, 900);
    caption();
}
static void unfocus(void)
{
    item *t = item_of(focus);
    if (!t) { st = S_LAID; caption(); return; }
    if (t->b) bx_tw1(&t->b->flip, 0, 600, 0);
    if (t->f) bx_tw1(&t->f->flip, 0, 700, 0);
    if (t->c) bx_tw1(&t->c->flip, 0, 600, 0);
    if (t->kind == K_JEWEL) bx_tw1(&J.open, 0, 600, 0);
    r3_node *m = bx_n(focus);
    bx_tw3(m->pos, t->home[0], t->home[1], t->home[2], 900, 200); bx_tw3(m->scale, 1, 1, 1, 900, 200); bx_tw3(m->rot, 0, 0, 0, 900, 200);
    for (int i = 0; i < nit; i++) bx_n(IT[i].node)->vis = 1;
    bx_n(rig)->vis = 1;
    bx_cam_to(0, 0, 40, 900);
    st = S_LAID; focus = -1; caption();
}
static void set_flip(float *f, float to) { bx_sfx(SFX_PAGE, 0); bx_tw1(f, to, 800, 0); }
static book *focus_book(void) { item *t = item_of(focus); return t ? t->b : NULL; }
static void next_page(int a) { (void)a; book *b = focus_book(); if (!b) return; int c = (int)lroundf(b->flip); if (c < b->n) set_flip(&b->flip, (float)c + 1); }
static void prev_page(int a) { (void)a; book *b = focus_book(); if (!b) return; int c = (int)lroundf(b->flip); if (c > 0) set_flip(&b->flip, (float)c - 1); }

/* ---- the reader ---- */
static char keys_[200][16];
static const char *keyp[200];
static int page_keys(const char *p, int n)
{
    int k = 0;
    for (int i = 0; i < n && k < 200; i++) { snprintf(keys_[k], 16, "%s%d", p, i); keyp[k] = keys_[k]; k++; }
    return k;
}
static void read_full(int a)
{
    (void)a;
    item *t = item_of(focus);
    char ttl[96];
    if (!t) return;
    if (t->b) {
        int tech = t->b == &tB, n = page_keys(tech ? "tech" : "man", tech ? BX_CD->tech_pages : BX_CD->man_pages);
        snprintf(ttl, sizeof ttl, "%s %s", BX_CD->title, tech ? "technical reference card" : "manual");
        bx_view_open_keys(ttl, keyp, n, 1, NULL);
        return;
    }
    if (t->kind == K_JEWEL) {
        int n = 0;
        static const char *jk[] = { "jewelFront", "jewelInside", "trayBack", "disc" };
        for (int i = 0; i < 4; i++) if (bx_art_exists(jk[i])) keyp[n++] = jk[i];
        bx_view_open_keys("Jewel case", keyp, n, 0, NULL);
        return;
    }
    if (t->f) {
        const char *k[2] = { t->f == &lineF ? "lineOut" : "maskOut", t->f == &lineF ? "lineIn" : "maskIn" };
        bx_view_open_keys(t->label, k, 2, 0, NULL);
        return;
    }
    if (t->c) {
        const char *k[2]; int n = 0;
        if (t->c == &regC) { k[n++] = "regfront"; k[n++] = "regback"; }
        else if (t->c == &newsC) { k[n++] = "newsFront"; k[n++] = "newsBack"; }
        else k[n++] = "coupon";
        bx_view_open_keys(t->label, k, n, 0, NULL);
    }
}
static void open_manual(void)
{
    char ttl[96];
    init();
    int n = page_keys("man", BX_CD->man_pages);
    snprintf(ttl, sizeof ttl, "%s manual", BX_CD->title);
    bx_view_open_keys(ttl, keyp, n, 1, BX.manual_only ? bx_manual_only_closed : NULL);
}

/* ---- the scene interface ---- */
static void enter(int mode)
{
    init();
    reset_pose();
    if (mode == 99) { laid_now(); caption(); return; }
    st = S_BOX;
    caption();
}
void bx_cd_arranged(void) { enter(99); }

static void frame(float dt)
{
    if (bx_cd_install_up()) { bx_cd_install_frame(dt); return; }
    book_set(&mB); book_set(&tB); fold_set(&lineF); fold_set(&maskF); card_set(&regC); card_set(&newsC); card_set(&couponC);
    if (J.root >= 0) bx_n(J.lid)->rot[1] = -.93f * PI_F * bx_ease(clamp01(J.open));
    float k = powf(.9f, dt / 16.7f);
    int hv = bx_hovered_node();
    if (st == S_LAID) {
        for (int i = 0; i < nit; i++) {
            r3_node *m = bx_n(IT[i].node);
            m->rot[0] *= k; m->rot[1] *= k;
            if (IT[i].node == hv) m->pos[2] = .9f;
            else if (m->pos[2] > .4f) m->pos[2] = fmaxf(.4f, m->pos[2] - .05f * dt / 16.7f);
        }
    }
    if (!busy) {
        if (st == S_BOX) bx_hot_node(cbox, "The box: pick to open it");
        if (st == S_LAID) {
            for (int i = 0; i < nit; i++) bx_hot_node(IT[i].node, IT[i].kind == K_JEWEL && BX.first && !BX.from_menu ? "Jewel case: pick to install" : IT[i].label);
            bx_hot_node(cbox, "Put everything back in the box");
        }
        if (st == S_BOOK || st == S_CARD) bx_hot_node(focus, bx_n(focus)->label);
        if (st == S_JEWEL) {
            if (J.open > .9f) bx_hot_node(J.disc, BX.first && !BX.from_menu ? "The disc: pick to put it in the CD-ROM drive" : "The disc");
            bx_hot_node(J.base, J.open > .5f ? "Close the case" : "Open the case");
        }
    }
    bx_look[0] = BX_S->cam[0]; bx_look[1] = BX_S->cam[1]; bx_look[2] = 0;
}
static void back(void)
{
    if (bx_cd_install_up()) { bx_cd_install_back(); return; }
    if (busy) return;
    if (st == S_BOOK || st == S_JEWEL || st == S_CARD) unfocus();
    else if (st == S_LAID) to_closed();
    else if (st == S_BOX && !BX.first) bx_done();
}
static void back_btn(int a) { (void)a; back(); }
static void draw2d(void)
{
    if (bx_cd_install_up()) { bx_cd_install_draw2d(); return; }
    float u = BX_C.u, top = BX_C.sy + 1.4f * u;
    if (!(st == S_BOX && BX.first)) bx_button(BX_C.sx + 2 * u, top, st == S_BOX ? "< Back to the game" : "< Back", back_btn, 0, -1);
    bx_text(BX_C.sx + BX_C.sw - 2 * u, top + 2.0f * u, 1.5f * u, 0xb5a58f, 255, BX.title, 0, 1);
    if (st == S_BOOK || st == S_JEWEL || st == S_CARD) {
        float y = BX_C.sy + BX_C.sh - 4.6f * u - 3 * u, cx = BX_C.sx + BX_C.sw / 2;
        if (st == S_BOOK) { bx_button(cx - 1.2f * u - 7 * u, y, "< Turn back", prev_page, 0, 1); bx_button(cx - 7 * u, y, "Turn page >", next_page, 0, -1); }
        bx_button(cx + 7.5f * u, y, "Read full size", read_full, 0, -1);
    }
    if (BX.cap[0]) bx_rich(BX_C.sx + BX_C.sw / 2, BX_C.sy + BX_C.sh - 1.6f * u, 1.6f * u, BX.cap, 0xb5a58f, 0xf4ecdf);
}
static void install_cb(int a) { (void)a; bx_cd_install_start(); }
static void disc_pick(void)
{
    if (BX.first && !BX.from_menu) { bx_fade(install_cb, 0); return; }
    r3_tex *p = bx_art("disc");
    if (p) bx_view_open(BX_CD->title, &p, 1, 0, NULL);
}
static void activate(int node, float sx, float sy)
{
    (void)sy;
    if (bx_cd_install_up()) { bx_cd_install_activate(node); return; }
    if (busy || node < 0) return;
    if (st == S_BOX) { to_open(); return; }
    if (st == S_LAID) { if (node == cbox) to_closed(); else if (item_of(node)) focus_on(node); return; }
    if (st == S_JEWEL) {
        if (node == J.disc && J.open > .9f) { disc_pick(); return; }
        bx_sfx(SFX_CLICK, 0);
        bx_tw1(&J.open, J.open > .5f ? 0 : 1, 700, 0);
        bx_after(720, (void (*)(int))caption, 0);
        return;
    }
    item *t = item_of(focus);
    if (!t || node != focus) return;
    if (t->b) {
        float x, y, p[3]; r3_world_pos(BX_S, t->b->root, p);
        if (r3_project(BX_S, &(r3_target){ .w = BX_C.w, .h = BX_C.h }, p, &x, &y) && sx < x) prev_page(0); else next_page(0);
        return;
    }
    if (t->c) { set_flip(&t->c->flip, t->c->flip > .5f ? 0 : 1); return; }
    if (t->f) {
        int c = (int)lroundf(t->f->flip), to = c == 0 ? 1 : c == 1 ? 2 : 0;
        bx_sfx(SFX_PAGE, 0);
        bx_tw1(&t->f->flip, (float)to, to == 0 ? 1400 : 900, 0);
        float wide = t->f->n * t->f->pw * 1.5f;
        bx_tw3(bx_n(focus)->scale, to ? fminf(1.5f, 30.0f / wide) : 1.5f, to ? fminf(1.5f, 30.0f / wide) : 1.5f, 1, 900, 0);
        bx_n(focus)->scale[2] = 1;
    }
}
static void drag(int node, float dx, float dy)
{
    int t = -1;
    if (st == S_BOX) t = rig;
    else if (st == S_LAID && node >= 0 && node != cbox) t = node;
    else if (st == S_JEWEL) t = focus;
    if (t < 0 || busy || bx_cd_install_up()) return;
    r3_node *n = bx_n(t);
    n->rot[1] += dx * .012f;
    n->rot[0] += dy * .012f;
    if (n->rot[0] > 1.3f) n->rot[0] = 1.3f;
    if (n->rot[0] < -1.3f) n->rot[0] = -1.3f;
}
static void skip(void) { bx_cd_install_skip(); }

static bx_scene SC;
const bx_scene *bx_cd_scene(const bx_cd_game *g)
{
    BX_CD = g;
    bx_scene s = { g->dir, enter, frame, draw2d, activate, back, drag, open_manual, NULL, NULL, NULL, skip };
    SC = s;
    return &SC;
}
