/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The CD box's install (overbar/box_cd.h): the jewel case open on the desk by the 486; the disc goes into the CD-ROM's
 * tray, the tray closes, and the game's own installer runs on the VGA monitor as it looked, screen by screen. The steps are
 * the port's (bx_cd_game.install): lines typed at the DOS prompt, then the installer's screens, which are pictures in the
 * player's art folder like the scans (box-art/<dir>/<key>.png). The text mode's 8 x 16 font is vgafont.png there too. */
#include "overbar/box_cd.h"
#include "overbar/vfont.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the 80 x 25 text screen (the DOS prompt) ---- */
static const uint32_t PAL[16] = { 0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
                                  0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff };
static char dos[25][81]; static int dos_n, cur_on = 1, dirty = 1;
static const char *pic;                       /* an installer screen shown instead of the text, or NULL */
static void dos_line(const char *s) { if (dos_n == 25) { memmove(dos, dos + 1, sizeof dos[0] * 24); dos_n = 24; } snprintf(dos[dos_n++], 81, "%s", s); dirty = 1; }
static void dos_add(const char *s) { if (!dos_n) dos_line(""); size_t L = strlen(dos[dos_n - 1]); snprintf(dos[dos_n - 1] + L, 81 - L, "%s", s); dirty = 1; }
static void dos_clear(void) { dos_n = 0; dirty = 1; }

#define SW 1280
#define SH 800
static int scr_tex = -1, scr_node, blink_on = -1;
static void scr_render(void)
{
    int bl = ((int)(bx_now() * 3.75)) & 1;
    if (!dirty && (pic || bl == blink_on)) return;
    dirty = 0; blink_on = bl;
    uint32_t *px = (uint32_t *)malloc((size_t)SW * SH * 4);
    if (!px) return;
    const r3_tex *p = pic ? bx_art(pic) : NULL;
    if (p) {                                    /* the installer's screen, stretched to the tube like the VGA did */
        for (int y = 0; y < SH; y++) {
            const uint32_t *row = p->lv[0] + (size_t)(y * p->lh[0] / SH) * p->lw[0];
            for (int x = 0; x < SW; x++) px[(size_t)y * SW + x] = row[x * p->lw[0] / SW] | 0xff000000u;
        }
    } else {
        const r3_tex *f = bx_art("vgafont");
        int font = f && f->lw[0] == 128 && f->lh[0] == 256;
        int cy = dos_n ? dos_n - 1 : 0, cx = dos_n ? (int)strlen(dos[dos_n - 1]) : 0;
        for (int i = 0; i < SW * SH; i++) px[i] = 0xff000000u;
        for (int y = 0; y < 25; y++)
            for (int x = 0; x < 80; x++) {
                int ch = y < dos_n ? (unsigned char)dos[y][x] : 0;
                if (y < dos_n && x >= (int)strlen(dos[y])) ch = 0;
                int cur = cur_on && bl && x == cx && y == cy;
                if (!ch && !cur) continue;
                for (int j = 0; j < 16; j++)
                    for (int i = 0; i < 8; i++) {
                        int on = font && ch ? (f->lv[0][((ch >> 4) * 16 + j) * 128 + (ch & 15) * 8 + i] >> 24) > 128 : 0;
                        if (cur && j >= 14) on = 1;
                        if (!on) continue;
                        uint32_t *r0 = px + (size_t)(y * 32 + j * 2) * SW + x * 16 + i * 2;
                        r0[0] = r0[1] = r0[SW] = r0[SW + 1] = 0xff000000u | PAL[7];
                    }
                if (!font && ch > 32 && ch < 127) { char s[2] = { (char)ch, 0 }; ob_vf_draw(px, SW, SH, SW, x * 16 + 2.0f, y * 32 + 25.0f, OB_VF_MEDIUM, 26, PAL[7], 255, s, 0, 0); }
            }
        for (int i = 0; i < SW * SH; i++) px[i] |= 0xff000000u;
    }
    r3_tex_set(&BX_S->texs[scr_tex], px, SW, SH);
}

