/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * r3gl: the box's 3D picture drawn by the GPU. The same scene, the same lighting (per vertex, as r3d.c does it), the same
 * colours (linear light, sRGB out, blending in sRGB like the software path) and the same draw order, but at the window's full
 * size with 4x multisampling, in about a millisecond, then read back into the caller's buffer so the box composes its 2D on
 * top exactly as before. It has its own OpenGL 3.3 context on a small hidden window (never shown), so it works the same under
 * every port's presentation (its own GL, SDL's Direct3D renderer, anything) and puts the port's context back when done.
 * No GL headers or libraries are needed at build time: every function comes from SDL_GL_GetProcAddress. When there is no
 * OpenGL 3.3 (handhelds, a broken driver) r3gl_render returns 0 and the box draws in software as before. OB_BOX_GPU=0 turns it off. */
#include "overbar/r3d.h"
#ifdef OB_SDL_BASE                 /* (the SDL client build only, like box.c) */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define GLAPI_ __stdcall
/* laptops with two GPUs: ask for the fast one (read by the NVIDIA and AMD drivers from the exe's exports) */
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#else
#define GLAPI_
#endif

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef intptr_t GLsizeiptr, GLintptr;

#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_MULTISAMPLE 0x809D
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_INT 0x1404
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_INT 0x1405
#define GL_RED_INTEGER 0x8D94
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_R32I 0x8235
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_SAMPLES 0x8D57
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_COLOR 0x1800

