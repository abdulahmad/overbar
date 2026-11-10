/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The serial port carried by the room (overbar/serial.h), two machines with messages passed in memory. Each runs a
 * toy driver written the way NHL 96's is (its serial driver at B103C .. B1830): the divisor and line set-up, the FIFO
 * probe (FCR 87h, IIR bit 7), DTR, RTS and OUT2, receive and transmitter-empty interrupts, a send ring the interrupt
 * empties, and its modem code's AT commands (ATE1, ATQ0, ATX4, ATS0=1 to answer, ATD to dial). They connect, send
 * 20 kB each way (bytes of every value, "+++" among them), escape with +++ and a second of quiet, go back with ATO,
 * then one drops DTR and the other hears NO CARRIER. A null-modem cable too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "overbar/serial.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s (line %d)\n", what, __LINE__); fails++; } } while (0)

typedef struct drv {
    ob_serial u;
    uint8_t tx[4096];                   /* the driver's send ring */
    int tx_head, tx_n;
    uint8_t got[65536];                 /* everything read */
    int ngot;
    int irqs;
} drv;
static drv D[2];
static uint32_t now_ms;

typedef struct msg { int to; uint8_t d[2048]; size_t n; } msg;
static msg Q[4096];
static int qn;
static int peer_up = 1;

static void send0(void *ud, const void *d, size_t n) { (void)ud; if (peer_up && qn < 4096 && n <= 2048) { Q[qn].to = 1; memcpy(Q[qn].d, d, n); Q[qn++].n = n; } }
static void send1(void *ud, const void *d, size_t n) { (void)ud; if (peer_up && qn < 4096 && n <= 2048) { Q[qn].to = 0; memcpy(Q[qn].d, d, n); Q[qn++].n = n; } }

/* the interrupt handler: IIR until nothing is pending */
static void isr(drv *x)
{
    int guard = 0;
    x->irqs++;
    for (;;) {
        uint8_t iir = ob_serial_in(&x->u, 2) & 0x0F;
        if (iir & 1 || ++guard > 10000) break;
        if (iir == 4 || iir == 0x0C) {
            while (ob_serial_in(&x->u, 5) & 1) { uint8_t b = ob_serial_in(&x->u, 0); if (x->ngot < (int)sizeof x->got) x->got[x->ngot++] = b; }
        } else if (iir == 2) {
            if (x->tx_n && (ob_serial_in(&x->u, 5) & 0x20)) {
                ob_serial_out(&x->u, 0, x->tx[x->tx_head]);
                x->tx_head = (x->tx_head + 1) % (int)sizeof x->tx;
                x->tx_n--;
            }
        } else if (iir == 0) {
            ob_serial_in(&x->u, 6);
        } else if (iir == 6) {
            ob_serial_in(&x->u, 5);
        }
    }
}

static void irq_check(drv *x) { if (ob_serial_irq_edge(&x->u)) isr(x); else if (ob_serial_irq(&x->u)) isr(x); }

/* the driver's send (B1328): into the ring, then one byte straight out if THR is empty */
static void dsend(drv *x, const void *data, int n)
{
    const uint8_t *d = (const uint8_t *)data;
    int i;
    for (i = 0; i < n; i++) {
        while (x->tx_n == (int)sizeof x->tx) irq_check(x);
        x->tx[(x->tx_head + x->tx_n++) % (int)sizeof x->tx] = d[i];
    }
    if (x->tx_n && (ob_serial_in(&x->u, 5) & 0x20)) {
        ob_serial_out(&x->u, 0, x->tx[x->tx_head]);
        x->tx_head = (x->tx_head + 1) % (int)sizeof x->tx;
        x->tx_n--;
    }
    irq_check(x);
}
static void dsays(drv *x, const char *s) { dsend(x, s, (int)strlen(s)); }

