/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Frame generation for full motion video: the pictures between two of a movie's frames, made by a motion
 * network (RIFE 4.6, MIT, third_party/rife: its ncnn model files, read at run time) run on the CPU in plain C.
 * Made once from the user's own movie and kept on their machine; nothing derived is shipped.
 *
 * The network finds the motion on small pictures (w x h, any size: padded to 32 inside); the in-between is
 * then drawn at any size from the motion it found (ob_framegen_draw), so a 2x de-dithered movie gets 2x
 * in-betweens for the cost of a 1x network.
 *
 * Generic: a port gives it pairs of frames; it knows nothing of any game. Threads: SDL's.
 */
#ifndef OVERBAR_FRAMEGEN_H
#define OVERBAR_FRAMEGEN_H
#include <stdint.h>

typedef struct ob_framegen ob_framegen;

/* the model: dir holds flownet.param and flownet.bin (the rife-ncnn-vulkan v4.6 files). threads 0: all cores.
 * NULL when they are missing or not a model this reads */
ob_framegen *ob_framegen_load(const char *dir, int threads);
void ob_framegen_free(ob_framegen *g);

/* the motion between a and b (w x h ARGB) at time t (0 a .. 1 b): flow is 4 x w x h floats (pixels: a's dx, dy,
 * then b's), mask w x h (how much of a). 0 when it could not */
int ob_framegen_motion(ob_framegen *g, const uint32_t *a, const uint32_t *b, int w, int h, float t, float *flow, float *mask);

/* the in-between drawn from the motion found at w x h onto pictures A and B of W x H (any multiple: the motion is
 * scaled up), out W x H ARGB. g: its threads (NULL: the caller's alone) */
void ob_framegen_draw(ob_framegen *g, const uint32_t *A, const uint32_t *B, int W, int H, const float *flow, const float *mask, int w, int h,
                      uint32_t *out);

/* (tests) the network's own output for a and b at t, w x h ARGB, as its graph draws it */
int ob_framegen_run(ob_framegen *g, const uint32_t *a, const uint32_t *b, int w, int h, float t, uint32_t *out);
#endif