#define GLFNS \
    F(const unsigned char *, GetString, (GLenum)) \
    F(GLenum, GetError, (void)) \
    F(void, GetIntegerv, (GLenum, GLint *)) \
    F(void, GetFloatv, (GLenum, GLfloat *)) \
    F(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    F(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    F(void, Clear, (GLbitfield)) \
    F(void, Enable, (GLenum)) \
    F(void, Disable, (GLenum)) \
    F(void, DepthMask, (GLboolean)) \
    F(void, DepthFunc, (GLenum)) \
    F(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    F(void, CullFace, (GLenum)) \
    F(void, FrontFace, (GLenum)) \
    F(void, PolygonOffset, (GLfloat, GLfloat)) \
    F(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    F(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    F(void, PixelStorei, (GLenum, GLint)) \
    F(void, GenTextures, (GLsizei, GLuint *)) \
    F(void, DeleteTextures, (GLsizei, const GLuint *)) \
    F(void, BindTexture, (GLenum, GLuint)) \
    F(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    F(void, TexParameteri, (GLenum, GLenum, GLint)) \
    F(void, TexParameterf, (GLenum, GLenum, GLfloat)) \
    F(void, GenerateMipmap, (GLenum)) \
    F(void, ActiveTexture, (GLenum)) \
    F(void, GenBuffers, (GLsizei, GLuint *)) \
    F(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    F(void, BindBuffer, (GLenum, GLuint)) \
    F(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    F(void, GenVertexArrays, (GLsizei, GLuint *)) \
    F(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
    F(void, BindVertexArray, (GLuint)) \
    F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    F(void, EnableVertexAttribArray, (GLuint)) \
    F(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
    F(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    F(GLuint, CreateShader, (GLenum)) \
    F(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    F(void, CompileShader, (GLuint)) \
    F(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    F(GLuint, CreateProgram, (void)) \
    F(void, AttachShader, (GLuint, GLuint)) \
    F(void, LinkProgram, (GLuint)) \
    F(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    F(void, UseProgram, (GLuint)) \
    F(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    F(void, Uniform1i, (GLint, GLint)) \
    F(void, Uniform1f, (GLint, GLfloat)) \
    F(void, Uniform3fv, (GLint, GLsizei, const GLfloat *)) \
    F(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    F(void, Uniform1fv, (GLint, GLsizei, const GLfloat *)) \
    F(void, Uniform1iv, (GLint, GLsizei, const GLint *)) \
    F(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    F(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    F(void, GenFramebuffers, (GLsizei, GLuint *)) \
    F(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    F(void, BindFramebuffer, (GLenum, GLuint)) \
    F(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    F(GLenum, CheckFramebufferStatus, (GLenum)) \
    F(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    F(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    F(void, BindRenderbuffer, (GLenum, GLuint)) \
    F(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    F(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    F(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    F(void, ReadBuffer, (GLenum)) \
    F(void, ClearBufferiv, (GLenum, GLint, const GLint *))

#define F(ret, name, args) typedef ret (GLAPI_ *pfn_##name) args; static pfn_##name gl##name;
GLFNS
#undef F

static struct {
    int state;                 /* 0 not tried, 1 ready, -1 no GPU */
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint prog, pprog, bprog, vao0;
    GLuint fbo_ms, rb_col, rb_dep, fbo_rs, rb_rs, fbo_pk, rb_pk, rb_pkd;
    int fw, fh, samples, maxtex;
    float aniso;
    GLuint bgtex; const uint32_t *bgp; int bgw, bgh;
    /* per mesh / texture of the scene */
    const r3_scene *scene;
    struct gmesh { const float *v; const uint32_t *idx; int nv, ni; GLuint vao, vbo, ibo; } *gm; int ngm;
    struct gtex { unsigned serial; const uint32_t *p; GLuint id; } *gt; int ngt;
    char renderer[96];
    float ms;
} G;

/* uniform locations: the main program [0] and the pick program [1] */
enum { U_MV, U_P, U_W, U_N, U_CAM, U_SUB, U_NL, U_LK, U_LC, U_LG, U_LD, U_LI, U_AMB, U_BASE, U_EMIS, U_SPEC, U_SHIN, U_MTYPE, U_SIDE,
       U_TEX, U_HASTEX, U_UVX, U_FLIPY, U_OPA, U_AT, U_BR, U_BLEND, U_NODE, U_COUNT };
static const char *const unames[U_COUNT] = { "MV", "P", "W", "N", "cam", "sub", "nl", "lk", "lc", "lg", "ld", "li", "amb", "base", "emis",
    "spec", "shin", "mtype", "side", "tex", "hastex", "uvx", "flipy", "opa", "at", "br", "blend", "node" };
static GLint U[2][U_COUNT];

static const char *vs_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 ap;\n"
    "layout(location = 1) in vec3 an;\n"
    "layout(location = 2) in vec2 at_;\n"
    "uniform mat4 MV, P, W;\n"
    "uniform mat3 N;\n"
    "uniform vec3 cam;\n"
    "uniform vec4 sub;\n"
    "uniform int nl;\n"
    "uniform int lk[8];\n"
    "uniform vec3 lc[8], lg[8], ld[8];\n"
    "uniform float li[8];\n"
    "uniform vec3 amb, base, emis, spec;\n"
    "uniform float shin;\n"
    "uniform int mtype, side;\n"
    "out vec2 uv;\n"
    "out vec3 dif, spc;\n"
    "void main() {\n"
    "  vec4 c = P * (MV * vec4(ap, 1.0));\n"
    "  c.xy = (c.xy - sub.zw * c.w) * sub.xy;\n"
    "  gl_Position = c;\n"
    "  uv = at_;\n"
    "  vec3 L = vec3(1.0), S = vec3(0.0);\n"
    "  if (mtype != 0) {\n"
    "    vec3 n = N * an; float q = length(n); if (q > 0.0) n /= q;\n"
    "    vec3 wp = (W * vec4(ap, 1.0)).xyz; vec3 vv = cam - wp; q = length(vv); if (q > 0.0) vv /= q;\n"
    "    if (side != 0 && dot(n, vv) < 0.0) n = -n;\n"
    "    L = amb;\n"
    "    for (int i = 0; i < nl; i++) {\n"
    "      if (lk[i] == 1) { float k = 0.5 * n.y + 0.5; L += (lg[i] + (lc[i] - lg[i]) * k) * li[i]; }\n"
    "      else if (lk[i] == 2) {\n"
    "        float d = dot(n, ld[i]);\n"
    "        if (d <= 0.0) continue;\n"
    "        L += lc[i] * li[i] * d;\n"
    "        if (mtype == 2 && shin > 0.0) {\n"
    "          vec3 h = ld[i] + vv; q = length(h); if (q > 0.0) h /= q;\n"
    "          float nh = dot(n, h);\n"
    "          if (nh > 0.0) {\n"
    "            float vh = dot(ld[i], h);\n"
    "            float fr = pow(1.0 - max(vh, 0.0), 5.0);\n"
    "            float D = 0.25 * (0.5 * shin + 1.0) * pow(nh, shin) * d * li[i];\n"
    "            S += (spec + (vec3(1.0) - spec) * fr) * D * lc[i];\n"
    "          }\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  dif = L * base + emis; spc = S;\n"
    "}\n";

#define FS_COMMON \
    "#version 330 core\n" \
    "in vec2 uv;\n" \
    "in vec3 dif, spc;\n" \
    "uniform sampler2D tex;\n" \
    "uniform int hastex, flipy, blend, node;\n" \
    "uniform vec4 uvx;\n" \
    "uniform float opa, at, br;\n" \
    "float s2l(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }\n" \
    "float l2s(float c) { c = clamp(c, 0.0, 1.0); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }\n" \
    "vec4 base_col() {\n" \
    "  vec4 c = vec4(1.0);\n" \
    "  if (hastex != 0) { vec2 t = uv * uvx.xy + uvx.zw; if (flipy != 0) t.y = 1.0 - t.y; vec4 s = texture(tex, t, -0.25); c = vec4(s2l(s.r), s2l(s.g), s2l(s.b), s.a); }\n" \
    "  c.a *= opa;\n" \
    "  if (at > 0.0 && c.a < at) discard;\n" \
    "  if (blend != 0 && c.a <= 0.002) discard;\n" \
    "  return c;\n" \
    "}\n"

static const char *fs_src =
    FS_COMMON
    "layout(location = 0) out vec4 o;\n"
    "void main() {\n"
    "  vec4 c = base_col();\n"
    "  vec3 f = (c.rgb * dif + spc) * br;\n"
    "  o = vec4(l2s(f.r), l2s(f.g), l2s(f.b), blend != 0 ? c.a : 1.0);\n"
    "}\n";

static const char *fs_pick =
    FS_COMMON
    "layout(location = 0) out int o;\n"
    "void main() { base_col(); o = node + 1; }\n";

/* the background: a picture over the whole frame */
static const char *vs_bg =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() { vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1); uv = vec2(p.x, p.y); gl_Position = vec4(p.x * 2.0 - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char *fs_bg =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "uniform sampler2D tex;\n"
    "layout(location = 0) out vec4 o;\n"
    "void main() { o = vec4(texture(tex, uv).rgb, 1.0); }\n";

static GLuint shader(GLenum kind, const char *src)
{
    GLuint s = glCreateShader(kind);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024] = ""; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "overbar box gpu: shader: %s\n", log); return 0; }
    return s;
}
static GLuint program(const char *vs, const char *fs)
{
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs), p;
    GLint ok = 0;
    if (!v || !f) return 0;
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024] = ""; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "overbar box gpu: link: %s\n", log); return 0; }
    return p;
}

/* the port's context, put back after each use of ours */
static SDL_Window *saved_win;
static SDL_GLContext saved_ctx;
static int ctx_enter(void)
{
    saved_win = SDL_GL_GetCurrentWindow();
    saved_ctx = SDL_GL_GetCurrentContext();
    if (saved_ctx == G.ctx) return 1;
    return SDL_GL_MakeCurrent(G.win, G.ctx) == 0;
}
static void ctx_leave(void)
{
    if (saved_ctx && saved_ctx != G.ctx) SDL_GL_MakeCurrent(saved_win, saved_ctx);
}

static int init(void)
{
    const char *e = getenv("OB_BOX_GPU");
    if (e && e[0] == '0') return 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return 0;
    /* SDL's GL attributes are global: keep the port's and put them back */
    static const SDL_GLattr keep[] = { SDL_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION, SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_FLAGS,
                                       SDL_GL_DOUBLEBUFFER, SDL_GL_DEPTH_SIZE, SDL_GL_MULTISAMPLEBUFFERS, SDL_GL_MULTISAMPLESAMPLES };
    int old[sizeof keep / sizeof keep[0]];
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) if (SDL_GL_GetAttribute(keep[i], &old[i])) old[i] = -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    saved_win = SDL_GL_GetCurrentWindow();
    saved_ctx = SDL_GL_GetCurrentContext();
    G.win = SDL_CreateWindow("overbar box", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_SKIP_TASKBAR);
    if (G.win) G.ctx = SDL_GL_CreateContext(G.win);
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) if (old[i] >= 0) SDL_GL_SetAttribute(keep[i], old[i]);
    if (!G.ctx) {
        fprintf(stderr, "overbar box gpu: no OpenGL 3.3 (%s), drawing in software\n", SDL_GetError());
        if (G.win) SDL_DestroyWindow(G.win);
        G.win = NULL;
        if (saved_ctx) SDL_GL_MakeCurrent(saved_win, saved_ctx);
        return 0;
    }
    int ok = 1;
#define F(ret, name, args) if (!(gl##name = (pfn_##name)SDL_GL_GetProcAddress("gl" #name))) ok = 0;
    GLFNS
#undef F
    if (ok) {
        G.prog = program(vs_src, fs_src);
        G.pprog = program(vs_src, fs_pick);
        G.bprog = program(vs_bg, fs_bg);
        ok = G.prog && G.pprog && G.bprog;
    }
    if (ok) {
        for (int p = 0; p < 2; p++) for (int u = 0; u < U_COUNT; u++) U[p][u] = glGetUniformLocation(p ? G.pprog : G.prog, unames[u]);
        glGenVertexArrays(1, &G.vao0);
        GLint ms = 0; glGetIntegerv(GL_MAX_SAMPLES, &ms);
        G.samples = ms >= 4 ? 4 : ms >= 2 ? 2 : 0;
        const char *se = getenv("OB_BOX_MSAA");
        if (se) { int n = atoi(se); G.samples = n < 0 ? 0 : n > ms ? ms : n; }
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &G.maxtex);
        if (G.maxtex < 1024) G.maxtex = 1024;
        while (glGetError() != GL_NO_ERROR) {}
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &G.aniso);       /* (an error and 0 when the driver has no anisotropic filtering) */
        while (glGetError() != GL_NO_ERROR) {}
        if (G.aniso > 8) G.aniso = 8;
        const char *r = (const char *)glGetString(GL_RENDERER);
        snprintf(G.renderer, sizeof G.renderer, "%s", r ? r : "?");
        fprintf(stderr, "overbar box gpu: %s, %dx multisampling\n", G.renderer, G.samples);
    } else fprintf(stderr, "overbar box gpu: OpenGL 3.3 functions or shaders missing, drawing in software\n");
    if (saved_ctx) SDL_GL_MakeCurrent(saved_win, saved_ctx);
    if (!ok) { SDL_GL_DeleteContext(G.ctx); SDL_DestroyWindow(G.win); G.ctx = NULL; G.win = NULL; }
    return ok;
}

static int frames(int w, int h)
{
    if (G.fw == w && G.fh == h && G.fbo_rs) return 1;
    if (G.fbo_ms) { glDeleteFramebuffers(1, &G.fbo_ms); glDeleteRenderbuffers(1, &G.rb_col); glDeleteRenderbuffers(1, &G.rb_dep); }
    if (G.fbo_rs) { glDeleteFramebuffers(1, &G.fbo_rs); glDeleteRenderbuffers(1, &G.rb_rs); }
    G.fbo_ms = G.fbo_rs = 0;
    /* the multisampled frame (when there is multisampling) and the plain one it resolves into, which is read back */
    glGenFramebuffers(1, &G.fbo_rs);
    glGenRenderbuffers(1, &G.rb_rs);
    glBindRenderbuffer(GL_RENDERBUFFER, G.rb_rs);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_rs);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, G.rb_rs);
    glGenRenderbuffers(1, &G.rb_dep);
    glBindRenderbuffer(GL_RENDERBUFFER, G.rb_dep);
    if (G.samples) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, G.samples, GL_DEPTH_COMPONENT24, w, h);
        glGenFramebuffers(1, &G.fbo_ms);
        glGenRenderbuffers(1, &G.rb_col);
        glBindRenderbuffer(GL_RENDERBUFFER, G.rb_col);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, G.samples, GL_RGBA8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_ms);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, G.rb_col);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, G.rb_dep);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_rs);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, G.rb_dep);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_rs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    if (!G.fbo_pk) {                             /* the pick: one pixel (a node number) with its depth */
        glGenFramebuffers(1, &G.fbo_pk);
        glGenRenderbuffers(1, &G.rb_pk);
        glGenRenderbuffers(1, &G.rb_pkd);
        glBindRenderbuffer(GL_RENDERBUFFER, G.rb_pk);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_R32I, 1, 1);
        glBindRenderbuffer(GL_RENDERBUFFER, G.rb_pkd);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 1, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_pk);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, G.rb_pk);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, G.rb_pkd);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    }
    G.fw = w; G.fh = h;
    return 1;
}

static void forget_scene(void)
{
    for (int i = 0; i < G.ngm; i++) if (G.gm[i].vao) { glDeleteVertexArrays(1, &G.gm[i].vao); glDeleteBuffers(1, &G.gm[i].vbo); glDeleteBuffers(1, &G.gm[i].ibo); }
    for (int i = 0; i < G.ngt; i++) if (G.gt[i].id) glDeleteTextures(1, &G.gt[i].id);
    free(G.gm); free(G.gt);
    G.gm = NULL; G.gt = NULL; G.ngm = G.ngt = 0;
}

static struct gmesh *mesh_of(const r3_scene *s, int i)
{
    if (i >= G.ngm) {
        struct gmesh *n = (struct gmesh *)realloc(G.gm, (size_t)s->nmeshes * sizeof *n);
        if (!n) return NULL;
        memset(n + G.ngm, 0, (size_t)(s->nmeshes - G.ngm) * sizeof *n);
        G.gm = n; G.ngm = s->nmeshes;
    }
    struct gmesh *g = &G.gm[i];
    const r3_mesh *m = &s->meshes[i];
    if (g->vao && g->v == m->v && g->idx == m->idx && g->nv == m->nv && g->ni == m->ni) return g;
    if (!g->vao) { glGenVertexArrays(1, &g->vao); glGenBuffers(1, &g->vbo); glGenBuffers(1, &g->ibo); }
    glBindVertexArray(g->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->nv * 8 * sizeof(float), m->v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m->ni * sizeof(uint32_t), m->idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 32, (const void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, 0, 32, (const void *)12);
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, 32, (const void *)24);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    g->v = m->v; g->idx = m->idx; g->nv = m->nv; g->ni = m->ni;
    return g;
}

static GLuint tex_of(const r3_scene *s, int i)
{
    if (i < 0 || i >= s->ntexs || !s->texs[i].loaded || !s->texs[i].nlv) return 0;
    if (i >= G.ngt) {
        struct gtex *n = (struct gtex *)realloc(G.gt, (size_t)s->ntexs * sizeof *n);
        if (!n) return 0;
        memset(n + G.ngt, 0, (size_t)(s->ntexs - G.ngt) * sizeof *n);
        G.gt = n; G.ngt = s->ntexs;
    }
    struct gtex *g = &G.gt[i];
    const r3_tex *t = &s->texs[i];
    if (g->id && g->serial == t->serial && g->p == t->lv[0]) return g->id;
    if (!g->id) glGenTextures(1, &g->id);
    glBindTexture(GL_TEXTURE_2D, g->id);
    int lv = 0;                                  /* a scan bigger than the GPU takes: its first mip level that fits */
    while (lv + 1 < t->nlv && (t->lw[lv] > G.maxtex || t->lh[lv] > G.maxtex)) lv++;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->lw[lv], t->lh[lv], 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, t->lv[lv]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, t->wrap_s ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t->wrap_t ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (G.aniso > 1) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, G.aniso);
    g->serial = t->serial; g->p = t->lv[0];
    return g->id;
}

/* the camera as r3d.c makes it: view matrix (row major) and the projection to the frame, y down so rows read back top first */
static void camera(const r3_scene *s, int w, int h, float V[16], float P[16])
{
    float f[3] = { s->cam[0] - s->look[0], s->cam[1] - s->look[1], s->cam[2] - s->look[2] };
    float L = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (L < 1e-6f) { f[0] = 0; f[1] = 0; f[2] = 1; L = 1; }
    for (int i = 0; i < 3; i++) f[i] /= L;
    float up[3] = { 0, 1, 0 };
    float x[3] = { up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0] };
    L = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); if (L < 1e-6f) { x[0] = 1; x[1] = 0; x[2] = 0; L = 1; }
    for (int i = 0; i < 3; i++) x[i] /= L;
    float y[3] = { f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0] };
    V[0] = x[0]; V[1] = x[1]; V[2] = x[2]; V[3] = -(x[0] * s->cam[0] + x[1] * s->cam[1] + x[2] * s->cam[2]);
    V[4] = y[0]; V[5] = y[1]; V[6] = y[2]; V[7] = -(y[0] * s->cam[0] + y[1] * s->cam[1] + y[2] * s->cam[2]);
    V[8] = f[0]; V[9] = f[1]; V[10] = f[2]; V[11] = -(f[0] * s->cam[0] + f[1] * s->cam[1] + f[2] * s->cam[2]);
    V[12] = V[13] = V[14] = 0; V[15] = 1;
    float aspect = (float)w / (h > 0 ? h : 1);
    float th = tanf(s->fov * 0.5f * 3.14159265f / 180.0f);
    float tw = th * (16.0f / 9.0f);
    if (aspect >= 16.0f / 9.0f) tw = th * aspect; else th = tw / aspect;
    float n = s->znear > 0 ? s->znear : 0.1f, fa = s->zfar > n ? s->zfar * 4 : n * 4000;
    memset(P, 0, 16 * sizeof(float));
    P[0] = 1 / tw;
    P[5] = -1 / th;                               /* row 0 of the frame is the top of the picture */
    P[10] = -(fa + n) / (fa - n); P[11] = -2 * fa * n / (fa - n);
    P[14] = -1;
}

