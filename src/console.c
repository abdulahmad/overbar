/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A console's online features (overbar/console.h). Messages: 'S' number u32, the state's bytes (the
 * host to every machine); 'A' number u32 (a machine to the host: it has that state). */
#include "overbar/console.h"

#include <stdlib.h>
#include <string.h>

#define GAP (2 * OB_NET_MAX_ROLLBACK + OB_NET_MAX_DELAY + 8)   /* steps between two loads, at least: a machine that is
                                                                  behind has played the last one's step before the
                                                                  one after next replaces its state */

static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

void ob_console_init(ob_console *c) { memset(c, 0, sizeof *c); }

void ob_console_free(ob_console *c)
{
    free(c->state[0]);
    free(c->state[1]);
    memset(c, 0, sizeof *c);
}

void ob_console_reset(ob_console *c) { c->reset = 1; }

static int keep(ob_console *c, uint32_t id, const void *b, size_t size)
{
    uint8_t *k = (uint8_t *)malloc(size ? size : 1);
    if (!k) return -1;
    memcpy(k, b, size);
    free(c->state[id & 1]);
    c->state[id & 1] = k;
    c->size[id & 1] = size;
    return 0;
}

/* (the host) the state on its way to the machines of the room it has not gone to (one that came since, too) */
static void send_to_new(ob_console *c, ob_net *n)
{
    unsigned to = ob_net_members(n) & ~c->sent;
    size_t size = c->size[c->id & 1];
    uint8_t *m;
    int k;
    if (!to || !(m = (uint8_t *)malloc(size + 5))) return;
    m[0] = 'S';
    put32(m + 1, c->id);
    memcpy(m + 5, c->state[c->id & 1], size);
    for (k = 1; k < OB_NET_MAX_MACHINES; k++)
        if (to >> k & 1 && !ob_net_send(n, k, OB_NET_TYPE_CONSOLE, m, size + 5)) c->sent |= 1u << k;
    free(m);
}

int ob_console_load(ob_console *c, ob_net *n, const void *state, size_t size)
{
    if (!ob_net_is_host(n) || ob_net_state(n) != OB_NET_GO || !state || !size || size + 5 > OB_NET_MAX_MESSAGE || c->sending) return -1;
    if (keep(c, c->id + 1, state, size)) return -1;
    c->id++;
    c->acked = c->sent = 1u;
    c->sending = 1;
    send_to_new(c, n);
    return 0;
}

int ob_console_busy(const ob_console *c) { return c->sending; }

void ob_console_local(ob_console *c, ob_net *n, uint16_t *local, int count)
{
    int k;
    for (k = 0; k < count; k++) local[k] &= (uint16_t)~OB_CONSOLE_BITS;
    if (!ob_net_is_host(n) || count < 1) { c->reset = 0; return; }
    if (c->reset) { local[0] |= OB_CONSOLE_RESET; c->reset = 0; }
    if (c->sending) send_to_new(c, n);
    if (c->sending && (ob_net_members(n) & ~c->acked) == 0 &&
        (!c->bound_at || ob_net_confirmed(n) >= c->bound_at + GAP)) {   /* (every machine has it) */
        local[0] |= (uint16_t)(OB_CONSOLE_LOAD | (c->id & 1 ? OB_CONSOLE_ODD : 0));
        c->bound_at = ob_net_step_count(n) + (uint32_t)ob_net_delay(n) + 1;
        c->sending = 0;
    }
}

int ob_console_step(ob_console *c, ob_net *n, uint16_t *all, int pads, int menus, const void **state, size_t *size)
{
    unsigned bits = 0, host = ob_net_pads_of(n, 0);
    int p;
    for (p = 0; p < pads && p < OB_NET_MAX_PADS; p++) {
        bits |= all[p] & OB_CONSOLE_BITS;
        all[p] &= (uint16_t)~OB_CONSOLE_BITS;
        if (menus && !(host >> p & 1)) all[p] = 0;
    }
    if (bits & OB_CONSOLE_LOAD) {
        int w = bits & OB_CONSOLE_ODD ? 1 : 0;
        if (!c->state[w]) return OB_CONSOLE_NONE;       /* (cannot be: the host waits for every machine to have it) */
        *state = c->state[w];
        *size = c->size[w];
        c->loads++;
        return OB_CONSOLE_DO_LOAD;
    }
    if (bits & OB_CONSOLE_RESET) { c->resets++; return OB_CONSOLE_DO_RESET; }
    return OB_CONSOLE_NONE;
}

int ob_console_message(ob_console *c, ob_net *n, int from, const void *data, size_t size)
{
    const uint8_t *b = (const uint8_t *)data;
    uint8_t ack[5];
    if (size < 5) return 0;
    if (b[0] == 'S' && from == 0 && !ob_net_is_host(n)) {
        uint32_t id = get32(b + 1);
        if (keep(c, id, b + 5, size - 5)) return 1;
        ack[0] = 'A';
        put32(ack + 1, id);
        ob_net_send(n, 0, OB_NET_TYPE_CONSOLE, ack, sizeof ack);
        return 1;
    }
    if (b[0] == 'A' && ob_net_is_host(n)) {
        if (get32(b + 1) == c->id && from > 0 && from < OB_NET_MAX_MACHINES) c->acked |= 1u << from;
        return 1;
    }
    return 0;
}