static void pump(void)
{
    int i, k, rounds;
    for (rounds = 0; rounds < 2000; rounds++) {
        int moved = 0;
        for (k = 0; k < 2; k++) { ob_serial_poll(&D[k].u, now_ms); irq_check(&D[k]); }
        for (i = 0; i < qn; i++) { ob_serial_message(&D[Q[i].to].u, Q[i].d, Q[i].n); irq_check(&D[Q[i].to]); moved = 1; }
        qn = 0;
        for (k = 0; k < 2; k++) if (D[k].tx_n) moved = 1;
        if (!moved) break;
    }
}

static void wait_ms(uint32_t ms) { uint32_t end = now_ms + ms; while (now_ms < end) { now_ms += 20; pump(); } }

static int heard(drv *x, const char *s)
{
    int n = (int)strlen(s), i;
    for (i = 0; i + n <= x->ngot; i++) if (!memcmp(x->got + i, s, (size_t)n)) return 1;
    return 0;
}
static void forget(drv *x) { x->ngot = 0; }

/* B103C and B1121: 9600 8N1, the FIFO probe, DTR RTS OUT2, receive and THR-empty interrupts */
static int open_port(drv *x)
{
    int fifo;
    ob_serial_out(&x->u, 1, 0);
    ob_serial_out(&x->u, 4, 9);
    ob_serial_out(&x->u, 3, 0x83);
    ob_serial_out(&x->u, 0, 12);
    ob_serial_out(&x->u, 1, 0);
    ob_serial_out(&x->u, 3, ob_serial_in(&x->u, 3) & 0x1F);
    ob_serial_in(&x->u, 5); ob_serial_in(&x->u, 0); ob_serial_in(&x->u, 2); ob_serial_in(&x->u, 6);
    ob_serial_out(&x->u, 1, 3);
    ob_serial_out(&x->u, 2, 0x87);
    fifo = ob_serial_in(&x->u, 2) & 0x80;
    ob_serial_out(&x->u, 4, ob_serial_in(&x->u, 4) | 3 | 8);
    irq_check(x);
    return fifo;
}