static float det3(const float *m) { return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]); }

typedef struct { int mesh; float depth; } item;
static int by_depth(const void *a, const void *b) { float d = ((const item *)a)->depth - ((const item *)b)->depth; return d < 0 ? -1 : d > 0; }

/* every visible mesh in r3d.c's order (opaque near to far by render order, then the see-through ones far to near) */
static int order(r3_scene *s, const float V[16], item **out)
{
    static item *it; static int cap;
    if (cap < s->nmeshes) { cap = s->nmeshes + 64; free(it); it = (item *)malloc((size_t)cap * sizeof *it); if (!it) { cap = 0; return 0; } }
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        int first = n;
        for (int i = 0; i < s->nmeshes; i++) {
            r3_mesh *me = &s->meshes[i];
            const r3_mat *m = &s->mats[me->mat_override >= 0 ? me->mat_override : me->mat];
            if (!m->visible || !r3_visible(s, me->node)) continue;
            float opa = m->opacity;
            for (int k = me->node; k >= 0; k = s->nodes[k].parent) opa *= s->nodes[k].opacity;
            if (opa <= 0.004f) continue;
            int tr = m->transparent || opa < 0.999f;
            if (tr && m->alpha_test > 0 && opa >= 0.999f) tr = 0;
            if (tr != pass) continue;
            const float *W = s->nodes[me->node].world;
            float vz = V[8] * W[3] + V[9] * W[7] + V[10] * W[11] + V[11];
            it[n].mesh = i;
            it[n].depth = pass ? vz + me->ro * 1000.0f : me->ro * 1000.0f - vz;
            n++;
        }
        qsort(it + first, (size_t)(n - first), sizeof *it, by_depth);
    }
    *out = it;
    return n;
}

