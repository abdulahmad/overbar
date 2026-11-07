/* Adapted from PulseMan (https://github.com/1nputs/pulseman), its src/net/udp.c. MIT License.
 * Copyright (c) 2026 The pulseman authors.
 *
 * Overbar's changes: no mdrt types (no link struct, no command line options); every address heard
 * from has a slot (the peer given at open the first) and the netplay core pins the slots of its
 * machines so a stranger never pushes one out; the default port is the caller's; all static, for
 * src/net.c, which includes this file (one translation unit, so a port's build needs no new rule).
 *
 * UDP: POSIX sockets, Winsock 2 on Windows (link ws2_32), IPv4 and IPv6 (an IPv6 socket that takes
 * IPv4 too, or IPv4 alone where there is no IPv6), names resolved (a name's IPv4 address first).
 * Nothing here blocks. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
typedef int socklen;
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
typedef socklen_t socklen;
#endif

#define UDP_MTU 1200                    /* a datagram, at most */
#define UDP_SLOTS 16                    /* addresses heard from, the least lately heard (not pinned) given up for a new one */

typedef struct udp_held { uint64_t at; size_t n; struct sockaddr_storage to; socklen to_len; uint8_t b[UDP_MTU]; } udp_held;

typedef struct ob_udp {
    sock_t s;
    int family;
    struct { struct sockaddr_storage a; socklen len; uint64_t heard; int used, pinned; } slot[UDP_SLOTS];
    uint16_t port;
    int sim[4];                         /* the simulated network: delay, jitter, loss, duplicates */
    uint32_t rng;
    udp_held *q;                        /* datagrams it holds back */
    int nq, capq;
    char error[96];                     /* why the last datagram could not go ("" when it went) */
} ob_udp;

static void udp_put_out(ob_udp *u, const struct sockaddr_storage *to, socklen len, const void *b, size_t n);

static uint64_t udp_ticks(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / f.QuadPart * 1000000 + c.QuadPart % f.QuadPart * 1000000 / f.QuadPart);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
#endif
}

static int udp_started(void)
{
#ifdef _WIN32
    static int done;
    if (!done) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return 0; done = 1; }
#endif
    return 1;
}

/* "host", "host:port", "[v6]:port", "v6" (more than one colon, no brackets) into a name and port */
static int udp_split(const char *addr, char *host, size_t hn, uint16_t *port, uint16_t def_port)
{
    const char *colon = NULL;
    *port = def_port;
    if (addr[0] == '[') {
        const char *close = strchr(addr, ']');
        if (!close || (size_t)(close - addr - 1) >= hn) return -1;
        memcpy(host, addr + 1, (size_t)(close - addr - 1));
        host[close - addr - 1] = 0;
        if (close[1] == ':') colon = close + 1;
        else if (close[1]) return -1;
    } else {
        const char *c = strchr(addr, ':');
        size_t k;
        if (c && !strchr(c + 1, ':')) colon = c;          /* (one colon: a port; more: an IPv6 address) */
        k = colon ? (size_t)(colon - addr) : strlen(addr);
        if (k >= hn || !k) return -1;
        memcpy(host, addr, k);
        host[k] = 0;
    }
    if (colon) {
        char *end;
        long p = strtol(colon + 1, &end, 10);
        if (*end || p < 1 || p > 65535) return -1;
        *port = (uint16_t)p;
    }
    return 0;
}

static int udp_resolve(const char *addr, uint16_t def_port, struct sockaddr_storage *out, socklen *len, char *err, size_t errsize)
{
    char host[256], service[8];
    uint16_t port;
    struct addrinfo hints, *res = NULL;
    const struct addrinfo *pick, *a;
    if (udp_split(addr, host, sizeof host, &port, def_port)) { snprintf(err, errsize, "not an address: %.150s", addr); return -1; }
    snprintf(service, sizeof service, "%u", (unsigned)port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, service, &hints, &res) || !res) { snprintf(err, errsize, "cannot find %.150s", host); return -1; }
    pick = res;                                           /* (a name's IPv4 address first: a LAN's IPv6 is often link-local, or not let through) */
    for (a = res; a; a = a->ai_next) if (a->ai_family == AF_INET) { pick = a; break; }
    memcpy(out, pick->ai_addr, pick->ai_addrlen);
    *len = (socklen)pick->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