/* ---- the desk ---- */
static int ready, desk, tray, door, jewel, disc, jlid, led_cd, led_hdd, mat_cd = -1, mat_hdd = -1;
static float disc_home[9], tray_in, jewel_saved[10];
static int jewel_par;
static float lin(int c) { float v = c / 255.0f; return v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f); }
static int own_mat(int node)
{
    int r = -1;
    for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == node) {
        if (r < 0) { r3_mat m = BX_S->mats[BX_S->meshes[i].mat]; r = r3_mat_add(BX_S, &m); }
        BX_S->meshes[i].mat_override = r;
    }
    return r;
}
static void mat_col(int m, uint32_t c) { if (m < 0) return; BX_S->mats[m].col[0] = lin(c >> 16); BX_S->mats[m].col[1] = lin((c >> 8) & 255); BX_S->mats[m].col[2] = lin(c & 255); }
static void init(void)
{
    if (ready) return;
    desk = bx_node("pcDesk"); scr_node = bx_node("pc.screen"); tray = bx_node("pc.cdTray"); door = bx_node("pc.cdDoor");
    led_cd = bx_node("pc.ledCd"); led_hdd = bx_node("pc.ledHdd");
    jewel = bx_node("item.jewel"); disc = bx_node("jewel.disc"); jlid = bx_node("jewel.lid");
    r3_node *d = bx_n(disc);
    memcpy(disc_home, d->pos, 12); memcpy(disc_home + 3, d->rot, 12); memcpy(disc_home + 6, d->scale, 12);
    tray_in = bx_n(tray)->pos[2];
    uint32_t *px = (uint32_t *)calloc((size_t)SW * SH, 4);
    scr_tex = r3_tex_add(BX_S, px, SW, SH);
    for (int i = 0; i < BX_S->nmeshes; i++) if (BX_S->meshes[i].node == scr_node) {
        r3_mat m = BX_S->mats[BX_S->meshes[i].mat]; m.map = scr_tex; m.type = 'B'; m.col[0] = m.col[1] = m.col[2] = 1;
        BX_S->meshes[i].mat_override = r3_mat_add(BX_S, &m);
    }
    mat_cd = own_mat(led_cd); mat_hdd = own_mat(led_hdd);
    ready = 1;
}
/* the box's own things are put away while the desk is up (the jewel case comes along onto the desk) */
static signed char was_vis[1024];
static void show_desk(int on)
{
    for (int i = 1; i < BX_S->nnodes && i < 1024; i++) {
        r3_node *n = &BX_S->nodes[i];
        if (n->parent > 0) continue;
        if (i == desk) { n->vis = on; continue; }
        if (on) { was_vis[i] = (signed char)n->vis; n->vis = 0; }
        else if (was_vis[i] >= 0) n->vis = was_vis[i];
    }
    r3_node *j = bx_n(jewel);
    if (on) {
        jewel_par = j->parent;
        memcpy(jewel_saved, j->pos, 12); memcpy(jewel_saved + 3, j->rot, 12); memcpy(jewel_saved + 6, j->scale, 12); jewel_saved[9] = (float)j->vis;
        bx_parent(jewel, desk);
        bx_set3(j->pos, 9.6f, .22f, 11.6f); bx_set3(j->rot, -PI_F / 2, 0, -.12f); bx_set3(j->scale, 1, 1, 1); j->vis = 1;
        bx_n(jlid)->rot[1] = -.93f * PI_F;
    } else {
        bx_parent(jewel, jewel_par);
        memcpy(j->pos, jewel_saved, 12); memcpy(j->rot, jewel_saved + 3, 12); memcpy(j->scale, jewel_saved + 6, 12); j->vis = (int)jewel_saved[9];
    }
}
static void disc_home_pose(void)
{
    bx_parent(disc, bx_node("jewel"));
    r3_node *d = bx_n(disc);
    memcpy(d->pos, disc_home, 12); memcpy(d->rot, disc_home + 3, 12); memcpy(d->scale, disc_home + 6, 12);
}

/* ---- the run ---- */
static struct {
    int up, run, busy, need, step, cd, hd;   /* cd/hd: the lights, 1 on, 2 flickering */
    const char *typing; int tpos, tnext;
    char cap[160];
} I;
int bx_cd_install_up(void) { return I.up; }
static void say(const char *s) { snprintf(I.cap, sizeof I.cap, "%s", s); }
static void step(int arg);
static void later(float ms, int pc) { bx_after(ms, step, (I.run << 8) | pc); }