static void lights(r3_scene *s, int p)
{
    float amb[3] = { 0, 0, 0 }, lc[24], lg[24], ld[24], li[8];
    int lk[8], nl = 0;
    for (int l = 0; l < s->nlights && l < 8; l++) {
        const r3_light *L = &s->lights[l];
        if (L->kind == 'A') { for (int k = 0; k < 3; k++) amb[k] += L->col[k] * L->I; continue; }
        if (L->kind != 'H' && L->kind != 'D') continue;
        lk[nl] = L->kind == 'H' ? 1 : 2;
        memcpy(lc + nl * 3, L->col, 12); memcpy(lg + nl * 3, L->ground, 12); memcpy(ld + nl * 3, L->dir, 12);
        li[nl] = L->I;
        nl++;
    }
    glUniform3fv(U[p][U_AMB], 1, amb);
    glUniform1i(U[p][U_NL], nl);
    if (nl) {
        glUniform1iv(U[p][U_LK], nl, lk);
        glUniform3fv(U[p][U_LC], nl, lc); glUniform3fv(U[p][U_LG], nl, lg); glUniform3fv(U[p][U_LD], nl, ld);
        glUniform1fv(U[p][U_LI], nl, li);
    }
    glUniform3fv(U[p][U_CAM], 1, s->cam);
}

