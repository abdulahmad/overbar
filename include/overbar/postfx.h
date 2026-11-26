/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A frame post-process: a stage a port puts between its decoder and the screen, for full motion video whose
 * colours were dithered for the console (a checkerboard or stripes of two colours standing for one between
 * them). Generic: a port opts in per movie by running its frames through ob_postfx_run; nothing else changes.
 *
 *   off     the frame as it came, the same pointer (bit for bit the decoder's)
 *   live    each frame as it comes: the dither's patterns found and evened out, everything else left as it is
 *           (3 x 3, integers only, about 1 ms for 256 x 224 on a PC: a handheld's frame budget)
 *   cached  a slower, better pass made once over the whole movie from the user's own disc and kept on their
 *           machine (never shipped): 2x up, the patterns low-passed, non-local means, a light sharpen, and
 *           smoothing over time. Until it is made, live.
 *
 * Integers only, so every machine makes the same pixels. No SDL in here.
 */
#ifndef OVERBAR_POSTFX_H
#define OVERBAR_POSTFX_H
#include <stdint.h>

typedef enum { OB_POSTFX_OFF, OB_POSTFX_LIVE, OB_POSTFX_CACHED, OB_POSTFX_MODES } ob_postfx_mode;
extern const char *const ob_postfx_names[OB_POSTFX_MODES];   /* "off", "live", "cached" (an ini's or a menu's) */

/* live, by itself: in to out (w x h ARGB, not the same buffer), scratch w * h bytes */
void ob_dedither_live(const uint32_t *in, uint32_t *out, uint8_t *scratch, int w, int h);

/* the cached quality, frame by frame (it smooths over time, so give it a movie's frames in order) */
typedef struct ob_dedither_hq ob_dedither_hq;
ob_dedither_hq *ob_dedither_hq_new(int w, int h);
const uint32_t *ob_dedither_hq_frame(ob_dedither_hq *q, const uint32_t *in);   /* 2w x 2h ARGB, the object's own */
void ob_dedither_hq_free(ob_dedither_hq *q);

/* the cache: a file of a movie's cached frames (2w x 2h). source is the port's id for the movie (a hash of its
 * file, say): a file made from another source, or another size, is not read */
typedef struct ob_ddcache ob_ddcache;
ob_ddcache *ob_ddcache_open(const char *path, int w, int h, uint32_t source);  /* NULL: none (yet) */
int ob_ddcache_frames(const ob_ddcache *c);
const uint32_t *ob_ddcache_read(ob_ddcache *c, int index);   /* frame index (any order, in order is quickest); NULL past the end */
void ob_ddcache_close(ob_ddcache *c);

/* making it: each frame of the movie in order, then end (ok 0 throws it away). It is written beside path and
 * put in place at the end, so a cache is never half made */
typedef struct ob_ddcache_maker ob_ddcache_maker;
ob_ddcache_maker *ob_ddcache_make(const char *path, int w, int h, uint32_t source);
int ob_ddcache_add(ob_ddcache_maker *m, const uint32_t *frame);
int ob_ddcache_end(ob_ddcache_maker *m, int ok);
/* the same cache from frames already through ob_dedither_hq (2w x 2h each: a maker that has them anyway) */
ob_ddcache_maker *ob_ddcache_make_done(const char *path, int w, int h, uint32_t source);

/* a file of any frames as they are (w x h each), in the same code: made with ob_frames_make and ob_ddcache_add /
 * ob_ddcache_end, read with ob_ddcache_read / ob_ddcache_frames / ob_ddcache_close */
ob_ddcache *ob_frames_open(const char *path, int w, int h, uint32_t source);
ob_ddcache_maker *ob_frames_make(const char *path, int w, int h, uint32_t source);

/* a movie played while its file is being made: the frame ob_ddcache_add just put in, as the file has it (*bytes the
 * maker's own until its next add or end: copy them), and such bytes back into a w x h frame (the file's size: 2x the
 * movie's for a de-dither cache). 0: none / it doesn't read */
long ob_ddcache_last(const ob_ddcache_maker *m, const uint8_t **bytes);
int ob_ddcache_decode(const uint8_t *bytes, long len, int w, int h, uint32_t *out);

/* the stage: a movie's w x h frames in, the picture to show out (*ow x *oh: w x h, or 2w x 2h from the cache).
 * cache_path may be NULL (no cached mode: it plays live) */
typedef struct ob_postfx ob_postfx;
ob_postfx *ob_postfx_new(int w, int h, const char *cache_path, uint32_t source);
const uint32_t *ob_postfx_run(ob_postfx *p, int mode, int index, const uint32_t *in, int *ow, int *oh);
int ob_postfx_has_cache(ob_postfx *p);          /* (looks again for a newly made one) */
void ob_postfx_free(ob_postfx *p);

uint32_t ob_postfx_hash(const uint8_t *data, long n);   /* a source id: FNV-1a over the bytes */
#endif