static float look[3];
static void look_to(float x, float y, float z, float ms) { bx_tw3(look, x, y, z, ms, 0); }
static void view_tray(float ms)
{
    float p[3]; r3_world_pos(BX_S, bx_node("pc.tower"), p);
    bx_cam_to(p[0] - 4.5f, 19.5f, p[2] + 19, ms); look_to(p[0] - .5f, 12.2f, p[2] + 2.5f, ms);
}
static void view_screen(float ms) { bx_cam_to(-3, 11.05f, 30, ms); look_to(-3, 8.05f, -1, ms); }

/* the tray out (the door swings in), the tray back */
static void tray_out(void) { bx_tw1(&bx_n(door)->rot[0], 1.45f, 250, 0); bx_tw1(&bx_n(tray)->pos[2], tray_in + 5.85f, 1100, 150); }
static void tray_close(void) { bx_tw1(&bx_n(tray)->pos[2], tray_in, 1100, 0); bx_tw1(&bx_n(door)->rot[0], 0, 250, 1000); }
/* the disc from the case onto the tray: lifted, carried over, set down on the spindle */
static void disc_over(void)
{
    float seat[3];
    bx_attach(disc, desk);
    r3_world_pos(BX_S, tray, seat);
    seat[1] += .2f; seat[2] += -.15f;
    r3_node *d = bx_n(disc);
    float sx = d->pos[0], sz = d->pos[2];
    bx_tw3(d->pos, sx, d->pos[1] + 2.2f, sz, 450, 0);
    bx_tw3(d->pos, (sx + seat[0]) / 2, seat[1] + 3, (sz + seat[2]) / 2, 700, 450);
    bx_tw3(d->pos, seat[0], seat[1] + 1.2f, seat[2], 600, 1150);
    bx_tw3(d->pos, seat[0], seat[1], seat[2], 350, 1750);
    bx_tw3(d->rot, -PI_F / 2, 0, 0, 900, 300);
}

static void step(int arg)
{
    if ((arg >> 8) != I.run || !I.up) return;
    int pc = arg & 255;
    const bx_cd_game *G = BX_CD;
    switch (pc) {
    case 0: bx_sfx(SFX_BEEP, 0); I.hd = 2; dos_clear(); dos_line("Starting MS-DOS..."); later(1300, 1); break;
    case 1: I.hd = 0; dos_line(""); dos_line("C:\\>"); say("<b>Put the CD in the CD-ROM drive.</b> Pick the disc."); I.need = 1; I.busy = 0; break;
    /* the disc picked: the eject button, the tray, the disc onto it, the tray closes */
    case 10: view_tray(900); later(700, 11); break;
    case 11: bx_sfx(SFX_CLICK, 0); later(150, 12); break;
    case 12: bx_sfx(SFX_MOTOR, 0); tray_out(); later(1300, 13); break;
    case 13: disc_over(); bx_sfx(SFX_SLIDE, 0); later(2200, 14); break;
    case 14: bx_attach(disc, tray); bx_sfx(SFX_CLICK, 0); later(250, 15); break;
    case 15: bx_sfx(SFX_MOTOR, 0); tray_close(); later(1300, 16); break;
    case 16: I.cd = 2; bx_sfx(SFX_DISK_IN, 0); bx_sfx(SFX_SEEK, 4); say("<b>The CD is in.</b>"); later(900, 17); break;
    case 17: I.cd = 0; view_screen(1000); later(1100, 20); I.step = 0; break;
    /* the port's steps */
    case 20: {
        if (I.step >= G->ninstall) { later(200, 40); break; }
        const bx_cd_step *s = &G->install[I.step++];
        if (s->key) bx_sfx(SFX_KEY, 0);
        if (s->kind == CD_TEXT) {
            pic = NULL; cur_on = 1; dirty = 1;
            if (s->type) { I.typing = s->s; I.tpos = 0; I.tnext = 21; later(140, 30); }
            else { dos_line(s->s); later((float)(s->ms ? s->ms : 300), 20); }
        } else if (s->kind == CD_CLS) { pic = NULL; dos_clear(); later((float)(s->ms ? s->ms : 200), 20); }
        else {
            if (!bx_art_exists(s->s)) { later(1, 20); break; }   /* that screen is not in the art folder */
            pic = s->s; dirty = 1;
            if (s->kind == CD_COPY) {
                I.cd = 2; I.hd = 2;
                for (float t = 300; t < s->ms; t += 520) bx_after(t, step, (I.run << 8) | 22);
            } else { I.cd = 0; I.hd = 0; }
            later((float)(s->ms ? s->ms : 1500), 20);
        }
        break;
    }
    case 21: dos_line(""); I.cd = 2; bx_sfx(SFX_SEEK, 3); later(500, 23); break;
    case 23: I.cd = 0; later(1, 20); break;
    case 22: bx_sfx(SFX_STEP, 0); if (((int)(bx_now() * 10)) % 3 == 0) bx_sfx(SFX_HDD, 0); break;
    case 30:
        if (!I.typing[I.tpos]) { later(1, I.tnext); break; }
        { char s[2] = { I.typing[I.tpos], 0 }; dos_add(s); }
        I.tpos++; bx_sfx(SFX_KEY, 0); later(130, 30);
        break;
    /* installed */
    case 40: I.cd = I.hd = 0; say("<b>Installed.</b> Starting the game..."); later(1600, 41); break;
    case 41: bx_done(); break;
    }
}

