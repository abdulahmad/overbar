/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Lockstep netplay (overbar/net.h), adapted from PulseMan's rollback netplay (src/net/netplay.c):
 * its handshake, identity check, names, settings in acknowledged chunks, star of machines with the
 * host passing buttons on, "both connect", desync check and goodbye; and, for a game that gives
 * states, its rollback: other machines' buttons guessed (held), the steps since a wrong guess
 * played again from a state, and each machine kept in time with the others. Without states a step
 * runs once every pad's buttons for it are known (lockstep).
 *
 * The messages, all little-endian after "OB", the protocol's version (OB_NET_PROTOCOL) and a type.
 * HELLO's role and nonce and REFUSE's nonce and why stay where they are in every version, so a
 * machine of another version is told why it is refused.
 *   HELLO      role u8, nonce u64, identity (length u8, bytes), name (length u8, bytes), here u8, pads u8
 *   ACCEPT     the guest's nonce u64, session u32, delay u8, settings' size u32, the host's name
 *   REFUSE     the guest's nonce u64, why (length u8, bytes)
 *   CHUNK      session u32, index u32, length u16, bytes (the settings, CHUNK bytes at a time)
 *   CHUNK_ACK  session u32, chunks had in a row u32, which of the 64 after the first missing it has u64
 *   READY      session u32 (the guest has the settings)
 *   START      session u32, the receiver's place u8, machines u8, then each one's pads (bit p: pad p) u16 and name
 *   INPUT      session u32, whose u8 (the machine whose buttons these are), first step u32, count u8,
 *              pads u8, the buttons (count x pads u16); then machines u8 and the steps of each one's
 *              buttons the sender has in a row (u32 each: its acknowledgement); then the sender's
 *              newest desync check: step + 1 u32 (0: none) and hash u64; then the sender's steps run
 *              u32 and its lead on the receiver (steps, s16: its steps run less the receiver's last heard)
 *   PING, PONG session u32, time u64 (PONG echoes PING's)
 *   BYE        session u32, why (length u8, bytes)
 *   ROOM       session u32, players in so far u8, players in all u8 (the host's waiting room)
 *   LANE       session u32, seq u32, length u16, bytes: one fragment of the messages between two
 *              machines that talk (in order, resent until acknowledged)
 *   LANE_ACK   session u32, fragments had in a row u32, which of the 64 after the first missing it has u64
 * A fragment: from u8 (the machine that sent the message), to u8 (255: all), type u8, flags u8 (1: the
 * message's first, 2: its last), the first's the message's size u32, then bytes. The host passes a
 * guest's on to the machines it is for. The core's own messages (types 250 ..):
 *   MEMBERS    machines u8, then each: number u8, players u8, ping ms u16, name
 *   MATCH      match u32, delay u8, pads u8, each pad's machine u8 (255: none), state's step u32, flags u8
 *              (1: every machine starts from the state; else the receiver joins a match that goes on),
 *              settings' size u32, settings, then the state (shared: ob_net_states.share; none: no bytes)
 *   END        match u32, at step u32 (0: now), why
 * In a room an INPUT's session is followed by its match u32. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* the sockets: PulseMan's, adapted, in this translation unit (a port compiles the files in src and nothing more) */
#include "../third_party/pulseman/udp.c"
#include "overbar/net.h"

#define RING 256                        /* steps of buttons kept: more than 3 x OB_NET_MAX_DELAY */
#define MAX_SEND 64                     /* steps of one machine's buttons in one INPUT */
#define CHUNK 1024
#define WINDOW 64                       /* settings chunks in flight past the guest's acknowledgement */
#define CHECKS 16
#define MACH OB_NET_MAX_MACHINES
#define PADS OB_NET_MAX_PADS
#define MS 1000u
#define GONE_US ((uint64_t)OB_NET_TIMEOUT_MS * MS)
#define RESEND_US (20u * MS)            /* buttons not acknowledged go again this often (and as a heartbeat) */
#define HELLO_US (100u * MS)
#define PING_US (250u * MS)
#define NONE 0xFFFFFFFFu
#define GIVE_EVERY 8                    /* steps at least between two steps given back */
#define LANE_WIN 64                     /* fragments in flight past the other's acknowledgement */
#define FRAG 1000                       /* bytes of a fragment */
#define TO_ALL 255
#define MEMBERS_US (2000u * MS)         /* (a room's host) who is in, again this often */

enum { T_HELLO = 1, T_ACCEPT, T_REFUSE, T_CHUNK, T_CHUNK_ACK, T_READY, T_START, T_INPUT, T_PING, T_PONG, T_BYE, T_ROOM,
       T_LANE, T_LANE_ACK };
enum { P_HELLO, P_HOSTING, P_GUEST, P_RUNNING, P_DONE, P_ROOM };
enum { M_MEMBERS = 250, M_MATCH, M_END };

/* the messages between two machines that talk: fragments base .. base + qn - 1 (the oldest not
 * acknowledged first) to send, and those come in */
typedef struct frag { uint32_t seq; uint16_t len; uint8_t acked, sent; uint64_t at; uint8_t b[FRAG]; } frag;
typedef struct lane {
    frag **q;
    uint32_t qn, qcap, base, next;
    uint32_t in_have;                   /* fragments had in a row */
    uint8_t *in_b[LANE_WIN];            /* those past it, by seq % LANE_WIN */
    uint32_t in_seq[LANE_WIN];
    uint16_t in_len[LANE_WIN];
} lane;
typedef struct msg { struct msg *next; int from, type; size_t size; uint8_t *b; } msg;
typedef struct asmb { uint8_t *b; size_t size, have; int type, on; } asmb;   /* a message coming, from one machine */
enum { ROLE_HOST, ROLE_JOIN, ROLE_BOTH };            /* ob_online.role */

typedef struct check { uint32_t step; uint64_t hash; int used; } check;

/* a machine of the session: this one, the host, a guest */
typedef struct peer {
    int used;
    int slot;                           /* its address's slot in the socket (-1: one this machine does not talk to) */
    uint64_t nonce;
    char name[OB_NET_NAME + 1];
    int here;
    unsigned pads;
    uint32_t has[MACH];                 /* (one this machine talks to) the steps of each machine's buttons it has in a row */
    uint64_t heard_at, ping_at;
    int rtt_us;
    uint32_t set_have, set_sent;        /* (a host's guest) the settings: chunks it has in a row, sent up to, */
    uint64_t set_mask, set_at;          /* which of the window's after the first missing it has, when they last went */
    int ready;
    uint32_t their_cur;                 /* (one this machine talks to) the steps it had run, newest heard */
    int their_lead, heard_cur;          /* its lead on this machine as it said; whether either was heard */
    check theirs[CHECKS];               /* (a host's guest) its desync checks */
    int ntheirs;
    int inm;                            /* (a room) in the match that goes on */
    int lin;                            /* (a room's host) was in the match that just ended: still sent its buttons */
    int ping_ms;                        /* (a room) its round trip to the host, as the host said (-1: not known) */
    lane ln;                            /* (one this machine talks to) the messages */
} peer;

struct ob_net {
    ob_udp *u;
    int role, phase;
    char why[200], note[200], addr[100];
    int host, me, npeers, pads;
    peer p[MACH];
    int8_t owner[PADS];                 /* the machine each pad is played at (-1: none) */
    uint32_t known[MACH];               /* steps of each machine's buttons known here in a row */
    int here, total, delay;
    char identity[256], name[OB_NET_NAME + 1];
    uint64_t nonce, rng;
    uint32_t session;
    uint8_t *settings, *got;            /* the settings (a guest's: coming in, chunk by chunk) */
    size_t settings_size;
    uint32_t chunks, got_have;
    uint64_t hello_at, ready_at, room_at, sent_at;
    int started, room_in, room_total, dirty;
    uint32_t cur, waited_steps;         /* steps run; of them, those that waited */
    int waited;                         /* this step has waited */
    uint16_t btn[RING][PADS];
    uint16_t ran[RING][PADS];           /* the buttons each step was run with (guesses included) */
    /* rollback (states given) */
    ob_net_states st;
    int rb, max_rb, nslots;
    struct slot { uint32_t step; size_t size, cap; uint8_t *b; int valid; } slot[OB_NET_MAX_ROLLBACK + 2];
    uint32_t rb_to;                     /* the first step run with a wrong guess (NONE: none) */
    uint32_t replay_end;                /* steps before this one, run again, are replays */
    uint32_t gave_at;                   /* the step a step was last given back at (+1; 0 none) */
    int last_replay;
    check pend[CHECKS];                 /* (rollback) checks of steps whose buttons are not all known yet */
    long rollbacks, replayed, gave_back, predicted, checks;
    int most_replayed, resyncs;
    uint32_t resynced;                  /* (the host) the step of the last desync it went on from (+0; resyncs 0: none) */
    uint16_t host_bits;                 /* (ob_net_game.host_bits) bits only the host's buttons carry */
    check mine[CHECKS];                 /* (the host) its desync checks */
    int nmine;
    check last;                         /* (a guest) its newest, sent with its buttons */
    /* a room */
    int room, room_delay;
    uint32_t match, end_at, changes, join_step;
    int end_pending, join_pending, join_fresh;
    int hold;                           /* (a room) seats held for players who drop */
    unsigned held;                      /* (the host) machines dropped out of the match on, waited for */
    char held_name[MACH][OB_NET_NAME + 1];
    uint64_t end_heard, linger_until;   /* (a guest) when the host's END came; (the host) buttons sent until */
    char mwhy[200];
    uint8_t *msettings, *join_state;    /* the match's settings; (one that joins a match) the state it takes */
    size_t msettings_size, join_size;
    uint64_t members_at;
    int members_dirty;
    asmb as[MACH];
    msg *inbox, *inbox_end;
};

/* ---- bytes ------------------------------------------------------------------------------------ */

typedef struct out { uint8_t b[UDP_MTU]; size_t n; } out;
static void p8(out *o, uint32_t v) { if (o->n < sizeof o->b) o->b[o->n] = (uint8_t)v; o->n++; }
static void p16(out *o, uint32_t v) { p8(o, v); p8(o, v >> 8); }
static void p32(out *o, uint32_t v) { p16(o, v); p16(o, v >> 16); }
static void p64(out *o, uint64_t v) { p32(o, (uint32_t)v); p32(o, (uint32_t)(v >> 32)); }
static void pbytes(out *o, const void *b, size_t n) { size_t i; for (i = 0; i < n; i++) p8(o, ((const uint8_t *)b)[i]); }
static void pstr(out *o, const char *s) { size_t k = strlen(s); if (k > 255) k = 255; p8(o, (uint32_t)k); pbytes(o, s, k); }
static void begin(out *o, int type) { o->n = 0; p8(o, 'O'); p8(o, 'B'); p8(o, OB_NET_PROTOCOL); p8(o, (uint32_t)type); }

typedef struct in { const uint8_t *b; size_t n, size; int bad; } in;
static uint32_t g8(in *i) { if (i->n >= i->size) { i->bad = 1; return 0; } return i->b[i->n++]; }
static uint32_t g16(in *i) { uint32_t v = g8(i); return v | g8(i) << 8; }
static uint32_t g32(in *i) { uint32_t v = g16(i); return v | g16(i) << 16; }
static uint64_t g64(in *i) { uint64_t v = g32(i); return v | (uint64_t)g32(i) << 32; }
static const uint8_t *gbytes(in *i, size_t n)
{
    const uint8_t *p;
    if (i->n > i->size || i->size - i->n < n) { i->bad = 1; return NULL; }
    p = i->b + i->n;
    i->n += n;
    return p;
}

static uint64_t now(void) { return udp_ticks(); }
/* microseconds from at to t (0 when at is later: a time taken after t) */
static uint64_t since(uint64_t t, uint64_t at) { return t > at ? t - at : 0; }

static void to_slot(ob_net *n, int slot, const out *o) { if (o->n <= UDP_MTU) ob_udp_send_to(n->u, slot, o->b, o->n); }
static void to_peer(ob_net *n, int k, const out *o) { to_slot(n, n->p[k].slot, o); }

/* the machines this one talks to: a guest its host, a host its guests */
static int talks(const ob_net *n, int k) { return n->host ? k > 0 && n->p[k].used : k == 0 && n->p[0].used; }

static uint64_t next_random(ob_net *n)  /* (splitmix64) */
{
    uint64_t z = (n->rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* a name as the others may show it: printable ASCII, cut short */
static void take_name(char *to, const uint8_t *from, size_t n)
{
    size_t i, k = 0;
    for (i = 0; from && i < n && k < OB_NET_NAME; i++) to[k++] = from[i] >= 32 && from[i] < 127 ? (char)from[i] : '?';
    to[k] = 0;
}

static int count_bits(unsigned v) { int k = 0; for (; v; v &= v - 1) k++; return k; }

/* a machine as the others say it: its name, else Player and its first pad's number */
static void who(const ob_net *n, int k, char *buf, size_t size)
{
    const peer *q = &n->p[k];
    int pad = 0;
    while (pad < PADS && !(q->pads >> pad & 1)) pad++;
    if (q->name[0]) snprintf(buf, size, "%s", q->name);
    else if (pad < PADS) snprintf(buf, size, "Player %d", pad + 1);
    else snprintf(buf, size, k ? "a guest" : "the host");
}

static void end_with(ob_net *n, const char *why)
{
    if (n->phase == P_DONE) return;
    n->phase = P_DONE;
    snprintf(n->why, sizeof n->why, "%s", why);
}

static void send_bye(ob_net *n, int k, const char *why)
{
    out o;
    int i;
    begin(&o, T_BYE);
    p32(&o, n->session);
    pstr(&o, why);
    for (i = 0; i < 3; i++) to_peer(n, k, &o);          /* (three: one is enough) */
}

/* the session over for every machine: each one this one talks to told why (the host tells its guests) */
static void end_all(ob_net *n, const char *why)
{
    int k;
    if (n->phase == P_DONE) return;
    if (n->session)
        for (k = 0; k < MACH; k++) if (talks(n, k)) send_bye(n, k, why);
    end_with(n, why);
}

static void say(char *err, int size, const char *s) { if (err && size > 0) snprintf(err, (size_t)size, "%s", s); }

/* ---- the messages (a room) -------------------------------------------------------------------- */

static int lane_wait_us(const ob_net *n, int k)
{
    int w = n->p[k].rtt_us > 0 ? 2 * n->p[k].rtt_us : 0;
    return w < (int)(100 * MS) ? (int)(100 * MS) : w;
}

static void lane_free(lane *l)
{
    uint32_t i;
    for (i = 0; i < l->qn; i++) free(l->q[i]);
    free(l->q);
    for (i = 0; i < LANE_WIN; i++) free(l->in_b[i]);
    memset(l, 0, sizeof *l);
}

static void send_lane(ob_net *n, int k, frag *f)
{
    out o;
    begin(&o, T_LANE);
    p32(&o, n->session); p32(&o, f->seq); p16(&o, f->len); pbytes(&o, f->b, f->len);
    to_peer(n, k, &o);
    f->sent = 1;
    f->at = now();
}

/* a fragment for machine k (one this machine talks to), sent at once when the window has room */
static int lane_push(ob_net *n, int k, const uint8_t *b, size_t len)
{
    lane *l = &n->p[k].ln;
    frag *f;
    if (len > FRAG) return -1;
    if (l->qn == l->qcap) {
        uint32_t cap = l->qcap ? l->qcap * 2 : 64;
        frag **q = realloc(l->q, cap * sizeof *q);
        if (!q) return -1;
        l->q = q;
        l->qcap = cap;
    }
    f = malloc(sizeof *f);
    if (!f) return -1;
    f->seq = l->next++;
    f->len = (uint16_t)len;
    f->acked = f->sent = 0;
    memcpy(f->b, b, len);
    l->q[l->qn++] = f;
    if (l->qn <= LANE_WIN) send_lane(n, k, f);
    return 0;
}

/* a message (from: the machine it is from; to: the one it is for, TO_ALL) in fragments to machine k */
static int lane_message(ob_net *n, int k, int from, int to, int type, const uint8_t *data, size_t size)
{
    uint8_t b[FRAG];
    size_t at = 0;
    int first = 1;
    do {
        size_t head = first ? 8 : 4, take = size - at < FRAG - head ? size - at : FRAG - head;
        b[0] = (uint8_t)from; b[1] = (uint8_t)to; b[2] = (uint8_t)type;
        b[3] = (uint8_t)((first ? 1 : 0) | (at + take == size ? 2 : 0));
        if (first) { b[4] = (uint8_t)size; b[5] = (uint8_t)(size >> 8); b[6] = (uint8_t)(size >> 16); b[7] = (uint8_t)(size >> 24); }
        if (take) memcpy(b + head, data + at, take);
        if (lane_push(n, k, b, head + take)) return -1;
        at += take;
        first = 0;
    } while (at < size);
    return 0;
}

static void lane_pump(ob_net *n, int k, uint64_t t)
{
    lane *l = &n->p[k].ln;
    uint32_t i;
    uint64_t wait = (uint64_t)lane_wait_us(n, k);
    for (i = 0; i < l->qn && i < LANE_WIN; i++) {
        frag *f = l->q[i];
        if (!f->acked && (!f->sent || since(t, f->at) >= wait)) send_lane(n, k, f);
    }
}

static void lane_acked(ob_net *n, int k, uint32_t have, uint64_t mask)
{
    lane *l = &n->p[k].ln;
    uint32_t i, j = 0;
    for (i = 0; i < l->qn; i++) {
        frag *f = l->q[i];
        uint32_t d = f->seq - have - 1;
        if (f->seq < have || (f->seq > have && d < 64 && (mask >> d & 1))) f->acked = 1;
    }
    while (j < l->qn && l->q[j]->acked) { free(l->q[j]); j++; }
    if (j) {
        memmove(l->q, l->q + j, (l->qn - j) * sizeof *l->q);
        l->qn -= j;
        l->base += j;
        for (i = 0; i < l->qn && i < LANE_WIN; i++) if (!l->q[i]->sent) send_lane(n, k, l->q[i]);
    }
}

static void send_lane_ack(ob_net *n, int k)
{
    lane *l = &n->p[k].ln;
    uint64_t mask = 0;
    uint32_t j;
    out o;
    for (j = 0; j < 64; j++) {
        uint32_t s = l->in_have + 1 + j, w = s % LANE_WIN;
        if (l->in_b[w] && l->in_seq[w] == s) mask |= (uint64_t)1 << j;
    }
    begin(&o, T_LANE_ACK); p32(&o, n->session); p32(&o, l->in_have); p64(&o, mask);
    to_peer(n, k, &o);
}

static void core_message(ob_net *n, int from, int type, uint8_t *b, size_t size);

/* a whole message from machine `from` */
static void got_message(ob_net *n, int from, int type, uint8_t *b, size_t size)
{
    msg *m;
    if (type > OB_NET_TYPE_LAST) { core_message(n, from, type, b, size); free(b); return; }
    m = malloc(sizeof *m);
    if (!m) { free(b); return; }
    m->next = NULL; m->from = from; m->type = type; m->size = size; m->b = b;
    if (n->inbox_end) n->inbox_end->next = m; else n->inbox = m;
    n->inbox_end = m;
}

/* whether machine k is in the room (the host, or a guest that has the room's settings) */
static int member(const ob_net *n, int k)
{
    if (k < 0 || k >= MACH || !n->p[k].used) return 0;
    return k == 0 || !n->host || n->p[k].ready;
}

/* a fragment, in order, from machine k (one this machine talks to) */
static void lane_fragment(ob_net *n, int k, const uint8_t *b, size_t len)
{
    int from, to, type, flags, j;
    size_t head;
    asmb *a;
    if (len < 4) return;
    from = b[0]; to = b[1]; type = b[2]; flags = b[3];
    head = flags & 1 ? 8 : 4;
    if (len < head || from >= MACH) return;
    if (n->host) {
        if (from != k) return;                            /* (a guest sends its own) */
        if (to == TO_ALL) { for (j = 1; j < MACH; j++) if (j != k && member(n, j)) lane_push(n, j, b, len); }
        else if (to != 0) { if (member(n, to)) lane_push(n, to, b, len); return; }
    }
    a = &n->as[from];
    if (flags & 1) {
        size_t size = (size_t)b[4] | (size_t)b[5] << 8 | (size_t)b[6] << 16 | (size_t)b[7] << 24;
        free(a->b);
        a->b = NULL; a->on = 0;
        if (size > OB_NET_MAX_MESSAGE) return;
        a->b = malloc(size ? size : 1);
        if (!a->b) return;
        a->size = size; a->have = 0; a->type = type; a->on = 1;
    }
    if (!a->on) return;
    if (len - head > a->size - a->have) { free(a->b); a->b = NULL; a->on = 0; return; }
    memcpy(a->b + a->have, b + head, len - head);
    a->have += len - head;
    if (flags & 2) {
        uint8_t *whole = a->b;
        a->b = NULL; a->on = 0;
        if (a->have == a->size) got_message(n, from, a->type, whole, a->size);
        else free(whole);
    }
}

static void lane_in(ob_net *n, int k, uint32_t seq, const uint8_t *b, size_t len)
{
    lane *l = &n->p[k].ln;
    uint32_t w = seq % LANE_WIN;
    if (seq == l->in_have) {
        l->in_have++;
        lane_fragment(n, k, b, len);
        while (n->phase != P_DONE) {
            w = l->in_have % LANE_WIN;
            if (!l->in_b[w] || l->in_seq[w] != l->in_have) break;
            {
                uint8_t *held = l->in_b[w];
                l->in_b[w] = NULL;
                l->in_have++;
                lane_fragment(n, k, held, l->in_len[w]);
                free(held);
            }
        }
    } else if (seq > l->in_have && seq - l->in_have < LANE_WIN && !(l->in_b[w] && l->in_seq[w] == seq)) {
        free(l->in_b[w]);
        l->in_b[w] = malloc(len ? len : 1);
        if (l->in_b[w]) { memcpy(l->in_b[w], b, len); l->in_seq[w] = seq; l->in_len[w] = (uint16_t)len; }
    }
    send_lane_ack(n, k);
}

/* the core's own message to every member that is a guest of this host (from: the host) */
static void host_tell(ob_net *n, int k, int type, const uint8_t *b, size_t size)
{
    if (k > 0 && member(n, k)) lane_message(n, k, 0, k, type, b, size);
}

/* ---- the start -------------------------------------------------------------------------------- */

ob_net *ob_net_new(const ob_online *o, const ob_net_game *g, char *err, int err_size)
{
    char e[200];
    ob_net *n;
    uint16_t port;
    int pads, k;
    say(err, err_size, "");
    if (!o || !g) { say(err, err_size, "no settings"); return NULL; }
    pads = g->pads;
    if (pads < 2 || pads > PADS) { say(err, err_size, "the game's pads must be 2 to 16"); return NULL; }
    if (g->identity && strlen(g->identity) > 255) { say(err, err_size, "the game's identity is too long"); return NULL; }
    if (g->settings_size > OB_NET_MAX_SETTINGS || (g->settings_size && !g->settings)) { say(err, err_size, "the settings are too large"); return NULL; }
    if (o->role % 3 != ROLE_HOST && !o->addr[0]) { say(err, err_size, "no address to connect to"); return NULL; }
    n = calloc(1, sizeof *n);
    if (!n) { say(err, err_size, "out of memory"); return NULL; }
    n->role = o->role < 0 ? ROLE_HOST : o->role % 3;
    n->pads = pads;
    n->room = g->room != 0;
    n->hold = n->room && g->hold_seats;
    n->host_bits = g->host_bits;
    n->here = o->here < (n->room ? 0 : 1) ? (n->room ? 0 : 1) : o->here > pads - 1 ? pads - 1 : o->here;
    n->total = o->all <= n->here ? n->here + 1 : o->all > pads ? pads : o->all;
    n->delay = o->delay < 0 ? 0 : o->delay > OB_NET_MAX_DELAY ? OB_NET_MAX_DELAY : o->delay;
    snprintf(n->identity, sizeof n->identity, "%s", g->identity ? g->identity : "");
    take_name(n->name, (const uint8_t *)o->name, strlen(o->name));
    snprintf(n->addr, sizeof n->addr, "%s", o->addr);
    for (k = 0; k < MACH; k++) { n->p[k].slot = -1; n->p[k].rtt_us = -1; n->p[k].ping_ms = -1; }
    for (k = 0; k < PADS; k++) n->owner[k] = -1;
    port = (uint16_t)(o->port >= 1 && o->port <= 65535 ? o->port : OB_NET_PORT_DEFAULT);
    n->u = ob_udp_open(n->role == ROLE_JOIN ? 0 : port, n->role == ROLE_HOST ? NULL : o->addr, port, e, sizeof e);
    if (!n->u) { say(err, err_size, e); free(n); return NULL; }
    n->rng = now() ^ (uint64_t)(uintptr_t)n;
    n->nonce = next_random(n);
    if (g->settings_size) {
        n->settings = malloc(g->settings_size);
        if (!n->settings) { ob_udp_close(n->u); free(n); say(err, err_size, "out of memory"); return NULL; }
        memcpy(n->settings, g->settings, g->settings_size);
        n->settings_size = g->settings_size;
    }
    n->chunks = (uint32_t)((n->settings_size + CHUNK - 1) / CHUNK);
    if (g->states && g->states->save && g->states->load) {
        n->st = *g->states;
        n->rb = 1;
        n->max_rb = g->rollback <= 0 ? 12 : g->rollback > OB_NET_MAX_ROLLBACK ? OB_NET_MAX_ROLLBACK : g->rollback;
        n->nslots = n->max_rb + 2;
    }
    n->rb_to = NONE;
    n->room_delay = n->delay;
    if (n->room) n->npeers = MACH;
    if (n->role == ROLE_HOST) {
        n->host = 1;
        n->phase = n->room ? P_ROOM : P_HOSTING;
        n->changes = 1;
        n->session = (uint32_t)next_random(n) | 1;
        n->p[0].used = 1;
        n->p[0].here = n->here;
        snprintf(n->p[0].name, sizeof n->p[0].name, "%s", n->name);
    }
    return n;
}

void ob_net_free(ob_net *n)
{
    int k;
    if (!n) return;
    if (n->phase != P_DONE) ob_net_quit(n);
    ob_udp_close(n->u);
    for (k = 0; k < OB_NET_MAX_ROLLBACK + 2; k++) free(n->slot[k].b);
    for (k = 0; k < MACH; k++) { lane_free(&n->p[k].ln); free(n->as[k].b); }
    while (n->inbox) { msg *m = n->inbox; n->inbox = m->next; free(m->b); free(m); }
    free(n->msettings);
    free(n->join_state);
    free(n->settings);
    free(n->got);
    free(n);
}

int ob_net_state(ob_net *n)
{
    return n->phase == P_DONE ? OB_NET_ENDED : n->phase == P_RUNNING ? OB_NET_GO : n->phase == P_ROOM ? OB_NET_LOBBY : OB_NET_CONNECTING;
}
int ob_net_is_host(ob_net *n) { return n->host; }
unsigned ob_net_my_pads(ob_net *n) { return n->p[n->me].pads; }
int ob_net_delay(ob_net *n) { return n->delay; }
int ob_net_machines(ob_net *n) { return n->room ? count_bits(ob_net_members(n)) : n->npeers; }
uint32_t ob_net_step_count(ob_net *n) { return n->cur; }
const char *ob_net_why(ob_net *n) { return n->phase == P_DONE ? n->why : ""; }
const char *ob_net_name_of(ob_net *n, int k) { return k >= 0 && k < MACH && n->p[k].used ? n->p[k].name : ""; }
unsigned ob_net_all_pads(ob_net *n)
{
    unsigned all = 0;
    int k;
    for (k = 0; k < n->npeers; k++) all |= n->p[k].pads;
    return all;
}
const void *ob_net_settings(ob_net *n, size_t *size)
{
    int have;
    if (n->room && (n->phase == P_RUNNING || n->match)) {
        if (size) *size = n->msettings_size;
        return n->msettings_size ? n->msettings : NULL;
    }
    have = n->room ? n->host || n->got_have == n->chunks : n->host || n->phase == P_RUNNING || (n->started && n->got_have == n->chunks);
    if (size) *size = have ? n->settings_size : 0;
    return have && n->settings_size ? n->settings : NULL;
}

/* ---- the handshake ---------------------------------------------------------------------------- */

static void send_hello(ob_net *n)
{
    out o;
    begin(&o, T_HELLO);
    p8(&o, (uint32_t)n->role); p64(&o, n->nonce);
    pstr(&o, n->identity);
    pstr(&o, n->name);
    p8(&o, (uint32_t)n->here); p8(&o, (uint32_t)n->pads);
    to_slot(n, 0, &o);
}

static void send_accept(ob_net *n, int k)
{
    out o;
    begin(&o, T_ACCEPT);
    p64(&o, n->p[k].nonce); p32(&o, n->session); p8(&o, (uint32_t)n->delay); p32(&o, (uint32_t)n->settings_size);
    p8(&o, (uint32_t)n->room); p8(&o, (uint32_t)k);
    pstr(&o, n->name);
    to_peer(n, k, &o);
}

/* a HELLO refused: told why; a "both connect" machine that has no one else ends there too */
static void refuse(ob_net *n, int from, uint64_t nonce, const char *why)
{
    out o;
    begin(&o, T_REFUSE);
    p64(&o, nonce);
    pstr(&o, why);
    to_slot(n, from, &o);
    if (n->phase == P_HELLO) {
        char text[200];
        snprintf(text, sizeof text, "cannot play with them: %.170s", why);
        end_with(n, text);
    } else if (n->phase != P_RUNNING) {
        snprintf(n->note, sizeof n->note, "refused a player: %.170s", why);
    }
}

static void send_session(ob_net *n, int k, int type)
{
    out o;
    begin(&o, type);
    p32(&o, n->session);
    to_peer(n, k, &o);
}

static void send_start(ob_net *n, int k)
{
    out o;
    int i;
    begin(&o, T_START);
    p32(&o, n->session); p8(&o, (uint32_t)k); p8(&o, (uint32_t)n->npeers);
    for (i = 0; i < n->npeers; i++) { p16(&o, n->p[i].pads); pstr(&o, n->p[i].name); }
    to_peer(n, k, &o);
}

static void send_chunk(ob_net *n, int k, uint32_t i)
{
    out o;
    size_t at = (size_t)i * CHUNK, len = n->settings_size - at < CHUNK ? n->settings_size - at : CHUNK;
    begin(&o, T_CHUNK);
    p32(&o, n->session); p32(&o, i); p16(&o, (uint32_t)len); pbytes(&o, n->settings + at, len);
    to_peer(n, k, &o);
}

/* whether guest g said it has chunk i (the one at its count in a row it lacks) */
static int guest_has(const peer *g, uint32_t i)
{
    uint32_t k = i - g->set_have - 1;
    return i < g->set_have || (i > g->set_have && k < 64 && (g->set_mask >> k & 1));
}

/* the settings' chunks up to WINDOW past guest k's acknowledgement: those not sent yet and (again:
 * nothing acknowledged for a while) those it lacks */
static void send_chunks(ob_net *n, int k, int again)
{
    peer *g = &n->p[k];
    uint32_t i, end = n->chunks - g->set_have > WINDOW ? g->set_have + WINDOW : n->chunks;
    if (again)
        for (i = g->set_have; i < end && i < g->set_sent; i++) if (!guest_has(g, i)) send_chunk(n, k, i);
    for (i = g->set_sent > g->set_have ? g->set_sent : g->set_have; i < end; i++) send_chunk(n, k, i);
    if (end > g->set_sent) g->set_sent = end;
    g->set_at = now();
}

static void drop_guest(ob_net *n, int k)
{
    lane_free(&n->p[k].ln);
    free(n->as[k].b);
    memset(&n->as[k], 0, sizeof n->as[k]);
    if (n->p[k].slot >= 0) n->u->slot[n->p[k].slot].pinned = 0;
    memset(&n->p[k], 0, sizeof n->p[k]);
    n->p[k].slot = -1;
    n->p[k].rtt_us = -1;
    n->p[k].ping_ms = -1;
}

/* a guest's HELLO (from: its address's slot), taken by this machine as the host */
static void host_guest(ob_net *n, int from, uint64_t nonce, const uint8_t *id, size_t idn, const uint8_t *name, size_t namen,
                       int here, int pads)
{
    char why[200];
    int k, seats = n->total, free_k = 0;
    peer *g;
    for (k = 1; k < MACH; k++)
        if (n->p[k].used && n->p[k].nonce == nonce) { n->p[k].slot = from; send_accept(n, k); return; }   /* (it did not hear the first) */
    for (k = 0; k < MACH; k++) if (n->p[k].used) seats -= n->p[k].here;
    if (!n->p[0].used) seats -= n->here;                  /* (a "both connect" machine that is about to host) */
    {
        char nm[OB_NET_NAME + 1];
        take_name(nm, name, namen);
        for (k = MACH - 1; k > 0; k--) if (!n->p[k].used && !(n->held >> k & 1)) free_k = k;   /* (a held seat is its player's) */
        for (k = MACH - 1; k > 0; k--) if (!n->p[k].used && (n->held >> k & 1) && !strcmp(n->held_name[k], nm)) free_k = k;
    }
    if (n->room && !free_k) snprintf(why, sizeof why, "the room is full");
    else if (n->room && n->phase != P_ROOM && n->phase != P_RUNNING && !(n->role == ROLE_BOTH && n->phase == P_HELLO))
        snprintf(why, sizeof why, "the room is closing");
    else if (!n->room && n->phase == P_RUNNING) snprintf(why, sizeof why, "the game has begun");
    else if (idn != strlen(n->identity) || memcmp(id, n->identity, idn))
        snprintf(why, sizeof why, "another game, build or settings (here: %.60s; there: %.*s)", n->identity, idn > 60 ? 60 : (int)idn, (const char *)id);
    else if (pads != n->pads) snprintf(why, sizeof why, "another game (%d pads here, %d there)", n->pads, pads);
    else if (n->room) why[0] = 0;
    else if (here < 1 || here > seats || !free_k)
        snprintf(why, sizeof why, seats > 0 ? "the game has room for %d more player%s" : "the game is full", seats, seats == 1 ? "" : "s");
    else why[0] = 0;
    if (why[0]) { refuse(n, from, nonce, why); return; }
    n->host = 1;
    if (!n->room) n->phase = P_HOSTING;
    else if (n->phase == P_HELLO) { n->phase = P_ROOM; n->changes++; }   /* (both connect: this one hosts the room) */
    if (!n->p[0].used) {
        n->p[0].used = 1;
        n->p[0].here = n->here;
        snprintf(n->p[0].name, sizeof n->p[0].name, "%s", n->name);
    }
    if (!n->session) n->session = (uint32_t)next_random(n) | 1;
    g = &n->p[free_k];
    drop_guest(n, free_k);
    g->used = 1;
    g->slot = from;
    g->nonce = nonce;
    g->here = here;
    g->heard_at = now();
    take_name(g->name, name, namen);
    ob_udp_pin(n->u, from);
    send_accept(n, free_k);
    send_chunks(n, free_k, 0);
}

/* the session begins (every machine has its START and the settings): steps 0 .. delay-1 known, all zero */
static void go_running(ob_net *n)
{
    int k, w;
    uint64_t t = now();
    n->phase = P_RUNNING;
    n->cur = 0;
    n->waited = 0;
    n->waited_steps = 0;
    memset(n->btn, 0, sizeof n->btn);
    memset(n->ran, 0, sizeof n->ran);
    for (k = 0; k < OB_NET_MAX_ROLLBACK + 2; k++) n->slot[k].valid = 0;
    for (k = 0; k < CHECKS; k++) n->pend[k].used = 0;
    n->rb_to = NONE;
    n->replay_end = n->gave_at = 0;
    n->rollbacks = n->replayed = n->gave_back = n->predicted = n->checks = 0;
    n->most_replayed = 0;
    memset(n->mine, 0, sizeof n->mine);                   /* (a room's earlier match's checks are not this one's) */
    n->nmine = 0;
    memset(&n->last, 0, sizeof n->last);
    for (k = 0; k < MACH; k++) { memset(n->p[k].theirs, 0, sizeof n->p[k].theirs); n->p[k].ntheirs = 0; }
    for (k = 0; k < MACH; k++) {
        n->p[k].heard_cur = 0;
        n->known[k] = (uint32_t)n->delay;
        for (w = 0; w < MACH; w++) n->p[k].has[w] = (uint32_t)n->delay;
        if (talks(n, k)) n->p[k].heard_at = t;
    }
    n->dirty = 1;
}

/* every seat taken and every guest with the settings: the guests in the order they came (their
 * places closed up), the pads given in that order (the host's first), START to each */
static void begin_session(ob_net *n)
{
    int i, j, k = 1, pad = 0;
    for (i = 1; i < MACH; i++) {
        if (!n->p[i].used) continue;
        if (i != k) { n->p[k] = n->p[i]; memset(&n->p[i], 0, sizeof n->p[i]); n->p[i].slot = -1; n->p[i].rtt_us = -1; }
        k++;
    }
    n->npeers = k;
    n->me = 0;
    for (i = 0; i < PADS; i++) n->owner[i] = -1;
    for (i = 0; i < n->npeers; i++) {
        n->p[i].pads = 0;
        for (j = 0; j < n->p[i].here; j++, pad++) { n->p[i].pads |= 1u << pad; n->owner[pad] = (int8_t)i; }
    }
    for (i = 1; i < n->npeers; i++) send_start(n, i);
    go_running(n);
}

/* ---- rooms and matches -------------------------------------------------------------------------- */

typedef struct wbuf { uint8_t *b; size_t n, cap; int bad; } wbuf;
static void bput(wbuf *w, const void *d, size_t k)
{
    if (w->bad) return;
    if (w->n + k > w->cap) {
        size_t cap = w->cap ? w->cap : 256;
        uint8_t *b;
        while (cap < w->n + k) cap *= 2;
        b = realloc(w->b, cap);
        if (!b) { w->bad = 1; return; }
        w->b = b; w->cap = cap;
    }
    memcpy(w->b + w->n, d, k);
    w->n += k;
}
static void b8(wbuf *w, uint32_t v) { uint8_t c = (uint8_t)v; bput(w, &c, 1); }
static void b16(wbuf *w, uint32_t v) { b8(w, v); b8(w, v >> 8); }
static void b32(wbuf *w, uint32_t v) { b16(w, v); b16(w, v >> 16); }
static void bstr(wbuf *w, const char *s) { size_t k = strlen(s); if (k > 255) k = 255; b8(w, (uint32_t)k); bput(w, s, k); }

/* (the host) who is in, to every guest in the room */
static void tell_members(ob_net *n)
{
    wbuf w = { 0 };
    int k, count = 0;
    for (k = 0; k < MACH; k++) count += member(n, k);
    b8(&w, (uint32_t)count);
    for (k = 0; k < MACH; k++) {
        int ms = k == 0 ? 0 : n->p[k].rtt_us < 0 ? 0xFFFF : (n->p[k].rtt_us + 500) / 1000;
        if (!member(n, k)) continue;
        b8(&w, (uint32_t)k); b8(&w, (uint32_t)n->p[k].here); b16(&w, (uint32_t)(ms > 0xFFFE ? 0xFFFE : ms)); bstr(&w, n->p[k].name);
    }
    if (!w.bad) for (k = 1; k < MACH; k++) host_tell(n, k, M_MEMBERS, w.b, w.n);
    free(w.b);
    n->members_at = now();
    n->members_dirty = 0;
}

/* this machine's match over: back to the lobby */
static void stop_match(ob_net *n, const char *why)
{
    int k;
    if (n->phase != P_RUNNING) return;
    n->phase = P_ROOM;
    for (k = 0; k < MACH; k++) n->p[k].inm = 0;
    snprintf(n->mwhy, sizeof n->mwhy, "%s", why ? why : "");
    n->end_pending = n->join_pending = 0;
    n->held = 0;
    free(n->join_state); n->join_state = NULL; n->join_size = 0;
}

static void tell_end(ob_net *n, int k, uint32_t at, const char *why)
{
    wbuf w = { 0 };
    b32(&w, n->match); b32(&w, at); bstr(&w, why);
    if (!w.bad) host_tell(n, k, M_END, w.b, w.n);
    free(w.b);
}

/* (the host) the match ended for every machine: at the host's step (at), or now (why: someone left) */
static void end_match_all(ob_net *n, uint32_t at, const char *why)
{
    int k;
    if (n->phase != P_RUNNING) return;
    for (k = 1; k < MACH; k++) {
        n->p[k].lin = at && n->p[k].inm;
        if (n->p[k].inm) tell_end(n, k, at, why);
    }
    if (at) n->linger_until = now() + 2000 * MS;          /* (the buttons up to it, to those that lack them) */
    stop_match(n, why);
}

/* the match's message: its seats, settings and (one that joins it as it goes on) a shared state */
static int match_message(ob_net *n, wbuf *w, uint32_t step, int fresh, const void *state, size_t state_size)
{
    int p;
    b32(w, n->match); b8(w, (uint32_t)n->delay); b8(w, (uint32_t)n->pads);
    for (p = 0; p < n->pads; p++) b8(w, n->owner[p] < 0 ? 255u : (uint32_t)n->owner[p]);
    b32(w, step); b8(w, fresh ? 1u : 0u); b32(w, (uint32_t)n->msettings_size);
    if (n->msettings_size) bput(w, n->msettings, n->msettings_size);
    if (state_size) bput(w, state, state_size);
    return w->bad ? -1 : 0;
}

static void set_owners(ob_net *n, const int8_t *owner)
{
    int k, p;
    for (k = 0; k < MACH; k++) n->p[k].pads = 0;
    for (p = 0; p < PADS; p++) {
        n->owner[p] = p < n->pads ? owner[p] : -1;
        if (n->owner[p] >= 0) n->p[n->owner[p]].pads |= 1u << p;
    }
}

/* this machine takes a state at its first resumable ob_net_frame and goes on from step: joining a match
 * that goes on (the others' buttons from step come), or (fresh) every machine starting there, the
 * buttons of step .. step + delay - 1 none */
static int set_join(ob_net *n, const void *state, size_t size, uint32_t step, int fresh)
{
    int k, w;
    uint32_t from = fresh ? step + (uint32_t)n->delay : step;
    free(n->join_state);
    n->join_state = malloc(size ? size : 1);
    n->join_size = 0;
    if (!n->join_state) return -1;
    memcpy(n->join_state, state, size);
    n->join_size = size;
    n->join_pending = 1;
    n->join_fresh = fresh;
    n->join_step = step;
    n->cur = step;
    n->replay_end = step;
    for (k = 0; k < MACH; k++) { n->known[k] = from; for (w = 0; w < MACH; w++) n->p[k].has[w] = from; }
    return 0;
}

/* (the host) a match every machine of the room starts from a shared state at step */
static int begin_from_state(ob_net *n, uint32_t step, const void *state, size_t size)
{
    wbuf w = { 0 };
    int k;
    n->match++;
    for (k = 0; k < MACH; k++) n->p[k].lin = 0;
    go_running(n);
    n->mwhy[0] = 0;
    n->held = 0;
    if (set_join(n, state, size, step, 1) || match_message(n, &w, step, 1, state, size)) {
        free(w.b);
        stop_match(n, "out of memory for the match's state");
        return -1;
    }
    n->p[0].inm = 1;
    for (k = 1; k < MACH; k++)
        if (member(n, k)) { n->p[k].inm = 1; n->p[k].heard_cur = 0; host_tell(n, k, M_MATCH, w.b, w.n); }
    free(w.b);
    n->changes++;
    return 0;
}

int ob_net_match_begin(ob_net *n, const ob_net_match *m, char *err, int err_size)
{
    int p, k;
    say(err, err_size, "");
    if (!n->room || !n->host || n->phase == P_DONE) { say(err, err_size, "not the host of a room"); return -1; }
    if (n->phase == P_RUNNING) { say(err, err_size, "a match is on"); return -1; }
    n->resyncs = 0;
    if (!m || (m->settings_size && !m->settings) || m->settings_size > OB_NET_MAX_SETTINGS) { say(err, err_size, "bad settings"); return -1; }
    for (p = 0; p < n->pads; p++)
        if (m->owner[p] >= 0 && !member(n, m->owner[p])) { say(err, err_size, "a pad's machine is not in the room"); return -1; }
    free(n->msettings);
    n->msettings = NULL;
    n->msettings_size = 0;
    if (m->settings_size) {
        n->msettings = malloc(m->settings_size);
        if (!n->msettings) { say(err, err_size, "out of memory"); return -1; }
        memcpy(n->msettings, m->settings, m->settings_size);
        n->msettings_size = m->settings_size;
    }
    n->delay = m->delay < 0 ? n->room_delay : m->delay > OB_NET_MAX_DELAY ? OB_NET_MAX_DELAY : m->delay;
    set_owners(n, m->owner);
    if (m->state_size) {
        if (!m->state || !n->rb || !n->st.take) { say(err, err_size, "a match from a state needs rollback that can share states"); return -1; }
        if (begin_from_state(n, m->state_step, m->state, m->state_size)) { say(err, err_size, "out of memory"); return -1; }
        return 0;
    }
    n->match++;
    for (k = 0; k < MACH; k++) n->p[k].lin = 0;
    go_running(n);
    n->mwhy[0] = 0;
    n->p[0].inm = 1;
    {
        wbuf w = { 0 };
        if (match_message(n, &w, 0, 0, NULL, 0)) { free(w.b); say(err, err_size, "out of memory"); n->phase = P_ROOM; return -1; }
        for (k = 1; k < MACH; k++)
            if (member(n, k)) { n->p[k].inm = 1; n->p[k].heard_cur = 0; host_tell(n, k, M_MATCH, w.b, w.n); }
        free(w.b);
    }
    return 0;
}

struct slot;
static struct slot *newest_upto(ob_net *n, uint32_t step);

/* (the host) machine k came into the room while a match goes on: the newest state whose buttons are
 * all known, shared, so it can join (when there is one yet) */
static void try_join(ob_net *n, int k);
static uint32_t confirmed_of(ob_net *n);

/* the newest step a state kept here may be from and be right: every button before it known, and no wrong
 * guess before it found and not yet played again (a state after rb_to is the guess's) */
static uint32_t sound_upto(ob_net *n)
{
    uint32_t c = confirmed_of(n);
    return n->rb_to < c ? n->rb_to : c;
}

void ob_net_match_end(ob_net *n)
{
    if (!n->room || n->phase != P_RUNNING) return;
    if (n->host) end_match_all(n, n->cur, "");
    else stop_match(n, "");
}

uint32_t ob_net_match_id(ob_net *n) { return n->match; }
const char *ob_net_match_why(ob_net *n) { return n->mwhy; }
int ob_net_me(ob_net *n) { return n->me; }
unsigned ob_net_members(ob_net *n)
{
    unsigned m = 0;
    int k;
    if (!n->room) { for (k = 0; k < n->npeers; k++) m |= 1u << k; return m; }
    for (k = 0; k < MACH; k++) if (member(n, k)) m |= 1u << k;
    return m;
}
int ob_net_here_of(ob_net *n, int k) { return k >= 0 && k < MACH && n->p[k].used ? n->p[k].here : 0; }
int ob_net_ping_of(ob_net *n, int k)
{
    if (k < 0 || k >= MACH || !n->p[k].used) return -1;
    if (k == n->me) return n->host ? 0 : n->p[0].rtt_us < 0 ? -1 : (n->p[0].rtt_us + 500) / 1000;
    if (n->host) return n->p[k].rtt_us < 0 ? -1 : (n->p[k].rtt_us + 500) / 1000;
    if (k == 0) return 0;
    return n->p[k].ping_ms;
}
uint32_t ob_net_room_changes(ob_net *n) { return n->changes; }

int ob_net_send(ob_net *n, int to, int type, const void *data, size_t size)
{
    int k, sent = 0;
    if (!n->room || type < 0 || type > OB_NET_TYPE_LAST || size > OB_NET_MAX_MESSAGE || (size && !data)) return -1;
    if (n->phase != P_ROOM && n->phase != P_RUNNING) return -1;
    if (to != OB_NET_ALL && (to == n->me || !member(n, to))) return -1;
    if (n->host) {
        for (k = 1; k < MACH; k++)
            if ((to == OB_NET_ALL || to == k) && member(n, k)) {
                if (lane_message(n, k, 0, k, type, data, size)) return -1;
                sent++;
            }
        return 0;
    }
    return lane_message(n, 0, n->me, to == OB_NET_ALL ? TO_ALL : to, type, data, size);
}

int ob_net_recv(ob_net *n, int *from, int *type, void *b, size_t cap, size_t *size)
{
    msg *m = n->inbox;
    if (!m) return 0;
    if (size) *size = m->size;
    if (from) *from = m->from;
    if (type) *type = m->type;
    if (m->size > cap) return -1;
    if (m->size) memcpy(b, m->b, m->size);
    n->inbox = m->next;
    if (!n->inbox) n->inbox_end = NULL;
    free(m->b);
    free(m);
    return 1;
}

/* the core's own messages (a guest's, from its host) */
static void core_message(ob_net *n, int from, int type, uint8_t *b, size_t size)
{
    in i;
    i.b = b; i.n = 0; i.size = size; i.bad = 0;
    if (n->host || from != 0) return;
    if (type == M_MEMBERS) {
        uint32_t count = g8(&i), c;
        int seen[MACH] = { 0 }, k, me_in = 0;
        for (c = 0; c < count && !i.bad; c++) {
            uint32_t id = g8(&i), here = g8(&i), ms = g16(&i), len = g8(&i);
            const uint8_t *nm = gbytes(&i, len);
            if (i.bad || id >= MACH) return;
            seen[id] = 1;
            if (!n->p[id].used || n->p[id].here != (int)here) n->changes++;
            n->p[id].used = 1;
            n->p[id].here = (int)here;
            n->p[id].ping_ms = ms == 0xFFFF ? -1 : (int)ms;
            if (id) take_name(n->p[id].name, nm, len);
            if ((int)id == n->me) me_in = 1;
        }
        for (k = 1; k < MACH; k++) if (!seen[k] && n->p[k].used && k != n->me) { memset(&n->p[k].name, 0, sizeof n->p[k].name); n->p[k].used = 0; if (n->phase != P_RUNNING) n->p[k].pads = 0; n->changes++; }
        if (me_in && n->phase == P_GUEST) { n->phase = P_ROOM; n->changes++; }
        return;
    }
    if (type == M_MATCH) {
        uint32_t id = g8(&i), delay, pads, p, step, ssize, flags;
        int8_t owner[PADS];
        const uint8_t *set;
        id |= g8(&i) << 8; id |= g8(&i) << 16; id |= g8(&i) << 24;
        delay = g8(&i); pads = g8(&i);
        if (i.bad || pads != (uint32_t)n->pads || delay > OB_NET_MAX_DELAY) return;
        for (p = 0; p < PADS; p++) owner[p] = -1;
        for (p = 0; p < pads; p++) { uint32_t o = g8(&i); owner[p] = o == 255 ? -1 : o < MACH ? (int8_t)o : -1; }
        step = g32(&i); flags = g8(&i); ssize = g32(&i);
        set = gbytes(&i, ssize);
        if (i.bad || (n->phase != P_ROOM && n->phase != P_RUNNING)) return;
        free(n->msettings);
        n->msettings = ssize ? malloc(ssize) : NULL;
        n->msettings_size = n->msettings ? ssize : 0;
        if (n->msettings) memcpy(n->msettings, set, ssize);
        free(n->join_state); n->join_state = NULL; n->join_size = 0;
        n->match = id;
        n->delay = (int)delay;
        set_owners(n, owner);
        go_running(n);
        n->mwhy[0] = 0;
        n->end_pending = 0;
        n->join_pending = 0;
        if (i.n < i.size &&                                /* (the state to take: it joins as the match goes on, or all start from it) */
            set_join(n, i.b + i.n, i.size - i.n, step, flags & 1)) { stop_match(n, "out of memory for the match's state"); return; }
        n->changes++;
        return;
    }
    if (type == M_END) {
        uint32_t id = g32(&i), at = g32(&i), len = g8(&i);
        const uint8_t *w = gbytes(&i, len);
        if (i.bad || id != n->match || n->phase != P_RUNNING) return;
        n->end_pending = 1;
        n->end_at = at;
        n->end_heard = now();
        snprintf(n->mwhy, sizeof n->mwhy, "%.*s", (int)len, w ? (const char *)w : "");
        return;
    }
}

/* ---- the steps -------------------------------------------------------------------------------- */

/* machine whose's buttons to machine `to`, from the first it lacks; then what this one has of each
 * machine's (its acknowledgement) and its newest desync check */
static void send_buttons(ob_net *n, int to, int whose)
{
    peer *t = &n->p[to], *w = &n->p[whose];
    uint32_t f, first = t->has[whose], last = n->known[whose], count = last > first ? last - first : 0;
    int p, k;
    out o;
    if (count > MAX_SEND) count = MAX_SEND;
    if (!count && whose != n->me) return;
    begin(&o, T_INPUT);
    p32(&o, n->session);
    if (n->room) p32(&o, n->match);
    p8(&o, (uint32_t)whose); p32(&o, first); p8(&o, count); p8(&o, (uint32_t)count_bits(w->pads));
    for (f = first; f < first + count; f++)
        for (p = 0; p < PADS; p++) if (w->pads >> p & 1) p16(&o, n->btn[f % RING][p]);
    p8(&o, (uint32_t)n->npeers);
    for (k = 0; k < n->npeers; k++) p32(&o, n->known[k]);
    p32(&o, !n->host && n->last.used ? n->last.step + 1 : 0); p64(&o, n->last.hash);
    {
        long lead = t->heard_cur ? (long)n->cur - (long)t->their_cur : 0;
        p32(&o, n->cur);
        p16(&o, (uint32_t)(uint16_t)(int16_t)(lead > 30000 ? 30000 : lead < -30000 ? -30000 : lead));
    }
    to_peer(n, to, &o);
}

static void send_inputs(ob_net *n)
{
    uint64_t t = now();
    int k, w;
    if (!n->dirty && since(t, n->sent_at) < RESEND_US) return;
    for (k = 0; k < n->npeers; k++) {
        if (!talks(n, k) || (n->room && n->host && !(n->phase == P_RUNNING ? n->p[k].inm : n->p[k].lin))) continue;
        send_buttons(n, k, n->me);
        if (n->host) for (w = 1; w < n->npeers; w++) if (w != k && n->p[w].used && (!n->room || n->p[w].inm || (n->phase != P_RUNNING && n->p[w].lin))) send_buttons(n, k, w);
    }
    n->sent_at = t;
    n->dirty = 0;
}

static int go_on_from_state(ob_net *n, const char *note);
static void desync(ob_net *n, uint32_t step)
{
    char why[64], note[80];
    snprintf(why, sizeof why, "desync at step %u", (unsigned)step);
    if (n->room && n->rb && n->st.share && n->st.take && n->resyncs < OB_NET_MAX_RESYNCS) {   /* (every machine from the host's state) */
        n->resyncs++;
        n->resynced = step;
        snprintf(note, sizeof note, "desync at step %u: everyone went on from the host's state", (unsigned)step);
        if (!go_on_from_state(n, note)) return;
    }
    if (n->room) end_match_all(n, 0, why);
    else end_all(n, why);
}

/* (the host) a check, its own or a guest's, against the other side's for the same step */
static void compare(ob_net *n, const check *a, const check *list, int count)
{
    int i;
    for (i = 0; i < count && i < CHECKS; i++)
        if (list[i].used && list[i].step == a->step && list[i].hash != a->hash) { desync(n, a->step); return; }
}

static void add_check(check *list, int *count, uint32_t step, uint64_t hash)
{
    int i;
    for (i = 0; i < CHECKS; i++) if (list[i].used && list[i].step == step) { list[i].hash = hash; return; }
    list[*count % CHECKS].step = step;
    list[*count % CHECKS].hash = hash;
    list[*count % CHECKS].used = 1;
    ++*count;
}

static void check_now(ob_net *n, uint32_t step, uint64_t sim_hash)
{
    check c;
    int k;
    c.step = step; c.hash = sim_hash; c.used = 1;
    n->checks++;
    if (!n->host) { n->last = c; return; }
    add_check(n->mine, &n->nmine, step, sim_hash);
    for (k = 1; k < n->npeers && n->phase == P_RUNNING; k++) compare(n, &c, n->p[k].theirs, CHECKS);
}

/* rollback: a step's check waits until every button up to it is known (a replay's replaces it) */
static void check_due(ob_net *n, uint32_t confirmed)
{
    int k;
    for (k = 0; k < CHECKS && n->phase == P_RUNNING; k++)
        if (n->pend[k].used && n->pend[k].step < confirmed) { n->pend[k].used = 0; check_now(n, n->pend[k].step, n->pend[k].hash); }
}

void ob_net_check(ob_net *n, uint32_t step, uint64_t sim_hash)
{
    int k, free_k = -1;
    if (n->phase != P_RUNNING) return;
    if (!n->rb) { check_now(n, step, sim_hash); return; }
    for (k = 0; k < CHECKS; k++) {
        if (n->pend[k].used && n->pend[k].step == step) { n->pend[k].hash = sim_hash; return; }
        if (!n->pend[k].used && free_k < 0) free_k = k;
    }
    if (free_k < 0) {                                     /* (full: the oldest goes) */
        free_k = 0;
        for (k = 1; k < CHECKS; k++) if (n->pend[k].step < n->pend[free_k].step) free_k = k;
    }
    n->pend[free_k].step = step; n->pend[free_k].hash = sim_hash; n->pend[free_k].used = 1;
}

/* ---- the messages ----------------------------------------------------------------------------- */

static int sender(const ob_net *n, int from)
{
    int k;
    if (!n->host) return n->p[0].used && n->p[0].slot == from ? 0 : -1;
    for (k = 1; k < MACH; k++) if (n->p[k].used && n->p[k].slot == from) return k;
    return -1;
}

static void take_inputs(ob_net *n, in *i, int whose, uint32_t first, uint32_t count)
{
    peer *w = &n->p[whose];
    uint32_t j;
    int p;
    for (j = 0; j < count; j++) {
        uint32_t f = first + j;
        uint16_t b[PADS] = { 0 };
        for (p = 0; p < PADS; p++) if (w->pads >> p & 1) b[p] = (uint16_t)g16(i);
        if (i->bad) return;
        if (whose) for (p = 0; p < PADS; p++) b[p] &= (uint16_t)~n->host_bits;   /* (the host's alone) */
        if (f != n->known[whose] || f >= n->cur + RING / 2) continue;   /* (had already, or a gap: it comes again) */
        for (p = 0; p < PADS; p++)
            if (w->pads >> p & 1) {
                n->btn[f % RING][p] = b[p];
                if (f < n->cur && n->ran[f % RING][p] != b[p] && f < n->rb_to) n->rb_to = f;   /* (a wrong guess) */
            }
        n->known[whose]++;
        n->dirty = 1;                                     /* (acknowledged, and passed on, at once) */
    }
}

/* (a room's host) guest k left or went quiet: out of the room, and the match over if it played in it */
static void member_gone(ob_net *n, int k, const char *why)
{
    int played = n->p[k].inm && n->p[k].pads, was = member(n, k);
    if (played && n->hold && n->phase == P_RUNNING && n->rb && n->st.share && n->st.take) {
        n->held |= 1u << k;                               /* (its seat waits for it: the match stops at its buttons) */
        memcpy(n->held_name[k], n->p[k].name, sizeof n->held_name[k]);
    } else if (played) end_match_all(n, 0, why);
    snprintf(n->note, sizeof n->note, "%.190s", why);
    drop_guest(n, k);
    if (was) { n->changes++; tell_members(n); }
}

static void try_join(ob_net *n, int k)
{
    struct slot *t;
    size_t need;
    uint8_t *state;
    wbuf w = { 0 };
    int j;
    if (!n->rb || !n->st.share || !n->st.take || n->phase != P_RUNNING || n->p[k].inm || !member(n, k)) return;
    t = newest_upto(n, sound_upto(n));
    if (!t) return;
    need = n->st.share(n->st.user, t->b, t->size, NULL, 0);
    if (!need) return;
    state = malloc(need);
    if (!state) return;
    if (n->st.share(n->st.user, t->b, t->size, state, need) != need || match_message(n, &w, t->step, 0, state, need)) { free(state); free(w.b); return; }
    free(state);
    host_tell(n, k, M_MATCH, w.b, w.n);
    free(w.b);
    n->p[k].inm = 1;
    n->p[k].heard_cur = 0;
    for (j = 0; j < MACH; j++) n->p[k].has[j] = t->step;
}

/* (the host) a held player is back in the room: every machine goes on from the newest state whose buttons
 * were all known, as a new match with the same seats (their buttons after it are played again) */
static void resume_held(ob_net *n)
{
    int k;
    for (k = 1; k < MACH; k++) if ((n->held >> k & 1) && !(member(n, k) && n->p[k].ready)) return;   /* (all of them back) */
    if (go_on_from_state(n, "back")) end_match_all(n, 0, "the match could not go on from a state");
}

/* (the host) every machine goes on from the host's newest state whose buttons were all known, as a new match
 * with the same seats: 0, or -1 (no such state) */
static int go_on_from_state(ob_net *n, const char *note)
{
    struct slot *t;
    size_t need;
    uint8_t *state;
    int8_t owner[PADS];
    uint32_t step;
    t = newest_upto(n, sound_upto(n));
    if (!t || !(need = n->st.share(n->st.user, t->b, t->size, NULL, 0)) || !(state = malloc(need))) return -1;
    step = t->step;
    if (n->st.share(n->st.user, t->b, t->size, state, need) != need) { free(state); return -1; }
    memcpy(owner, n->owner, sizeof owner);
    set_owners(n, owner);
    if (begin_from_state(n, step, state, need)) { free(state); return -1; }
    snprintf(n->note, sizeof n->note, "%.150s: the match goes on from step %u", note, (unsigned)step);
    free(state);
    return 0;
}

unsigned ob_net_waiting_for(ob_net *n) { return n->phase == P_RUNNING ? n->held : 0; }

size_t ob_net_share_confirmed(ob_net *n, uint32_t *step, void *buf, size_t cap)
{
    struct slot *t;
    if (!n->rb || !n->st.share || n->phase != P_RUNNING || n->join_pending) return 0;
    t = newest_upto(n, sound_upto(n));
    if (!t) return 0;
    if (step) *step = t->step;
    return n->st.share(n->st.user, t->b, t->size, buf, cap);
}

static void handle(ob_net *n, const uint8_t *b, size_t size, int from)
{
    in i;
    uint32_t version, type, session;
    uint64_t t = now();
    int s;
    peer *sp;
    i.b = b; i.n = 0; i.size = size; i.bad = 0;
    if (g8(&i) != 'O' || g8(&i) != 'B') return;
    version = g8(&i);
    type = g8(&i);
    if (i.bad) return;
    if (type == T_HELLO) {
        uint32_t role = g8(&i), idn, namen, here, pads;
        uint64_t nonce = g64(&i);
        const uint8_t *id, *name;
        int host_it;
        if (i.bad || n->phase == P_DONE || nonce == n->nonce || n->phase == P_GUEST || (n->phase == P_RUNNING && !n->host)) return;
        host_it = n->host || (n->role == ROLE_BOTH && (role == ROLE_JOIN || (role == ROLE_BOTH && n->nonce > nonce)));
        if (version != OB_NET_PROTOCOL) {
            char why[96];
            snprintf(why, sizeof why, "another version of Overbar's netplay (%d here, %u there)", OB_NET_PROTOCOL, (unsigned)version);
            if (n->host || n->role == ROLE_BOTH) refuse(n, from, nonce, why);
            return;
        }
        idn = g8(&i); id = gbytes(&i, idn);
        namen = g8(&i); name = gbytes(&i, namen);
        here = g8(&i); pads = g8(&i);
        if (i.bad) return;
        if (n->role == ROLE_JOIN && role == ROLE_JOIN) { refuse(n, from, nonce, "both are joining: one must host"); return; }
        if (host_it) host_guest(n, from, nonce, id, idn, name, namen, (int)here, (int)pads);
        return;
    }
    if (type == T_REFUSE) {                               /* (from any version) */
        uint64_t nonce = g64(&i);
        uint32_t k = g8(&i);
        const uint8_t *why = gbytes(&i, k);
        char text[200];
        if (i.bad || nonce != n->nonce || n->phase != P_HELLO) return;
        snprintf(text, sizeof text, "%s refused: %.*s", n->role == ROLE_JOIN ? "the host" : "the other machine", (int)k, (const char *)why);
        end_with(n, text);
        return;
    }
    if (version != OB_NET_PROTOCOL) return;
    if (type == T_ACCEPT) {
        uint64_t nonce = g64(&i);
        uint32_t delay, ssize, namen;
        const uint8_t *name;
        uint32_t room, id;
        session = g32(&i); delay = g8(&i); ssize = g32(&i);
        room = g8(&i); id = g8(&i);
        namen = g8(&i); name = gbytes(&i, namen);
        if (i.bad || nonce != n->nonce || n->phase != P_HELLO || n->host || delay > OB_NET_MAX_DELAY || ssize > OB_NET_MAX_SETTINGS) return;
        if ((int)room != n->room) { end_with(n, room ? "the host has a room; this game wants one game" : "the host has one game; this game wants a room"); return; }
        if (room) {
            if (id < 1 || id >= MACH) return;
            n->me = (int)id;
            n->p[id].used = 1;
            n->p[id].here = n->here;
            snprintf(n->p[id].name, sizeof n->p[id].name, "%s", n->name);
        }
        n->session = session;
        n->delay = (int)delay;
        n->p[0].used = 1;
        n->p[0].slot = from;
        n->p[0].heard_at = t;
        take_name(n->p[0].name, name, namen);
        ob_udp_pin(n->u, from);
        free(n->settings);
        n->settings_size = ssize;
        n->chunks = (ssize + CHUNK - 1) / CHUNK;
        n->settings = ssize ? malloc(ssize) : NULL;
        n->got = n->chunks ? calloc(n->chunks, 1) : NULL;
        if ((ssize && !n->settings) || (n->chunks && !n->got)) { end_with(n, "out of memory for the host's settings"); return; }
        n->phase = P_GUEST;
        return;
    }
    session = g32(&i);
    if (i.bad || !n->session || session != n->session || n->phase == P_DONE) return;
    s = sender(n, from);
    if (s < 0) return;
    sp = &n->p[s];
    sp->heard_at = t;
    switch (type) {
    case T_CHUNK: {
        uint32_t idx = g32(&i), k = g16(&i), j;
        const uint8_t *d = gbytes(&i, k);
        size_t at = (size_t)idx * CHUNK;
        uint64_t mask = 0;
        out o;
        if (!d || n->host || !n->got || idx >= n->chunks || k != (n->settings_size - at < CHUNK ? n->settings_size - at : CHUNK)) return;
        if (!n->got[idx]) { memcpy(n->settings + at, d, k); n->got[idx] = 1; }
        while (n->got_have < n->chunks && n->got[n->got_have]) n->got_have++;
        for (j = 0; j < 64 && n->got_have + 1 + j < n->chunks; j++) if (n->got[n->got_have + 1 + j]) mask |= (uint64_t)1 << j;
        begin(&o, T_CHUNK_ACK); p32(&o, n->session); p32(&o, n->got_have); p64(&o, mask);
        to_peer(n, 0, &o);
        break;
    }
    case T_CHUNK_ACK: {
        uint32_t have = g32(&i);
        uint64_t mask = g64(&i);
        int moved;
        if (i.bad || !n->host || have > n->chunks || have < sp->set_have) return;
        moved = have > sp->set_have;
        sp->set_have = have;
        sp->set_mask = mask;
        if (moved) send_chunks(n, s, 0);
        break;
    }
    case T_READY:
        if (n->host && n->room) {
            if (!sp->ready && sp->set_have == n->chunks) {
                sp->ready = 1;
                n->members_dirty = 1;
                n->changes++;
                tell_members(n);
                if (n->phase == P_RUNNING) { if (n->held >> s & 1) resume_held(n); else try_join(n, s); }
            }
            break;
        }
        if (n->host && sp->set_have == n->chunks) { sp->ready = 1; if (n->phase == P_RUNNING) send_start(n, s); }
        break;
    case T_LANE: {
        uint32_t seq = g32(&i), k = g16(&i);
        const uint8_t *d = gbytes(&i, k);
        if (!d || !n->room || k > FRAG || (n->host && !sp->ready)) return;
        lane_in(n, s, seq, d, k);
        break;
    }
    case T_LANE_ACK: {
        uint32_t have = g32(&i);
        uint64_t mask = g64(&i);
        if (i.bad || !n->room) return;
        lane_acked(n, s, have, mask);
        break;
    }
    case T_START: {
        uint32_t me = g8(&i), np = g8(&i), k;
        unsigned pads[MACH], all = 0;
        char names[MACH][OB_NET_NAME + 1];
        if (n->host || n->started || i.bad || np < 2 || np > MACH || me < 1 || me >= np) return;
        for (k = 0; k < np; k++) {
            uint32_t len;
            const uint8_t *nm;
            pads[k] = g16(&i);
            len = g8(&i);
            nm = gbytes(&i, len);
            if (i.bad || !pads[k] || (pads[k] & all) || (pads[k] >> n->pads)) return;
            all |= pads[k];
            take_name(names[k], nm, len);
        }
        if (count_bits(pads[me]) != n->here) return;
        n->me = (int)me;
        n->npeers = (int)np;
        for (k = 0; k < np; k++) {
            int p;
            n->p[k].used = 1;
            n->p[k].pads = pads[k];
            n->p[k].here = count_bits(pads[k]);
            if (k) n->p[k].slot = -1;
            memcpy(n->p[k].name, names[k], sizeof n->p[k].name);
            for (p = 0; p < PADS; p++) if (pads[k] >> p & 1) n->owner[p] = (int8_t)k;
        }
        n->started = 1;
        break;
    }
    case T_INPUT: {
        uint32_t match = n->room ? g32(&i) : 0, whose = g8(&i), first = g32(&i), count = g8(&i), npads = g8(&i), np, k, cs;
        uint64_t ch;
        if (i.bad || n->phase != P_RUNNING) return;       /* (buttons come again once it runs) */
        if (n->room && (match != n->match || (n->host && !sp->inm) || n->join_pending)) return;
        if (whose >= (uint32_t)n->npeers || !n->p[whose].used || (int)whose == n->me || npads != (uint32_t)count_bits(n->p[whose].pads)) return;
        if (n->host && (int)whose != s) return;           /* (a guest sends its own) */
        take_inputs(n, &i, (int)whose, first, count);
        np = g8(&i);
        for (k = 0; k < np; k++) {
            uint32_t v = g32(&i);
            if (k < (uint32_t)n->npeers && !i.bad) {
                if (v > n->known[k]) v = n->known[k];     /* (it can have no more than there is here to send) */
                if (v > sp->has[k]) sp->has[k] = v;
            }
        }
        cs = g32(&i);
        ch = g64(&i);
        {
            uint32_t their = g32(&i);
            int lead = (int16_t)g16(&i);
            if (!i.bad && (!sp->heard_cur || their > sp->their_cur)) { sp->their_cur = their; sp->their_lead = lead; sp->heard_cur = 1; }
        }
        if (i.bad || !n->host || !cs) return;
        {
            check c;
            c.step = cs - 1; c.hash = ch; c.used = 1;
            add_check(sp->theirs, &sp->ntheirs, c.step, ch);
            compare(n, &c, n->mine, CHECKS);
        }
        break;
    }
    case T_ROOM: {
        uint32_t in_room = g8(&i), total = g8(&i);
        if (i.bad || n->host || in_room > total) return;
        n->room_in = (int)in_room;
        n->room_total = (int)total;
        break;
    }
    case T_PING: {
        out o;
        uint64_t sent = g64(&i);
        if (i.bad) return;
        begin(&o, T_PONG); p32(&o, n->session); p64(&o, sent);
        to_peer(n, s, &o);
        break;
    }
    case T_PONG: {
        uint64_t sent = g64(&i);
        int sample;
        if (i.bad || sent > t) return;
        sample = (int)(t - sent);
        sp->rtt_us = sp->rtt_us < 0 ? sample : (sp->rtt_us * 7 + sample) / 8;
        break;
    }
    case T_BYE: {
        uint32_t k = g8(&i);
        const uint8_t *w = gbytes(&i, k);
        char why[200], name[40];
        who(n, s, name, sizeof name);
        if (w && k) snprintf(why, sizeof why, "%.*s", (int)k, (const char *)w);
        else snprintf(why, sizeof why, "%s left", name);
        if (n->host && n->room) { member_gone(n, s, why); break; }
        if (n->host && n->phase == P_HOSTING) {           /* (it left the waiting room: its seat given up) */
            snprintf(n->note, sizeof n->note, "%.190s", why);
            drop_guest(n, s);
            break;
        }
        if (n->host) end_all(n, why);                     /* (the host tells the others) */
        else end_with(n, why);
        break;
    }
    default: break;
    }
}

static void receive(ob_net *n)
{
    uint8_t b[UDP_MTU + 64];
    int k;
    for (k = 0; k < 1000 && n->phase != P_DONE; k++) {
        int from = -1, got = ob_udp_recv_from(n->u, b, sizeof b, &from);
        if (got <= 0) break;
        handle(n, b, (size_t)got, from);
    }
}

int ob_net_poll(ob_net *n)
{
    uint64_t t;
    int k;
    if (n->phase == P_DONE) return OB_NET_ENDED;
    receive(n);
    if (n->phase == P_DONE) return OB_NET_ENDED;
    t = now();
    if (n->phase == P_HELLO && since(t, n->hello_at) >= HELLO_US) { send_hello(n); n->hello_at = t; }
    if (n->room && n->host && (n->phase == P_ROOM || n->phase == P_RUNNING)) {
        for (k = 1; k < MACH && n->phase != P_DONE; k++) {
            peer *g = &n->p[k];
            uint64_t wait;
            if (!g->used) continue;
            if (since(t, g->heard_at) > GONE_US) {
                char why[120], name[40];
                who(n, k, name, sizeof name);
                snprintf(why, sizeof why, "%s was not heard from for %d seconds", name, OB_NET_TIMEOUT_MS / 1000);
                member_gone(n, k, why);
                continue;
            }
            wait = g->rtt_us > 0 ? 2 * (uint64_t)g->rtt_us : 0;
            if (wait < 100 * MS) wait = 100 * MS;
            if (!g->ready && g->set_have < n->chunks && since(t, g->set_at) >= wait) send_chunks(n, k, 1);
            if (g->ready && n->phase == P_RUNNING && !g->inm) { if (n->held >> k & 1) resume_held(n); else try_join(n, k); }
        }
        if (n->members_dirty || since(t, n->members_at) >= MEMBERS_US) tell_members(n);
    }
    if (n->phase == P_HOSTING) {
        int all_ready = 1, seats = n->total, guests = 0;
        for (k = 0; k < MACH; k++) {
            peer *g = &n->p[k];
            uint64_t wait;
            if (!g->used) continue;
            seats -= g->here;
            if (k == 0) continue;
            guests++;
            if (since(t, g->heard_at) > GONE_US) {              /* (gone before the game: its seat given up) */
                char name[40];
                who(n, k, name, sizeof name);
                snprintf(n->note, sizeof n->note, "%s went quiet", name);
                drop_guest(n, k);
                all_ready = 0;
                continue;
            }
            wait = g->rtt_us > 0 ? 2 * (uint64_t)g->rtt_us : 0;
            if (wait < 100 * MS) wait = 100 * MS;
            if (g->set_have < n->chunks && since(t, g->set_at) >= wait) send_chunks(n, k, 1);
            if (!g->ready) all_ready = 0;
        }
        if (since(t, n->room_at) >= PING_US) {                  /* (each guest told who is in so far) */
            out o;
            n->room_at = t;
            begin(&o, T_ROOM); p32(&o, n->session); p8(&o, (uint32_t)(n->total - seats)); p8(&o, (uint32_t)n->total);
            for (k = 1; k < MACH; k++) if (n->p[k].used) to_peer(n, k, &o);
        }
        if (guests && all_ready && seats <= 0) begin_session(n);
    }
    if (n->phase == P_GUEST) {
        if (n->got_have == n->chunks && !n->started && since(t, n->ready_at) >= HELLO_US) { send_session(n, 0, T_READY); n->ready_at = t; }
        if (n->started && n->got_have == n->chunks) go_running(n);
    }
    if ((n->phase == P_GUEST || n->phase == P_RUNNING || n->phase == P_ROOM) && !(n->room && n->host)) {   /* (a machine not heard from) */
        for (k = 0; k < MACH && n->phase != P_DONE; k++) {
            char why[120], name[40];
            if (!talks(n, k) || since(t, n->p[k].heard_at) <= GONE_US) continue;
            who(n, k, name, sizeof name);
            snprintf(why, sizeof why, "%s was not heard from for %d seconds", name, OB_NET_TIMEOUT_MS / 1000);
            if (n->host) { n->p[k].used = 0; end_all(n, why); }   /* (the others told; not the one gone) */
            else end_with(n, why);
        }
    }
    if (n->phase == P_RUNNING && !n->join_pending) send_inputs(n);
    else if (n->room && n->host && n->phase == P_ROOM && t < n->linger_until) send_inputs(n);
    if (n->room && n->phase != P_DONE)
        for (k = 0; k < MACH; k++) if (talks(n, k)) lane_pump(n, k, t);
    if (n->phase != P_DONE && n->session)
        for (k = 0; k < MACH; k++)
            if (talks(n, k) && since(t, n->p[k].ping_at) >= PING_US) {
                out o;
                begin(&o, T_PING); p32(&o, n->session); p64(&o, t);
                to_peer(n, k, &o);
                n->p[k].ping_at = t;
            }
    return ob_net_state(n);
}

/* ---- the step: lockstep, or rollback ---------------------------------------------------------- */

static struct slot *slot_of(ob_net *n, uint32_t step)
{
    struct slot *t = &n->slot[step % (uint32_t)n->nslots];
    return t->valid && t->step == step ? t : NULL;
}

/* the newest state of a step at or before `step` (NULL: none) */
static struct slot *newest_upto(ob_net *n, uint32_t step)
{
    struct slot *best = NULL;
    int k;
    for (k = 0; k < n->nslots; k++) {
        struct slot *t = &n->slot[k];
        if (t->valid && t->step <= step && (!best || t->step > best->step)) best = t;
    }
    return best;
}

static void save_state(ob_net *n)
{
    struct slot *t = &n->slot[n->cur % (uint32_t)n->nslots];
    size_t need;
    t->valid = 0;
    need = n->st.save(n->st.user, t->b, t->cap);
    if (need > t->cap) {
        uint8_t *b = realloc(t->b, need);
        if (!b) return;
        t->b = b;
        t->cap = need;
        need = n->st.save(n->st.user, t->b, t->cap);
        if (need > t->cap) return;
    }
    if (!need) return;
    t->size = need;
    t->step = n->cur;
    t->valid = 1;
}

/* whether this machine is ahead of one it talks to by enough to give a step back: its lead on
 * them less theirs on it is twice how far ahead it runs (the network's delay cancels out) */
static int ahead(ob_net *n)
{
    int k;
    if (n->gave_at && n->cur < n->gave_at - 1 + GIVE_EVERY) return 0;
    for (k = 0; k < n->npeers; k++) {
        const peer *q = &n->p[k];
        long mine;
        if (!talks(n, k) || !q->heard_cur || (n->room && n->host && !q->inm)) continue;
        mine = (long)n->cur - (long)q->their_cur;
        if (mine - q->their_lead >= 2) return 1;
    }
    return 0;
}

/* the steps whose every button is known: the least any machine that plays a pad has given (a match
 * nobody plays: this machine's own) */
static uint32_t confirmed_of(ob_net *n)
{
    uint32_t c = NONE;
    int k;
    if (n->phase != P_RUNNING) return 0;
    for (k = 0; k < n->npeers; k++) if ((n->p[k].pads || (n->held >> k & 1)) && n->known[k] < c) c = n->known[k];
    return c == NONE ? n->known[n->me] : c;
}

uint32_t ob_net_confirmed(ob_net *n) { return confirmed_of(n); }

int ob_net_frame(ob_net *n, int resumable, const uint16_t *local, uint16_t *all)
{
    int k, p, j;
    uint32_t confirmed, s;
    if (n->phase == P_DONE) return -1;
    if (n->phase != P_RUNNING) {
        if (ob_net_poll(n) == OB_NET_ENDED) return -1;
        return n->room && n->phase == P_ROOM ? OB_NET_OVER : OB_NET_WAIT;
    }
    if (n->join_pending) {                                /* (it joins a match that goes on: the state taken here) */
        int r;
        if (ob_net_poll(n) == OB_NET_ENDED) return -1;
        if (n->phase != P_RUNNING) return OB_NET_OVER;
        if (!resumable) return OB_NET_WAIT;
        r = n->st.take ? n->st.take(n->st.user, n->join_state, n->join_size) : -1;
        free(n->join_state); n->join_state = NULL; n->join_size = 0;
        n->join_pending = 0;
        if (r) { stop_match(n, "the match's state would not load"); return OB_NET_OVER; }
        n->cur = n->join_step;
        n->dirty = 1;
        return OB_NET_RESTART;
    }
    s = n->cur + (uint32_t)n->delay;
    if (s == n->known[n->me]) {                          /* (this machine's buttons for step cur + delay, once; none in a replay) */
        unsigned mine = n->p[n->me].pads;
        for (p = 0, j = 0; p < PADS; p++)
            if (mine >> p & 1) { n->btn[s % RING][p] = (uint16_t)((local ? local[j] : 0) & (n->me ? (unsigned)(uint16_t)~n->host_bits : 0xFFFFu)); j++; }
        n->known[n->me] = s + 1;
        n->dirty = 1;
    }
    if (ob_net_poll(n) == OB_NET_ENDED) return -1;
    if (n->room && n->phase != P_RUNNING) return OB_NET_OVER;
    if (n->join_pending) return OB_NET_WAIT;             /* (a match from a state came just now: taken at the next call) */
    if (n->rb && n->rb_to < n->cur) {                     /* a wrong guess: back to the state before it */
        struct slot *t = newest_upto(n, n->rb_to);
        if (!t || n->st.load(n->st.user, t->b, t->size)) {
            end_all(n, t ? "a state would not load" : "a rollback went further back than its states");
            return -1;
        }
        for (k = 0; k < n->nslots; k++) if (n->slot[k].valid && n->slot[k].step > t->step) n->slot[k].valid = 0;
        for (k = 0; k < CHECKS; k++) if (n->pend[k].used && n->pend[k].step >= t->step) n->pend[k].used = 0;   /* (they are played again) */
        if (n->cur > n->replay_end) n->replay_end = n->cur;
        n->rollbacks++;
        n->replayed += (long)(n->cur - t->step);
        if ((int)(n->cur - t->step) > n->most_replayed) n->most_replayed = (int)(n->cur - t->step);
        n->cur = t->step;
        n->rb_to = NONE;
        n->waited = 0;
        return OB_NET_RESTART;
    }
    if (n->end_pending && n->cur >= n->end_at) {         /* (the host ended the match here, once its buttons are in and a wrong guess
                                                             before it has been played again) */
        char why[200];
        if (n->end_at && confirmed_of(n) < n->end_at && since(now(), n->end_heard) < 3000 * MS) return OB_NET_WAIT;
        snprintf(why, sizeof why, "%s", n->mwhy);
        stop_match(n, why);
        return OB_NET_OVER;
    }
    confirmed = confirmed_of(n);
    if (n->rb) check_due(n, confirmed);
    if (n->rb && resumable && !slot_of(n, n->cur)) save_state(n);
    if (n->cur >= confirmed) {                            /* someone's buttons have not come */
        struct slot *t;
        if (!n->rb || !(t = newest_upto(n, confirmed)) || n->cur + 1 - t->step > (uint32_t)n->max_rb) {
            if (!n->waited) { n->waited = 1; n->waited_steps++; }
            return OB_NET_WAIT;
        }
    }
    if (n->rb && n->cur >= n->replay_end && ahead(n)) {   /* (ahead of the others: a step given back) */
        n->gave_at = n->cur + 1;
        n->gave_back++;
        return OB_NET_WAIT;
    }
    n->last_replay = n->cur < n->replay_end;
    if (n->cur >= confirmed && !n->last_replay) n->predicted++;
    for (p = 0; p < PADS; p++) {
        int o = n->owner[p];
        uint16_t b = 0;
        if (o >= 0) {
            if (n->known[o] > n->cur) b = n->btn[n->cur % RING][p];
            else if (n->known[o]) b = (uint16_t)(n->btn[(n->known[o] - 1) % RING][p] & ~n->host_bits);   /* (the guess: the last known, held;
                                                                                                 never the host's bits) */
        }
        all[p] = b;
        n->ran[n->cur % RING][p] = b;
    }
    n->cur++;
    n->waited = 0;
    return OB_NET_RUN;
}

int ob_net_step(ob_net *n, const uint16_t *local, uint16_t *all)
{
    if (n->rb) {                                          /* (rollback's steps go through ob_net_frame) */
        int r = ob_net_frame(n, 0, local, all);
        return r == OB_NET_RESTART ? 0 : r;
    }
    return ob_net_frame(n, 0, local, all);
}

int ob_net_replaying(ob_net *n) { return n->phase == P_RUNNING && n->last_replay; }

unsigned ob_net_pads_of(ob_net *n, int machine)
{
    if (machine < 0 || machine >= MACH || n->phase != P_RUNNING) return 0;
    return n->p[machine].pads;
}

void ob_net_stats_get(ob_net *n, ob_net_stats *s)
{
    int k, rtt = -1;
    memset(s, 0, sizeof *s);
    s->step = n->cur;
    s->confirmed = confirmed_of(n);
    s->rollbacks = n->rollbacks;
    s->replayed = n->replayed;
    s->most_replayed = n->most_replayed;
    s->gave_back = n->gave_back;
    s->waited = n->waited_steps;
    s->checks = n->checks;
    s->resyncs = n->resyncs;
    for (k = 0; k < MACH; k++) if (talks(n, k) && n->p[k].rtt_us > rtt) rtt = n->p[k].rtt_us;
    s->rtt_ms = rtt < 0 ? -1 : (rtt + 500) / 1000;
    for (k = 0; k < MACH; k++) if (talks(n, k) && n->p[k].ping_ms > s->rtt_ms) s->rtt_ms = n->p[k].ping_ms;   /* (a room's host: as it said) */
}

void ob_net_quit(ob_net *n)
{
    char why[80], name[40];
    if (n->phase == P_DONE) return;
    who(n, n->me, name, sizeof name);
    snprintf(why, sizeof why, "%s left", name);
    if (n->session) {
        int k;
        for (k = 0; k < MACH; k++) if (talks(n, k)) send_bye(n, k, why);
    }
    end_with(n, "you left");
}

void ob_net_status(ob_net *n, char *line, int size)
{
    size_t sz;
    if (!line || size <= 0) return;
    sz = (size_t)size;
    if (n->phase == P_DONE) { snprintf(line, sz, "%s", n->why); return; }
    if (n->phase == P_RUNNING && n->held) {
        int k;
        size_t at;
        snprintf(line, sz, "waiting for ");
        for (k = 1; k < MACH; k++)
            if (n->held >> k & 1) { at = strlen(line); snprintf(line + at, sz - at, "%s%s", at > 12 ? ", " : "", n->held_name[k][0] ? n->held_name[k] : "a player"); }
        at = strlen(line);
        snprintf(line + at, sz - at, " to come back");
        return;
    }
    if (n->phase == P_RUNNING) {
        int k, rtt = -1;
        char ms[16];
        for (k = 0; k < MACH; k++) if (talks(n, k) && n->p[k].rtt_us > rtt) rtt = n->p[k].rtt_us;
        if (rtt < 0) snprintf(ms, sizeof ms, "--");
        else snprintf(ms, sizeof ms, "%d", (rtt + 500) / 1000);
        if (n->rb)
            snprintf(line, sz, "%s ms, delay %d, rollback %.1f steps, waiting %.1f%%", ms, n->delay,
                     n->rollbacks ? (double)n->replayed / n->rollbacks : 0.0, n->cur ? 100.0 * n->waited_steps / n->cur : 0.0);
        else
            snprintf(line, sz, "%s ms, delay %d, waiting %.1f%%", ms, n->delay, n->cur ? 100.0 * n->waited_steps / n->cur : 0.0);
        return;
    }
    if (n->phase == P_ROOM) {
        int c = ob_net_machines(n);
        snprintf(line, sz, "in the room: %d machine%s%s%s", c, c == 1 ? "" : "s", n->mwhy[0] ? "; " : n->note[0] ? "; " : "",
                 n->mwhy[0] ? n->mwhy : n->note);
        return;
    }
    if (n->phase == P_HOSTING) {
        int k, in_room = 0;
        for (k = 0; k < MACH; k++) if (n->p[k].used) in_room += n->p[k].here;
        snprintf(line, sz, "waiting for players: %d of %d%s%s", in_room, n->total, n->note[0] ? "; " : "", n->note);
        return;
    }
    if (n->phase == P_HELLO) {
        char err[100];
        snprintf(err, sizeof err, "%s", n->u->error);
        snprintf(line, sz, "calling %s%s%s%s", n->addr, n->role == ROLE_BOTH ? " (they call you too)" : "",
                 err[0] ? ": " : "", err);
        return;
    }
    if (n->got_have < n->chunks) snprintf(line, sz, "receiving the host's settings: %u%%", (unsigned)(100u * n->got_have / n->chunks));
    else if (n->room_total) snprintf(line, sz, "waiting for players: %d of %d", n->room_in, n->room_total);
    else snprintf(line, sz, "waiting for the host");
}

const char *ob_net_lan_address(char *buf, int size) { return size > 0 ? ob_udp_lan_address(buf, (size_t)size) : buf; }

void ob_net_simulate(ob_net *n, int delay_ms, int jitter_ms, int loss_pct, int dup_pct)
{
    ob_udp_simulate(n->u, delay_ms, jitter_ms, loss_pct, dup_pct);
}
