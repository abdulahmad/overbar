/* SPDX-License-Identifier: CC0-1.0
 * Dedicated to the public domain by abdulahmad: the sounds this file makes are pure arithmetic (sines under a short envelope), no samples
 * and no third-party material, and may be used in any product, commercial or not, without attribution. */
#ifndef OVERBAR_SFX_H
#define OVERBAR_SFX_H

enum { OB_SFX_MOVE, OB_SFX_SELECT, OB_SFX_BACK, OB_SFX_COUNT };

/* the sound at volume 0..100, on a device of its own (opened on first use; nothing happens when there is no audio); samples for tests */
void ob_sfx_play(int kind, int volume);
/* the synthesized sound as 16-bit mono 44.1 kHz samples (n of them) for tests and tools; the buffer is static */
const short *ob_sfx_samples(int kind, int *n);
void ob_sfx_close(void);
#endif