void bx_cd_install_start(void)
{
    init();
    disc_home_pose();
    bx_n(tray)->pos[2] = tray_in; bx_n(door)->rot[0] = 0;
    I.run++; I.busy = 1; I.need = 0; I.cd = I.hd = 0;
    show_desk(1);
    pic = NULL; dos_clear(); cur_on = 1;
    look[0] = 5; look[1] = 5.2f; look[2] = 2;
    bx_cam(5, 23, 50);
    say("<b>Starting the PC...</b>");
    I.up = 1;
    snprintf(BX.title, sizeof BX.title, "Install %s", BX_CD->title);
    later(900, 0);
}
void bx_cd_install_skip(void) { I.run++; }
static void back_to_box(int a)
{
    (void)a;
    I.run++; I.up = 0;
    bx_time_clear();
    disc_home_pose();
    bx_n(tray)->pos[2] = tray_in; bx_n(door)->rot[0] = 0;
    show_desk(0);
    bx_cd_arranged();
}
void bx_cd_install_back(void) { bx_fade(back_to_box, 0); }
static void back_btn(int a) { (void)a; bx_cd_install_back(); }
void bx_cd_install_activate(int node)
{
    if (node == disc && I.need && !I.busy) { I.need = 0; I.busy = 1; say("<b>The CD</b> goes into the CD-ROM drive."); later(1, 10); }
}
void bx_cd_install_frame(float dt)
{
    (void)dt;
    mat_col(mat_cd, I.cd == 1 || (I.cd == 2 && (((int)(bx_now() * 9)) & 1)) ? 0x44ff66 : 0x1a2a1a);
    int hd = I.hd == 1 || (I.hd == 2 && (((int)(bx_now() * 13)) % 3 != 0));
    mat_col(mat_hdd, hd ? 0xffaa33 : 0x3a2200);
    scr_render();
    if (I.need && !I.busy) {
        bx_hot_node(disc, "Put the CD in the drive");
        r3_node *d = bx_n(disc);                      /* the disc lifts a little in its case: pick me */
        d->pos[2] = disc_home[2] + .25f * fabsf(sinf((float)bx_now() * 2.85f));
    }
    bx_look[0] = look[0]; bx_look[1] = look[1]; bx_look[2] = look[2];
}
void bx_cd_install_draw2d(void)
{
    float u = BX_C.u, sx = BX_C.sx, sy = BX_C.sy, sw = BX_C.sw, sh = BX_C.sh;
    bx_button(sx + 2 * u, sy + 1.4f * u, "< Back to the box", back_btn, 0, -1);
    bx_text(sx + sw - 2 * u, sy + 3.4f * u, 1.5f * u, 0xb5a58f, 255, BX.title, 0, 1);
    if (I.cap[0]) bx_rich(sx + sw / 2, sy + sh - 1.4f * u, 1.6f * u, I.cap, 0xb5a58f, 0xf4ecdf);
}
