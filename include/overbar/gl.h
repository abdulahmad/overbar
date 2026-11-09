/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The OpenGL shader presentation path of the desktop PC ports (src/gl.c, built with -DHAVE_GL): the frame as a texture drawn by a fragment
 * shader with sharp / cubic scaling, the CRT looks and the MiSTer filters. OpenGL 2.0 (compatibility). The window is made by the platform
 * layer with hooks.gl; if ob_gl_init fails the port falls back to SDL's renderer. OVERBAR_SHOT=file.ppm and OVERBAR_SHOT_AT=seconds save what
 * the window shows (a test aid). */
#ifndef OVERBAR_GL_H
#define OVERBAR_GL_H

#include <stdint.h>
#include <SDL.h>

extern const char *ob_gl_filter;               /* "nearest", "linear" or "cubic" (default), set before ob_gl_init */
int ob_gl_init(SDL_Window *win);
void ob_gl_draw(SDL_Window *win, const uint32_t *argb, int w, int h, int vx, int vy, int vw, int vh);
void ob_gl_quit(void);
void ob_gl_set_vsync(int on);
void ob_gl_set_look(int pre, float sigma, float gain, float center, int period, int style);
void ob_gl_set_target(int w, int h);
void ob_gl_mister_tables(const float horiz[64][4], const float vert[2][256][4], const float gamma[256]);
void ob_gl_mister_mask(const float *rgb, int w, int h);
void ob_gl_set_custom(float dark, float gamma, float mask, float sharp);
void ob_gl_set_bright(float b, float maskscale);
void ob_gl_osd_set(const char *l1, const char *l2, const char *l3);

#endif