/* one mesh's draw call with its state; p 0 the picture, 1 the pick */
static void draw_one(r3_scene *s, int mi, const float V[16], int p)
{
    r3_mesh *me = &s->meshes[mi];
    const r3_mat *m = &s->mats[me->mat_override >= 0 ? me->mat_override : me->mat];
    const float *W = s->nodes[me->node].world;
    float opa = m->opacity, bright = m->bright;
    for (int k = me->node; k >= 0; k = s->nodes[k].parent) { opa *= s->nodes[k].opacity; bright *= s->nodes[k].hl; }
    int blend = m->transparent || opa < 0.999f, zw = m->depth_write && !(opa < 0.5f);
    if (p && !zw) return;                         /* (nothing that leaves no depth is picked) */
    struct gmesh *g = mesh_of(s, mi);
    if (!g || me->ni < 3) return;
    float MV[16]; r3_m4_mul(MV, V, W);
    float Wi[16]; r3_m4_inv(Wi, W);
    float N[9] = { Wi[0], Wi[4], Wi[8], Wi[1], Wi[5], Wi[9], Wi[2], Wi[6], Wi[10] };
    int mirror = det3(W) < 0;
    glUniformMatrix4fv(U[p][U_MV], 1, 1, MV);
    glUniformMatrix4fv(U[p][U_W], 1, 1, W);
    glUniformMatrix3fv(U[p][U_N], 1, 1, N);
    GLuint tx = m->map >= 0 ? tex_of(s, m->map) : 0;
    float base[3] = { m->col[0], m->col[1], m->col[2] };
    if (m->map >= 0 && !tx) { base[0] *= 0.82f; base[1] *= 0.80f; base[2] *= 0.76f; }
    glUniform3fv(U[p][U_BASE], 1, base);
    glUniform3fv(U[p][U_EMIS], 1, m->emis);
    glUniform3fv(U[p][U_SPEC], 1, m->spec);
    glUniform1f(U[p][U_SHIN], m->shin);
    glUniform1i(U[p][U_MTYPE], m->type == 'B' ? 0 : m->type == 'P' ? 2 : 1);
    glUniform1i(U[p][U_SIDE], m->side);
    glUniform1i(U[p][U_HASTEX], tx != 0);
    if (tx) {
        const r3_tex *t = &s->texs[m->map];
        glBindTexture(GL_TEXTURE_2D, tx);
        glUniform4f(U[p][U_UVX], t->rep[0], t->rep[1], t->off[0], t->off[1]);
        glUniform1i(U[p][U_FLIPY], t->flip_y);
    }
    glUniform1f(U[p][U_OPA], opa);
    glUniform1f(U[p][U_AT], m->alpha_test);
    glUniform1f(U[p][U_BR], bright);
    glUniform1i(U[p][U_BLEND], blend);
    glUniform1i(U[p][U_NODE], me->node);
    if (blend && !p) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(zw ? 1 : 0);
    if (m->poff) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -2); } else glDisable(GL_POLYGON_OFFSET_FILL);
    /* the frame is upside down (y down), so a counter-clockwise front face shows clockwise; a mirrored node turns it back */
    glFrontFace(mirror ? GL_CCW : GL_CW);
    if (m->side == 2) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(m->side == 1 ? GL_FRONT : GL_BACK); }
    glBindVertexArray(g->vao);
    glDrawElements(GL_TRIANGLES, me->ni - me->ni % 3, GL_UNSIGNED_INT, (const void *)0);
}