static void modem(void)
{
    static uint8_t a[20000], b[20000];
    int i, same;
    memset(D, 0, sizeof D);
    ob_serial_init(&D[0].u, OB_SERIAL_MODEM, send0, NULL);
    ob_serial_init(&D[1].u, OB_SERIAL_MODEM, send1, NULL);
    ob_serial_peer(&D[0].u, 1);
    ob_serial_peer(&D[1].u, 1);
    CHECK(open_port(&D[0]) && open_port(&D[1]), "modem: a 16550 (FIFO probe)");
    dsays(&D[0], "ATE1\rATQ0\rATX4\r");
    pump();
    CHECK(heard(&D[0], "ATE1\r") && heard(&D[0], "\r\nOK\r\n"), "modem: commands echoed and answered OK");
    forget(&D[0]);
    dsays(&D[0], "+++");                       /* (96's sub_17AC7: +++ while offline, then AT on the same line) */
    wait_ms(200);
    dsays(&D[0], "AT\r");
    pump();
    CHECK(heard(&D[0], "\r\nOK\r\n") && !heard(&D[0], "ERROR"), "modem: +++ while offline, then AT: OK");
    forget(&D[0]);
    dsays(&D[0], "AT &F&KN0S37=9\r");
    pump();
    CHECK(heard(&D[0], "\r\nOK\r\n"), "modem: 96's init string: OK");
    dsays(&D[1], "ATZ\rATS0=1\r");
    pump();
    forget(&D[0]); forget(&D[1]);
    dsays(&D[0], "ATDT5551234\r");
    pump();
    CHECK(heard(&D[0], "CONNECT 9600") && heard(&D[1], "CONNECT 9600"), "modem: dialing reaches the other machine; both connect");
    CHECK((ob_serial_in(&D[0].u, 6) & 0x80) && (ob_serial_in(&D[1].u, 6) & 0x80), "modem: carrier on both");
    forget(&D[0]); forget(&D[1]);
    for (i = 0; i < (int)sizeof a; i++) { a[i] = (uint8_t)(i * 7 + (i >> 8)); b[i] = (uint8_t)(i % 3 ? '+' : i); }
    for (i = 0; i < (int)sizeof a; i += 500) { dsend(&D[0], a + i, 500); dsend(&D[1], b + i, 500); pump(); }
    pump();
    same = D[1].ngot == (int)sizeof a && !memcmp(D[1].got, a, sizeof a) && D[0].ngot == (int)sizeof b && !memcmp(D[0].got, b, sizeof b);
    CHECK(same, "modem: 20 kB each way, every byte (+++ among them did not escape)");
    if (!same) printf("  got %d and %d\n", D[1].ngot, D[0].ngot);
    /* escape: a second of quiet, +++, a second of quiet */
    forget(&D[0]); forget(&D[1]);
    wait_ms(1200);
    dsays(&D[0], "+++");
    wait_ms(1200);
    CHECK(heard(&D[0], "\r\nOK\r\n"), "modem: +++ with quiet around it: commands");
    CHECK(D[1].ngot == 3 && !memcmp(D[1].got, "+++", 3), "modem: the other side heard the three +");
    forget(&D[0]);
    dsays(&D[0], "ATO\r");
    pump();
    CHECK(heard(&D[0], "CONNECT"), "modem: ATO goes back on line");
    forget(&D[0]); forget(&D[1]);
    dsays(&D[0], "hello");
    pump();
    CHECK(D[1].ngot == 5 && !memcmp(D[1].got, "hello", 5), "modem: data again");
    /* DTR dropped (B1294): hung up */
    forget(&D[0]); forget(&D[1]);
    ob_serial_out(&D[0].u, 4, ob_serial_in(&D[0].u, 4) & ~1);
    pump();
    CHECK(heard(&D[1], "NO CARRIER") && !(ob_serial_in(&D[1].u, 6) & 0x80), "modem: DTR dropped: the other hears NO CARRIER");
    /* answering by hand: RING, then ATA */
    ob_serial_out(&D[0].u, 4, ob_serial_in(&D[0].u, 4) | 1);
    forget(&D[0]); forget(&D[1]);
    dsays(&D[1], "ATS0=0\r");
    dsays(&D[0], "ATD\r");
    pump();
    CHECK(heard(&D[1], "RING") && (ob_serial_in(&D[1].u, 6) & 0x40), "modem: no auto-answer: RING");
    dsays(&D[1], "ATA\r");
    pump();
    CHECK(heard(&D[0], "CONNECT") && heard(&D[1], "CONNECT"), "modem: ATA answers");
    /* the other machine leaves the room */
    forget(&D[1]);
    ob_serial_peer(&D[1].u, 0);
    irq_check(&D[1]);
    CHECK(heard(&D[1], "NO CARRIER"), "modem: the other machine left: NO CARRIER");
    printf("modem: AT commands, dial and answer, 20 kB each way, +++ and ATO, hang-ups: %s\n", fails ? "FAILED" : "ok");
}

static void cable(void)
{
    int f = fails;
    memset(D, 0, sizeof D);
    ob_serial_init(&D[0].u, OB_SERIAL_DIRECT, send0, NULL);
    ob_serial_init(&D[1].u, OB_SERIAL_DIRECT, send1, NULL);
    open_port(&D[0]); open_port(&D[1]);
    CHECK(!(ob_serial_in(&D[0].u, 6) & 0x80), "cable: nobody at the other end");
    ob_serial_peer(&D[0].u, 1);
    ob_serial_peer(&D[1].u, 1);
    CHECK((ob_serial_in(&D[0].u, 6) & 0xB0) == 0xB0, "cable: DCD, DSR, CTS with the other machine there");
    dsays(&D[0], "ATDT1\r");
    dsays(&D[1], "ok");
    pump();
    CHECK(D[1].ngot == 6 && !memcmp(D[1].got, "ATDT1\r", 6) && D[0].ngot == 2, "cable: bytes go straight through (no modem)");
    printf("cable: %s\n", fails == f ? "ok" : "FAILED");
}

int main(void)
{
    modem();
    cable();
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("serial ok\n");
    return 0;
}
