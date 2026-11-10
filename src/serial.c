/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A serial port carried by the room (overbar/serial.h). Messages to the other machine, the first byte says which:
 *   'D' the bytes that follow (written while connected)
 *   'C' a call (dialing)   'A' the call answered   'H' hung up
 * Registers: 0 RBR / THR (DLL with LCR bit 7), 1 IER (DLM), 2 IIR / FCR, 3 LCR, 4 MCR, 5 LSR, 6 MSR, 7 SCR. */
#include "overbar/serial.h"
#include <stdio.h>
#include <string.h>

enum { MCR_DTR = 1, MCR_OUT2 = 8, MCR_LOOP = 16 };
enum { MSR_CTS = 0x10, MSR_DSR = 0x20, MSR_RI = 0x40, MSR_DCD = 0x80 };

static void to_peer(ob_serial *p, uint8_t kind)
{
    if (p->send) p->send(p->ud, &kind, 1);
}

static void flush(ob_serial *p)
{
    uint8_t b[OB_SERIAL_TX + 1];
    if (!p->tx_n) return;
    b[0] = 'D';
    memcpy(b + 1, p->tx, (size_t)p->tx_n);
    if (p->send) p->send(p->ud, b, (size_t)p->tx_n + 1);
    p->tx_n = 0;
}

static uint8_t msr_lines(const ob_serial *p)
{
    uint8_t m = 0;
    if (p->mode == OB_SERIAL_DIRECT) {
        if (p->peer) m |= MSR_CTS | MSR_DSR | MSR_DCD;
    } else {
        m |= MSR_CTS | MSR_DSR;         /* (a modem is always ready) */
        if (p->carrier) m |= MSR_DCD;
        if (p->ringing) m |= MSR_RI;
    }
    return m;
}

/* the lines change: their delta bits (DCD, DSR, CTS changed; RI's trailing edge) */
static void lines_now(ob_serial *p, uint8_t before)
{
    uint8_t after = msr_lines(p), ch = before ^ after;
    if (ch & MSR_CTS) p->msr_delta |= 1;
    if (ch & MSR_DSR) p->msr_delta |= 2;
    if ((before & MSR_RI) && !(after & MSR_RI)) p->msr_delta |= 4;
    if (ch & MSR_DCD) p->msr_delta |= 8;
}

static void rx_byte(ob_serial *p, uint8_t b)
{
    if (p->rx_n == OB_SERIAL_RX) { p->overrun = 1; return; }
    p->rx[(p->rx_head + p->rx_n++) % OB_SERIAL_RX] = b;
}

static void rx_text(ob_serial *p, const char *s) { while (*s) rx_byte(p, (uint8_t)*s++); }

/* a result code, as V and Q say */
static void result(ob_serial *p, int code, const char *text)
{
    char b[48];
    if (p->quiet) return;
    if (p->verbose) snprintf(b, sizeof b, "\r\n%s\r\n", text);
    else snprintf(b, sizeof b, "%d\r", code);
    rx_text(p, b);
}

static void set_call(ob_serial *p, int call, int carrier)
{
    uint8_t before = msr_lines(p);
    p->call = call;
    p->carrier = carrier;
    if (call == OB_SERIAL_ONLINE) p->ringing = 0;
    lines_now(p, before);
}

static void connected(ob_serial *p)
{
    set_call(p, OB_SERIAL_ONLINE, 1);
    p->plus = 0;
    p->last_data = p->now;
    result(p, 1, p->connect);
}

static void hang_up(ob_serial *p, int tell_peer, int say)
{
    int was = p->call;
    flush(p);
    set_call(p, OB_SERIAL_IDLE, 0);
    if (tell_peer && was != OB_SERIAL_IDLE) to_peer(p, 'H');
    if (say && was != OB_SERIAL_IDLE) result(p, 3, "NO CARRIER");
}

