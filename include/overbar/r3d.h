/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * r3d: the box module's small software 3D renderer.
 * A scene is a tree of nodes (position, Euler XYZ rotation, scale) with triangle meshes, Lambert / Phong / unlit materials and
 * mipmapped textures, drawn with a z-buffer into an ARGB8888 buffer. Built for a handful of objects at window size, no GPU needed,
 * so it runs the same on every port and on handhelds. Units and conventions follow three.js (the mockup the scenes came from). */
#ifndef BOX_OVERBAR_R3D_H
#define BOX_OVERBAR_R3D_H
#include <stdint.h>

#define R3_NAME 48

typedef struct r3_tex {
    int w, h, nlv;
    uint32_t *lv[14];          /* ARGB, level 0 = the picture */
    int lw[14], lh[14];
    int wrap_s, wrap_t, flip_y;
    float off[2], rep[2];
    int alpha;                 /* has pixels that are not opaque */
    char key[64];              /* the art key (a scan, loaded from the art folder) or the file */
    int loaded;
} r3_tex;

typedef struct r3_mat {
    char type;                 /* 'B' unlit, 'L' Lambert, 'P' Phong */
    float col[3];              /* linear */
    int map;                   /* texture, -1 none */
    float opacity, alpha_test;
    int transparent, side;     /* side 0 front, 1 back, 2 both */
    int depth_write, visible, poff;
    float spec[3], shin, emis[3];
    float bright;              /* 1, or more while highlighted */
} r3_mat;

typedef struct r3_node {
    int parent;
    char name[R3_NAME], id[24], label[96];
    float pos[3], rot[3], scale[3];
    int vis;
    float on_x, off_x;
    float world[16];           /* row major */
    unsigned stamp;
    float hl;                  /* highlight (brightness factor, 1 = none) */
    float opacity;             /* multiplies every material under it (1) */
} r3_node;

typedef struct r3_mesh {
    int node, mat, ro;
    int nv, ni;
    float *v;                  /* pos3 normal3 uv2, indexed */
    uint32_t *idx;
    int mat_override;          /* -1, or a material drawn instead */
} r3_mesh;

typedef struct r3_light {
    char kind;                 /* A ambient, H hemisphere, D directional */
    float col[3], I, ground[3], dir[3];
} r3_light;

typedef struct r3_scene {
    r3_node *nodes; int nnodes;
    r3_mesh *meshes; int nmeshes;
    r3_mat *mats; int nmats, cap_mats;
    r3_tex *texs; int ntexs, cap_texs;
    r3_light lights[8]; int nlights;
    float fov, znear, zfar;
    float cam[3], look[3];     /* camera position and the point it looks at */
    unsigned stamp;
    char dir[1024];            /* where scene.txt was */
} r3_scene;

/* a frame to draw into (the caller's) with its depth and pick buffers */
typedef struct r3_target {
    uint32_t *px; int w, h, pitch;
    float *z; int16_t *pick; int cap;
} r3_target;

/* scene.txt + scene.vtx in dir; art(key, &w, &h) gives a scan's pixels (ARGB, malloc'd, caller frees) or NULL */
typedef uint32_t *(*r3_art_fn)(const char *key, int *w, int *h, void *ud);
uint32_t *r3_load_image(const char *path, int *w, int *h);   /* a picture file as 0xAARRGGBB (malloc), NULL when unreadable */
int r3_load(r3_scene *s, const char *dir, r3_art_fn art, void *ud);
void r3_free(r3_scene *s);
int r3_find(const r3_scene *s, const char *name);           /* node index or -1 */
int r3_find_id(const r3_scene *s, const char *id);
void r3_tex_set(r3_tex *t, uint32_t *argb, int w, int h);    /* takes the pixels; builds the mip levels */
int r3_tex_add(r3_scene *s, uint32_t *argb, int w, int h);   /* a new texture (pixels taken) */
int r3_mat_add(r3_scene *s, const r3_mat *m);

void r3_update(r3_scene *s);                                 /* world matrices */
const float *r3_world(r3_scene *s, int node);
void r3_world_pos(r3_scene *s, int node, float out[3]);
/* move a node under another parent (-1: the scene root) keeping where it is on screen (three.js attach) */
void r3_attach(r3_scene *s, int node, int parent);
int r3_visible(const r3_scene *s, int node);                  /* it and every parent visible */
int r3_is_under(const r3_scene *s, int node, int ancestor);

/* draw; pick[] gets the node of each pixel's mesh (-1: background). bg: ARGB background to clear to (NULL: leave px) */
void r3_render(r3_scene *s, r3_target *t, const uint32_t *bg);
/* project a world point; returns 0 if behind the camera. x, y in target pixels */
int r3_project(r3_scene *s, const r3_target *t, const float p[3], float *x, float *y);
/* the screen box of a node and everything under it; 0 if nothing visible */
int r3_screen_box(r3_scene *s, const r3_target *t, int node, float *x0, float *y0, float *x1, float *y1);

/* small math */
void r3_m4_mul(float *o, const float *a, const float *b);
void r3_m4_inv(float *o, const float *m);
void r3_m4_compose(float *o, const float p[3], const float r[3], const float s[3]);
void r3_m4_decompose(const float *m, float p[3], float r[3], float s[3]);
#endif