/* an IPv4 address as the IPv6 socket's mapped one */
static void udp_to_family(int family, struct sockaddr_storage *a, socklen *len)
{
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
    if (family != AF_INET6 || a->ss_family != AF_INET) return;
    memcpy(&v4, a, sizeof v4);
    memset(&v6, 0, sizeof v6);
    v6.sin6_family = AF_INET6;
    v6.sin6_port = v4.sin_port;
    v6.sin6_addr.s6_addr[10] = v6.sin6_addr.s6_addr[11] = 0xFF;
    memcpy(&v6.sin6_addr.s6_addr[12], &v4.sin_addr, 4);
    memset(a, 0, sizeof *a);
    memcpy(a, &v6, sizeof v6);
    *len = (socklen)sizeof v6;
}

static sock_t udp_make_socket(int family, uint16_t port, uint16_t *bound)
{
    sock_t s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_storage a;
    socklen len;
    int room = 1 << 20;                                   /* (room for a burst of the settings' chunks; the system caps it) */
    if (s == BAD_SOCK) return BAD_SOCK;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&room, sizeof room);
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&room, sizeof room);
    memset(&a, 0, sizeof a);
    if (family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&a;
        int off = 0;                                      /* (IPv4 too, as mapped addresses) */
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof off);
        v6->sin6_family = AF_INET6; v6->sin6_port = htons(port);   /* (the address all zero: any) */
        len = (socklen)sizeof *v6;
    } else {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&a;
        v4->sin_family = AF_INET; v4->sin_port = htons(port); v4->sin_addr.s_addr = htonl(INADDR_ANY);
        len = (socklen)sizeof *v4;
    }
    if (bind(s, (struct sockaddr *)&a, len)) { close_sock(s); return BAD_SOCK; }
#ifdef _WIN32
    {
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
    }
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    len = (socklen)sizeof a;
    getsockname(s, (struct sockaddr *)&a, &len);
    *bound = ntohs(a.ss_family == AF_INET6 ? ((struct sockaddr_in6 *)&a)->sin6_port : ((struct sockaddr_in *)&a)->sin_port);
    return s;
}

/* a socket on port (0: any free one), and the peer ("host", "host:port", "1.2.3.4:port",
 * "[v6]:port"; def_port when none is given) in slot 0, or NULL (a host: whoever sends); NULL on
 * failure, with why in err */
static ob_udp *ob_udp_open(uint16_t port, const char *peer, uint16_t def_port, char *err, size_t errsize)
{
    static const int families[2] = { AF_INET6, AF_INET };
    struct sockaddr_storage pa;
    socklen pl = 0;
    ob_udp *u;
    int k;
    if (!udp_started()) { snprintf(err, errsize, "no sockets (Winsock would not start)"); return NULL; }
    u = calloc(1, sizeof *u);
    if (!u) { snprintf(err, errsize, "out of memory"); return NULL; }
    u->s = BAD_SOCK;
    if (peer && udp_resolve(peer, def_port, &pa, &pl, err, errsize)) { free(u); return NULL; }
    /* an IPv6 socket that takes IPv4 too, unless the peer is IPv4 or there is no IPv6 */
    for (k = peer && pa.ss_family == AF_INET ? 1 : 0; k < 2 && u->s == BAD_SOCK; k++) {
        u->s = udp_make_socket(families[k], port, &u->port);
        u->family = families[k];
    }
    if (u->s == BAD_SOCK) { snprintf(err, errsize, "cannot open UDP port %u (in use?)", (unsigned)port); free(u); return NULL; }
    if (peer) {
        udp_to_family(u->family, &pa, &pl);
        if (pa.ss_family != u->family) { snprintf(err, errsize, "%s is not reachable from this socket", peer); close_sock(u->s); free(u); return NULL; }
        u->slot[0].a = pa;
        u->slot[0].len = pl;
        u->slot[0].used = 1;
        u->slot[0].heard = udp_ticks();
    }
    u->rng = (uint32_t)udp_ticks() | 1;
    return u;
}