void ob_serial_init(ob_serial *p, int mode, void (*send)(void *, const void *, size_t), void *ud)
{
    memset(p, 0, sizeof *p);
    p->mode = mode;
    p->send = send;
    p->ud = ud;
    p->echo = 1;
    p->verbose = 1;
    p->s[2] = '+';
    p->s[12] = 50;                      /* 1/50 s units: 1 s */
    snprintf(p->connect, sizeof p->connect, "CONNECT 9600");
    p->dll = 12;                        /* 9600 baud */
    if (mode == OB_SERIAL_DIRECT) p->call = OB_SERIAL_IDLE;
}

/* ---- the modem's commands ---- */

static int num(const char **s)
{
    int v = 0;
    while (**s >= '0' && **s <= '9') v = v * 10 + *(*s)++ - '0';
    return v;
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static void command(ob_serial *p, const char *line)
{
    const char *s = line;
    int ok = 1;
    while (*s == ' ') s++;
    {                                   /* a Hayes modem's line starts at its AT: what came before (a +++ sent while
                                           offline, line noise) is not part of it */
        const char *at = s;
        while (at[0] && !(upper(at[0]) == 'A' && upper(at[1]) == 'T')) at++;
        if (at[0]) s = at;
    }
    if (upper(s[0]) != 'A' || upper(s[1]) != 'T') { if (*s) result(p, 4, "ERROR"); return; }
    s += 2;
    while (*s && ok) {
        int c = upper(*s++), v;
        if (c == ' ') continue;
        if (c == '&') { if (*s) s++; num(&s); continue; }       /* &F, &C1, &D2, &K ...: taken */
        if (c == 'D') {                                         /* the number does not matter: the room is the line */
            if (p->ringing) { to_peer(p, 'A'); connected(p); return; }      /* (both dialed: the call meets) */
            set_call(p, OB_SERIAL_DIALING, 0);
            to_peer(p, 'C');
            return;                                             /* CONNECT comes when it is answered */
        }
        if (c == 'A') {
            if (p->ringing) { to_peer(p, 'A'); connected(p); }
            else set_call(p, OB_SERIAL_ANSWERING, 0);           /* waits for a call */
            return;
        }
        if (c == 'O') { if (p->carrier) { set_call(p, OB_SERIAL_ONLINE, 1); result(p, 1, p->connect); } else result(p, 3, "NO CARRIER"); return; }
        if (c == 'S') {
            int r = num(&s);
            if (*s == '=') { s++; v = num(&s); if (r >= 0 && r < 16) p->s[r] = (uint8_t)v; }
            else if (*s == '?') { char b[16]; s++; snprintf(b, sizeof b, "\r\n%03d\r\n", r >= 0 && r < 16 ? p->s[r] : 0); rx_text(p, b); }
            continue;
        }
        v = num(&s);
        switch (c) {
        case 'E': p->echo = v; break;
        case 'Q': p->quiet = v; break;
        case 'V': p->verbose = v; break;
        case 'H': if (p->call != OB_SERIAL_IDLE) hang_up(p, 1, 0); break;
        case 'Z': p->echo = 1; p->quiet = 0; p->verbose = 1; p->s[0] = 0; if (p->call != OB_SERIAL_IDLE) hang_up(p, 1, 0); break;
        case 'X': case 'M': case 'L': case 'F': case 'B': case 'C': case 'N': case 'W': case 'P': case 'T': case 'Y': case 'I': break;
        default: ok = 0; break;
        }
    }
    if (ok) result(p, 0, "OK");
    else result(p, 4, "ERROR");
}

/* a byte the game wrote */
static void written(ob_serial *p, uint8_t b)
{
    if (p->mcr & MCR_LOOP) { rx_byte(p, b); return; }
    if (p->mode == OB_SERIAL_DIRECT || p->call == OB_SERIAL_ONLINE) {
        if (p->mode == OB_SERIAL_DIRECT && !p->peer) return;
        if (p->mode == OB_SERIAL_MODEM) {                       /* +++ after a second of quiet */
            if (b == p->s[2] && (p->plus ? 1 : p->now - p->last_data >= p->s[12] * 20u)) { if (++p->plus == 3) p->plus_at = p->now; }
            else p->plus = 0;
            if (b != p->s[2]) p->last_data = p->now;
        }
        p->tx[p->tx_n++] = b;
        if (p->tx_n == OB_SERIAL_TX) flush(p);
        return;
    }
    if (p->call == OB_SERIAL_DIALING || p->call == OB_SERIAL_ANSWERING) {      /* a key while it waits: given up */
        set_call(p, OB_SERIAL_IDLE, 0);
        result(p, 3, "NO CARRIER");
        return;
    }
    if (p->echo) rx_byte(p, b);
    if (b == '\r') {
        p->cmd[p->ncmd] = 0;
        command(p, p->cmd);
        p->ncmd = 0;
    } else if (b == 8) {
        if (p->ncmd) p->ncmd--;
    } else if (b != '\n' && p->ncmd < (int)sizeof p->cmd - 1) {
        p->cmd[p->ncmd++] = (char)b;
    }
}

/* ---- the UART ---- */

static int rx_ready(const ob_serial *p) { return p->rx_n > 0; }

static int iir_now(const ob_serial *p)
{
    if ((p->ier & 4) && p->overrun) return 0x06;
    if ((p->ier & 1) && rx_ready(p)) return 0x04;
    if ((p->ier & 2) && p->thre_pending) return 0x02;
    if ((p->ier & 8) && p->msr_delta) return 0x00;
    return 0x01;
}

int ob_serial_irq(const ob_serial *p) { return (p->mcr & MCR_OUT2) && iir_now(p) != 0x01; }

int ob_serial_irq_edge(ob_serial *p)
{
    int now = ob_serial_irq(p), edge = now && !p->irq_was;
    p->irq_was = now;
    return edge;
}

uint8_t ob_serial_in(ob_serial *p, int reg)
{
    uint8_t v;
    switch (reg & 7) {
    case 0:
        if (p->lcr & 0x80) return p->dll;
        if (!p->rx_n) return 0;
        v = p->rx[p->rx_head];
        p->rx_head = (p->rx_head + 1) % OB_SERIAL_RX;
        p->rx_n--;
        return v;
    case 1: return (p->lcr & 0x80) ? p->dlm : p->ier;
    case 2:
        v = (uint8_t)iir_now(p);
        if (v == 0x02) p->thre_pending = 0;                     /* (reading it says it) */
        return (uint8_t)(v | ((p->fcr & 1) ? 0xC0 : 0));
    case 3: return p->lcr;
    case 4: return p->mcr;
    case 5:
        v = (uint8_t)(0x60 | (rx_ready(p) ? 1 : 0) | (p->overrun ? 2 : 0));       /* THR and the shifter always empty */
        p->overrun = 0;
        return v;
    case 6: {
        uint8_t m = p->mcr & MCR_LOOP ? (uint8_t)((p->mcr & 1) << 5 | (p->mcr & 2) << 3 | (p->mcr & 4) << 4 | (p->mcr & 8) << 4) : msr_lines(p);
        v = (uint8_t)(m | p->msr_delta);
        p->msr_delta = 0;
        return v;
    }
    default: return p->scr;
    }
}

void ob_serial_out(ob_serial *p, int reg, uint8_t v)
{
    switch (reg & 7) {
    case 0:
        if (p->lcr & 0x80) { p->dll = v; return; }
        written(p, v);
        p->thre_pending = 1;                                    /* sent at once: the register is empty again */
        return;
    case 1:
        if (p->lcr & 0x80) { p->dlm = v; return; }
        if ((v & 2) && !(p->ier & 2)) p->thre_pending = 1;      /* (enabling it with THR empty interrupts at once) */
        p->ier = v & 0x0F;
        return;
    case 2:
        p->fcr = v;
        if (v & 2) { p->rx_n = 0; p->rx_head = 0; }
        return;
    case 3: p->lcr = v; return;
    case 4: {
        uint8_t was = p->mcr;
        p->mcr = v & 0x1F;
        if (p->mode == OB_SERIAL_MODEM && (was & MCR_DTR) && !(v & MCR_DTR) && p->call != OB_SERIAL_IDLE)
            hang_up(p, 1, 1);                                   /* DTR dropped: the modem hangs up (&D2) */
        return;
    }
    case 7: p->scr = v; return;
    default: return;
    }
}

/* ---- the other machine ---- */

void ob_serial_peer(ob_serial *p, int present)
{
    uint8_t before = msr_lines(p);
    if (p->peer == present) return;
    p->peer = present;
    if (!present) {
        p->ringing = 0;
        if (p->mode == OB_SERIAL_MODEM && p->call != OB_SERIAL_IDLE) {
            int was = p->call;
            set_call(p, OB_SERIAL_IDLE, 0);
            if (was == OB_SERIAL_ONLINE) result(p, 3, "NO CARRIER");
        }
    }
    lines_now(p, before);
}

void ob_serial_message(ob_serial *p, const void *data, size_t size)
{
    const uint8_t *d = (const uint8_t *)data;
    size_t i;
    if (!size) return;
    switch (d[0]) {
    case 'D':
        if (p->mode == OB_SERIAL_MODEM && p->call != OB_SERIAL_ONLINE) return;     /* (not connected: nothing heard) */
        for (i = 1; i < size; i++) rx_byte(p, d[i]);
        return;
    case 'C':
        if (p->mode != OB_SERIAL_MODEM) return;
        if (p->call == OB_SERIAL_ANSWERING || p->call == OB_SERIAL_DIALING || p->s[0]) { to_peer(p, 'A'); connected(p); return; }
        if (p->call == OB_SERIAL_IDLE) {
            uint8_t before = msr_lines(p);
            p->ringing = 1;
            lines_now(p, before);
            result(p, 2, "RING");
        }
        return;
    case 'A':
        if (p->mode == OB_SERIAL_MODEM && p->call == OB_SERIAL_DIALING) connected(p);
        return;
    case 'H':
        if (p->mode == OB_SERIAL_MODEM) {
            p->ringing = 0;
            if (p->call == OB_SERIAL_ONLINE) { flush(p); set_call(p, OB_SERIAL_IDLE, 0); result(p, 3, "NO CARRIER"); }
            else if (p->call != OB_SERIAL_IDLE) set_call(p, OB_SERIAL_IDLE, 0);
        }
        return;
    default: return;
    }
}

void ob_serial_poll(ob_serial *p, uint32_t now_ms)
{
    p->now = now_ms;
    flush(p);
    if (p->mode == OB_SERIAL_MODEM && p->call == OB_SERIAL_ONLINE && p->plus == 3 && now_ms - p->plus_at >= p->s[12] * 20u) {
        p->plus = 0;
        p->call = OB_SERIAL_IDLE;                               /* command mode, still connected (ATO goes back) */
        result(p, 0, "OK");
    }
}

void ob_serial_status(const ob_serial *p, char *line, int size)
{
    const char *what = "";
    if (p->mode == OB_SERIAL_DIRECT) what = p->peer ? "Cable: connected" : "Cable: nobody at the other end";
    else if (p->call == OB_SERIAL_ONLINE) what = "Modem: connected";
    else if (p->call == OB_SERIAL_DIALING) what = "Modem: calling";
    else if (p->call == OB_SERIAL_ANSWERING) what = "Modem: waiting for a call";
    else if (p->ringing) what = "Modem: ringing";
    else if (p->carrier) what = "Modem: connected (commands)";
    snprintf(line, (size_t)size, "%s", what);
}
