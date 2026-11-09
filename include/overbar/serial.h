/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A serial port for DOS games' modem and cable play, carried by the room (the DOS platform): an 8250/16550
 * UART at the register level, with a Hayes modem behind it (OB_SERIAL_MODEM) or a null-modem cable
 * (OB_SERIAL_DIRECT). The game's own serial driver and modem code run unchanged: its AT commands are
 * answered, dialing reaches the other machine of the room whatever the number, and once connected its
 * bytes go across the room's message lane (OB_NET_TYPE_SERIAL), which loses nothing, so the game's own
 * resending never has to work. Two machines: the port's room says when the other one is there
 * (ob_serial_peer).
 *
 * The port's part: the game's in/out on the port's 8 registers go to ob_serial_in / ob_serial_out (reg =
 * port & 7); after those and after ob_serial_message / ob_serial_poll, the interrupt line is
 * ob_serial_irq (raise the IRQ when it goes up: IRQ 4 for COM1/COM3, 3 for COM2/COM4); once a frame
 * ob_serial_poll with the time in ms (sends what was written, the +++ escape's guard time). No SDL. */
#ifndef OVERBAR_SERIAL_H
#define OVERBAR_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { OB_SERIAL_MODEM, OB_SERIAL_DIRECT };
#define OB_SERIAL_RX 8192               /* bytes come and not read yet */
#define OB_SERIAL_TX 1024               /* bytes written, sent at the next poll (or when full) */

enum {                                  /* ob_serial.call */
    OB_SERIAL_IDLE, OB_SERIAL_DIALING, OB_SERIAL_ANSWERING, OB_SERIAL_ONLINE
};

typedef struct ob_serial {
    int mode;
    /* the UART */
    uint8_t ier, lcr, mcr, fcr, scr, dll, dlm;
    uint8_t msr_delta;                  /* MSR bits 0..3 */
    uint8_t overrun;
    int thre_pending;                   /* the transmitter-empty interrupt, until IIR is read or THR written */
    uint8_t rx[OB_SERIAL_RX];
    int rx_head, rx_n;
    uint8_t tx[OB_SERIAL_TX];
    int tx_n;
    int irq_was;
    /* the line */
    int peer;                           /* the other machine is in the room */
    int call;                           /* OB_SERIAL_IDLE .. ONLINE */
    int ringing;                        /* (modem) the other machine is calling */
    int carrier;
    /* the modem */
    int echo, quiet, verbose;
    uint8_t s[16];                      /* S registers (S0 auto-answer rings, S2 escape character, S12 guard time) */
    char cmd[80];
    int ncmd;
    int plus;                           /* escape characters in a row, online */
    uint32_t now, last_data, plus_at;
    char connect[32];                   /* "CONNECT 9600" */
    /* to the other machine */
    void (*send)(void *ud, const void *data, size_t size);
    void *ud;
} ob_serial;

void ob_serial_init(ob_serial *p, int mode, void (*send)(void *ud, const void *data, size_t size), void *ud);
uint8_t ob_serial_in(ob_serial *p, int reg);
void ob_serial_out(ob_serial *p, int reg, uint8_t v);
/* the UART's interrupt line (OUT2 on and something to say) */
int ob_serial_irq(const ob_serial *p);
/* 1 when the line went up since the last call (the port raises the IRQ then) */
int ob_serial_irq_edge(ob_serial *p);
/* the other machine came into the room (1) or left it (0: the carrier drops) */
void ob_serial_peer(ob_serial *p, int present);
/* a message of type OB_NET_TYPE_SERIAL from the other machine */
void ob_serial_message(ob_serial *p, const void *data, size_t size);
/* once a frame: what was written goes; the time (ms, any start) for the +++ escape */
void ob_serial_poll(ob_serial *p, uint32_t now_ms);
/* one line for the room page ("Modem: connected"; "" nothing) */
void ob_serial_status(const ob_serial *p, char *line, int size);

#ifdef __cplusplus
}
#endif

#endif