static void ob_udp_close(ob_udp *u)
{
    int i;
    if (!u) return;
    for (i = 0; i < u->nq; i++) udp_put_out(u, &u->q[i].to, u->q[i].to_len, u->q[i].b, u->q[i].n);   /* (what the simulator held: sent now) */
    if (u->s != BAD_SOCK) close_sock(u->s);
    free(u->q);
    free(u);
}

static void ob_udp_pin(ob_udp *u, int k) { if (k >= 0 && k < UDP_SLOTS) u->slot[k].pinned = 1; }

static uint32_t udp_rnd(ob_udp *u) { u->rng ^= u->rng << 13; u->rng ^= u->rng >> 17; u->rng ^= u->rng << 5; return u->rng; }

static void udp_put_out(ob_udp *u, const struct sockaddr_storage *to, socklen len, const void *b, size_t n)
{
    if (sendto(u->s, (const char *)b, (int)n, 0, (const struct sockaddr *)to, len) >= 0) { u->error[0] = 0; return; }
#ifdef _WIN32
    {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return;                  /* (the buffer full for now: not the network's doing) */
        if (e == WSAEHOSTUNREACH) snprintf(u->error, sizeof u->error, "no route to the host");
        else if (e == WSAENETUNREACH) snprintf(u->error, sizeof u->error, "the network is unreachable");
        else if (e == WSAEACCES) snprintf(u->error, sizeof u->error, "not allowed (a firewall?)");
        else if (e == WSAEADDRNOTAVAIL) snprintf(u->error, sizeof u->error, "the address is not available");
        else snprintf(u->error, sizeof u->error, "Winsock error %d", e);
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS || errno == EINTR) return;
    snprintf(u->error, sizeof u->error, "%s", strerror(errno));
#endif
}

/* the datagrams held back whose time has come */
static void udp_release(ob_udp *u)
{
    uint64_t t;
    int i;
    if (!u->nq) return;
    t = udp_ticks();
    for (i = 0; i < u->nq;) {
        if (u->q[i].at <= t) { udp_put_out(u, &u->q[i].to, u->q[i].to_len, u->q[i].b, u->q[i].n); u->q[i] = u->q[--u->nq]; }
        else i++;
    }
}

static void udp_hold(ob_udp *u, const struct sockaddr_storage *to, socklen len, const void *b, size_t n)
{
    udp_held *h;
    if (u->nq == u->capq) {
        int cap = u->capq ? 2 * u->capq : 64;
        udp_held *q = realloc(u->q, (size_t)cap * sizeof *q);
        if (!q) return;
        u->q = q;
        u->capq = cap;
    }
    h = &u->q[u->nq++];
    h->at = udp_ticks() + (uint64_t)u->sim[0] * 1000u + (u->sim[1] ? udp_rnd(u) % ((uint32_t)u->sim[1] * 1000u) : 0);
    h->n = n;
    h->to = *to;
    h->to_len = len;
    memcpy(h->b, b, n);
}

/* a datagram to slot k's address, through the simulated network when there is one: 0, or -1 */
static int ob_udp_send_to(ob_udp *u, int k, const void *b, size_t n)
{
    const struct sockaddr_storage *to;
    socklen len;
    if (k < 0 || k >= UDP_SLOTS || !u->slot[k].used || n > UDP_MTU) return -1;
    to = &u->slot[k].a;
    len = u->slot[k].len;
    udp_release(u);
    if (!(u->sim[0] | u->sim[1] | u->sim[2] | u->sim[3])) { udp_put_out(u, to, len, b, n); return 0; }
    if ((int)(udp_rnd(u) % 100) < u->sim[2]) return 0;
    udp_hold(u, to, len, b, n);
    if ((int)(udp_rnd(u) % 100) < u->sim[3]) udp_hold(u, to, len, b, n);
    return 0;
}