static void pass_common(r3_scene *s, int p, int w, int h, float px_, float py_, float P[16], float V[16])
{
    camera(s, w, h, V, P);
    glUseProgram(p ? G.pprog : G.prog);
    glUniformMatrix4fv(U[p][U_P], 1, 1, P);
    if (p) glUniform4f(U[p][U_SUB], (float)w, (float)h, 2 * (px_ + 0.5f) / w - 1, 2 * (py_ + 0.5f) / h - 1);
    else glUniform4f(U[p][U_SUB], 1, 1, 0, 0);
    glUniform1i(U[p][U_TEX], 0);
    lights(s, p);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

int r3gl_render(r3_scene *s, uint32_t *px, int w, int h, int pitch, const uint32_t *bg)
{
    if (G.state < 0 || w < 1 || h < 1) return 0;
    if (!G.state) G.state = init() ? 1 : -1;
    if (G.state < 0) return 0;
    if (!ctx_enter()) return 0;
    uint64_t t0 = SDL_GetPerformanceCounter();
    if (G.scene != s) { forget_scene(); G.scene = s; }
    if (!frames(w, h)) { fprintf(stderr, "overbar box gpu: no %dx%d frame, drawing in software\n", w, h); ctx_leave(); G.state = -1; return 0; }
    r3_update(s);
    glBindFramebuffer(GL_FRAMEBUFFER, G.samples ? G.fbo_ms : G.fbo_rs);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (bg) {                                    /* the room behind: the box's own gradient picture */
        if (!G.bgtex) glGenTextures(1, &G.bgtex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, G.bgtex);
        if (G.bgp != bg || G.bgw != w || G.bgh != h) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, bg);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            G.bgp = bg; G.bgw = w; G.bgh = h;
        }
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(0);
        glUseProgram(G.bprog);
        glUniform1i(glGetUniformLocation(G.bprog, "tex"), 0);
        glBindVertexArray(G.vao0);
        /* (GL's row 0 is read back first, into the picture's row 0, so the quad's bottom shows the background's row 0) */
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask(1);
    }
    float P[16], V[16];
    pass_common(s, 0, w, h, 0, 0, P, V);
    item *it; int n = order(s, V, &it);
    for (int i = 0; i < n; i++) draw_one(s, it[i].mesh, V, 0);
    if (G.samples) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, G.fbo_ms);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, G.fbo_rs);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, G.fbo_rs);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, pitch);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    GLenum err = glGetError();
    ctx_leave();
    G.ms = (float)((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    if (err != GL_NO_ERROR) { fprintf(stderr, "overbar box gpu: GL error 0x%x, drawing in software\n", err); G.state = -1; return 0; }
    return 1;
}

int r3gl_pick(r3_scene *s, int w, int h, int x, int y)
{
    if (G.state <= 0 || !G.fbo_pk || !ctx_enter()) return -1;
    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo_pk);
    glViewport(0, 0, 1, 1);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    static const GLint zero[4] = { 0, 0, 0, 0 };
    glClearBufferiv(GL_COLOR, 0, zero);          /* 0: nothing (a node is its number + 1) */
    float P[16], V[16];
    pass_common(s, 1, w, h, (float)x, (float)y, P, V);
    glDisable(GL_BLEND);
    item *it; int n = order(s, V, &it);
    for (int i = 0; i < n; i++) draw_one(s, it[i].mesh, V, 1);
    GLint v = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, G.fbo_pk);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, 1, 1, GL_RED_INTEGER, GL_INT, &v);
    while (glGetError() != GL_NO_ERROR) {}
    ctx_leave();
    return v - 1;
}

/* the box closed: give the GPU memory back (the context and its window go; the next picture makes them again) */
void r3gl_close(void)
{
    if (G.state <= 0) return;
    SDL_Window *w = SDL_GL_GetCurrentWindow();
    SDL_GLContext c = SDL_GL_GetCurrentContext(), mine = G.ctx;
    free(G.gm); free(G.gt);                      /* (the GL objects go with the context) */
    SDL_GL_DeleteContext(G.ctx);
    SDL_DestroyWindow(G.win);
    memset(&G, 0, sizeof G);
    if (c && c != mine) SDL_GL_MakeCurrent(w, c);
}

int r3gl_active(void) { return G.state > 0; }
float r3gl_ms(void) { return G.ms; }
const char *r3gl_renderer(void) { return G.renderer; }
#endif /* OB_SDL_BASE */
