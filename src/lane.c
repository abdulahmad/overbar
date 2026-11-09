/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The menu lane (overbar/lane.h). A step's two words:
 *   0: key u8, down (bit 8), the key's flip (9), the pause request's flip (10), mouse buttons (11, 12),
 *      the mouse's x bits 7 .. 9 (13 .. 15)
 *   1: x bits 0 .. 6, y (7 .. 15)
 * so x is 0 .. 1023 and y 0 .. 511. */
#include "overbar/lane.h"
#include <string.h>

void ob_lane_owners(int8_t *owner, unsigned machines)
{
    int k;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) {
        int p = OB_LANE_FIRST + OB_LANE_PADS * k;
        if (p + 1 >= OB_NET_MAX_PADS) break;
        owner[p] = owner[p + 1] = (int8_t)((machines >> k & 1) ? k : -1);
    }
}

void ob_lane_in_init(ob_lane_in *l) { memset(l, 0, sizeof *l); }

void ob_lane_in_key(ob_lane_in *l, int sc, int down)
{
    if (sc <= 0 || sc > 255 || l->n >= OB_LANE_KEYS) return;
    l->key[(l->head + l->n++) % OB_LANE_KEYS] = (uint16_t)(sc | (down ? 0x100 : 0));
}

static int clamp(int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; }

void ob_lane_in_mouse(ob_lane_in *l, int x, int y, int buttons)
{
    l->x = clamp(x, 1023);
    l->y = clamp(y, 511);
    l->buttons = buttons & 3;
}

void ob_lane_in_pause(ob_lane_in *l) { l->pause_wanted = 1; }

void ob_lane_in_pack(ob_lane_in *l, uint32_t step, uint16_t out[OB_LANE_PADS])
{
    int key = 0, down = 0;
    if (l->step && step + 1 <= l->step) { out[0] = l->last[0]; out[1] = l->last[1]; return; }   /* packed already */
    if (l->n) {
        uint16_t k = l->key[l->head];
        l->head = (l->head + 1) % OB_LANE_KEYS;
        l->n--;
        key = k & 0xFF;
        down = k >> 8 & 1;
        l->key_flip ^= 1;
    }
    if (l->pause_wanted) { l->pause_wanted = 0; l->pause_flip ^= 1; }
    out[0] = (uint16_t)(key | down << 8 | l->key_flip << 9 | l->pause_flip << 10 | l->buttons << 11 | (l->x >> 7 & 7) << 13);
    out[1] = (uint16_t)((l->x & 0x7F) | (l->y & 0x1FF) << 7);
    l->last[0] = out[0];
    l->last[1] = out[1];
    l->step = step + 1;
}

void ob_lane_unpack(const uint16_t w[OB_LANE_PADS], ob_lane_word *o)
{
    o->key = w[0] & 0xFF;
    o->down = w[0] >> 8 & 1;
    o->key_flip = w[0] >> 9 & 1;
    o->pause_flip = w[0] >> 10 & 1;
    o->buttons = w[0] >> 11 & 3;
    o->x = (w[0] >> 13 & 7) << 7 | (w[1] & 0x7F);
    o->y = w[1] >> 7 & 0x1FF;
}

void ob_lane_sim_init(ob_lane_sim *s)
{
    memset(s, 0, sizeof *s);
    s->holder = -1;
}

int ob_lane_step(ob_lane_sim *s, const uint16_t *all, unsigned machines, uint32_t step)
{
    int k, flags = 0;
    s->has_key = 0;
    for (k = 0; k < OB_NET_MAX_MACHINES; k++) {
        int p = OB_LANE_FIRST + OB_LANE_PADS * k, key_edge, pause_edge;
        ob_lane_word w;
        if (!(machines >> k & 1) || p + 1 >= OB_NET_MAX_PADS) continue;
        ob_lane_unpack(all + p, &w);
        key_edge = w.key_flip != (s->seen[k] & 1);
        pause_edge = w.pause_flip != (s->seen[k] >> 1 & 1);
        s->seen[k] = (uint8_t)(w.key_flip | w.pause_flip << 1);
        if (pause_edge) {
            if (s->holder < 0) { s->holder = (int8_t)k; s->paused_at = step + 1; flags |= OB_LANE_PAUSED; flags &= ~OB_LANE_RESUMED; }
            else if (s->holder == k) { s->holder = -1; flags |= OB_LANE_RESUMED; }
        }
        if (s->holder == k) {
            s->x = (int16_t)w.x;
            s->y = (int16_t)w.y;
            s->buttons = (uint8_t)w.buttons;
            if (key_edge && w.key) { s->key = (uint8_t)w.key; s->down = (uint8_t)w.down; s->has_key = 1; flags |= OB_LANE_KEY; }
        }
    }
    return flags;
}

void ob_lane_resume(ob_lane_sim *s) { s->holder = -1; }

int ob_lane_holder_may(const ob_lane_sim *s, unsigned holder_teams, int team)
{
    return s->holder >= 0 && team >= 0 && team < 32 && (holder_teams >> team & 1);
}