static int udp_same_address(const struct sockaddr_storage *a, socklen alen, const struct sockaddr_storage *b, socklen blen)
{
    if (alen != blen || a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a, *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port && !memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr);
    } else {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
        return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
}

/* the next datagram there is: its length (0: none, -1: an error) and its sender */
static int udp_take(ob_udp *u, void *buf, size_t size, struct sockaddr_storage *from, socklen *len)
{
    udp_release(u);
    for (;;) {
        int n;
        *len = (socklen)sizeof *from;
        n = (int)recvfrom(u->s, (char *)buf, (int)size, 0, (struct sockaddr *)from, len);
        if (n >= 0) return n;
#ifdef _WIN32
        {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return 0;
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;   /* (an ICMP reply to an earlier send; too long) */
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR || errno == ECONNREFUSED) continue;
#endif
        return -1;
    }
}

/* the next datagram and its sender's slot (a new address takes a free slot, else the one not
 * pinned heard from least lately; none free: dropped). Its length, 0 when none waits, -1 on an error */
static int ob_udp_recv_from(ob_udp *u, void *buf, size_t size, int *from)
{
    for (;;) {
        struct sockaddr_storage a;
        socklen len;
        int i, k = -1, n = udp_take(u, buf, size, &a, &len);
        if (n <= 0) return n;
        for (i = 0; i < UDP_SLOTS && k < 0; i++) if (u->slot[i].used && udp_same_address(&u->slot[i].a, u->slot[i].len, &a, len)) k = i;
        if (k < 0) {
            for (i = 0; i < UDP_SLOTS && k < 0; i++) if (!u->slot[i].used) k = i;
            if (k < 0) {
                int old = -1;
                for (i = 0; i < UDP_SLOTS; i++)
                    if (!u->slot[i].pinned && (old < 0 || u->slot[i].heard < u->slot[old].heard)) old = i;
                k = old;
            }
            if (k < 0) continue;                          /* (every slot pinned) */
            u->slot[k].a = a;
            u->slot[k].len = len;
            u->slot[k].used = 1;
            u->slot[k].pinned = 0;
        }
        u->slot[k].heard = udp_ticks();
        *from = k;
        return n;
    }
}

/* this machine's address on its own network (a LAN's, as 192.168.1.20: the one its traffic to the
 * internet leaves by; no datagram is sent), into buf; "" when it has none */
static const char *ob_udp_lan_address(char *buf, size_t size)
{
    sock_t s;
    struct sockaddr_in to, me;
    socklen len = (socklen)sizeof me;
    if (!size) return buf;
    buf[0] = 0;
    if (!udp_started()) return buf;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCK) return buf;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808u);              /* (a public address: the route there names the LAN's) */
    if (!connect(s, (struct sockaddr *)&to, (socklen)sizeof to) && !getsockname(s, (struct sockaddr *)&me, &len)) {
        uint32_t a = ntohl(me.sin_addr.s_addr);
        if (a && a != 0x7F000001u)
            snprintf(buf, size, "%u.%u.%u.%u", (unsigned)(a >> 24), (unsigned)(a >> 16 & 255), (unsigned)(a >> 8 & 255), (unsigned)(a & 255));
    }
    close_sock(s);
    return buf;
}

static void ob_udp_simulate(ob_udp *u, int delay_ms, int jitter_ms, int loss_pct, int dup_pct)
{
    u->sim[0] = delay_ms > 0 ? delay_ms : 0;
    u->sim[1] = jitter_ms > 0 ? jitter_ms : 0;
    u->sim[2] = loss_pct > 0 ? loss_pct : 0;
    u->sim[3] = dup_pct > 0 ? dup_pct : 0;
}
