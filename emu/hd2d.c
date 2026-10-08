// hd2d.c: HD-2D renderer, phase 1 (see hd2d.h and docs/HD2D.md)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include "hd2d.h"
#include "ppu.h"
#include "dsp1.h"

// ------------------------------------------------------------------ minimal GL loader
typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat;
typedef unsigned char GLboolean; typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef intptr_t GLintptr;
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_TEXTURE0 0x84C0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_LEQUAL 0x0203
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE

#define GLFUNCS \
  X(void, glGenTextures, GLsizei, GLuint*) X(void, glBindTexture, GLenum, GLuint) \
  X(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
  X(void, glTexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
  X(void, glTexParameteri, GLenum, GLenum, GLint) X(void, glTexParameterf, GLenum, GLenum, GLfloat) \
  X(void, glGenerateMipmap, GLenum) X(void, glActiveTexture, GLenum) \
  X(GLuint, glCreateShader, GLenum) X(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
  X(void, glCompileShader, GLuint) X(void, glGetShaderiv, GLuint, GLenum, GLint*) \
  X(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) X(GLuint, glCreateProgram, void) \
  X(void, glAttachShader, GLuint, GLuint) X(void, glLinkProgram, GLuint) X(void, glGetProgramiv, GLuint, GLenum, GLint*) \
  X(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) X(void, glUseProgram, GLuint) \
  X(GLint, glGetUniformLocation, GLuint, const GLchar*) X(void, glUniform1i, GLint, GLint) \
  X(void, glUniform1f, GLint, GLfloat) X(void, glUniform2f, GLint, GLfloat, GLfloat) \
  X(void, glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat) \
  X(void, glUniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*) \
  X(void, glGenVertexArrays, GLsizei, GLuint*) X(void, glBindVertexArray, GLuint) \
  X(void, glGenBuffers, GLsizei, GLuint*) X(void, glBindBuffer, GLenum, GLuint) \
  X(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum) \
  X(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
  X(void, glEnableVertexAttribArray, GLuint) X(void, glDrawArrays, GLenum, GLint, GLsizei) \
  X(void, glViewport, GLint, GLint, GLsizei, GLsizei) X(void, glScissor, GLint, GLint, GLsizei, GLsizei) \
  X(void, glEnable, GLenum) X(void, glDisable, GLenum) X(void, glClear, GLbitfield) \
  X(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat) X(void, glBlendFunc, GLenum, GLenum) \
  X(void, glDepthFunc, GLenum) X(void, glPixelStorei, GLenum, GLint) X(void, glDeleteTextures, GLsizei, const GLuint*) \
  X(void, glGenFramebuffers, GLsizei, GLuint*) X(void, glBindFramebuffer, GLenum, GLuint) \
  X(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) X(void, glGenRenderbuffers, GLsizei, GLuint*) \
  X(void, glBindRenderbuffer, GLenum, GLuint) X(void, glRenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei) \
  X(void, glFramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint) X(GLenum, glCheckFramebufferStatus, GLenum) \
  X(void, glDeleteFramebuffers, GLsizei, const GLuint*) X(void, glDeleteRenderbuffers, GLsizei, const GLuint*) \
  X(void, glUniform1fv, GLint, GLsizei, const GLfloat*)

#define X(ret, name, ...) static ret (*p_##name)(__VA_ARGS__);
GLFUNCS
#undef X

// ------------------------------------------------------------------ data
#define VW 400          // virtual screen = SNES widescreen picture
#define VH 224
#define MAXVIEWS 4

typedef struct {
  int row0, row1;       // screen rows (0..223) covered by this Mode 7 view
  int cam;              // index into the captured cameras
  float y0;             // principal row found by matching the PPU's own floor
  float err;
} View;

struct Hd2d {
  GLuint progQuad, progFloor, vao, vbo;
  GLuint texBase, texObj, texFloor, texFill;
  uint8_t* base;                    // 800 x 480 x 4 from snes_setPixelsWide
  uint32_t* obj;                    // 400 x 224 RGBA
  uint8_t* layer;                   // 400 x 224 main-screen layer id
  uint32_t* floorRGBA;              // 1024 x 1024
  uint32_t fill[64];
  uint32_t vramHash, cgramHash;
  Dsp1TapCamera cams[DSP1_TAP_MAX_CAMERAS]; int ncams;
  View views[MAXVIEWS]; int nviews;
  bool on3d, debug;
  // 3D environment: walls extruded from the track's surface types (WRAM $0B00 table, $7F0000 tilemap)
  GLuint progMesh, vaoMesh, vboMesh; int meshVerts;
  uint32_t envHash;
  float* meshData; int meshCap;
  float wallHeight;
  bool walls, wide;                 // options; wide = last captured picture was 16:9
  GLuint texBase43, texOverlay;     // 4:3 classic picture (512x448), menu overlay (256x224)
  uint8_t* base43;
  bool overlayOn;
  // post-processing (HD-2D look): scene FBO + quarter-res blur chain
  GLuint fboScene, texScene, rboDepth, fboA, texA, fboB, texB, progBright, progBlur, progPost;
  int fw, fh;
  bool post;
  float aniso;
};

// ------------------------------------------------------------------ shaders
static const char* vsQuad =
  "#version 330 core\n"
  "layout(location=0) in vec2 p;\n"
  "uniform vec4 rect; uniform vec4 uvRect; out vec2 uv;\n"
  "void main(){ uv = uvRect.xy + p * uvRect.zw; gl_Position = vec4(rect.xy + p * rect.zw, 0.0, 1.0); }\n";
static const char* fsQuad =
  "#version 330 core\n"
  "in vec2 uv; uniform sampler2D tex; out vec4 o;\n"
  "void main(){ o = texture(tex, uv); }\n";
static const char* vsFloor =
  "#version 330 core\n"
  "layout(location=0) in vec2 p;\n"
  "uniform mat4 mvp; uniform vec4 area; out vec2 w;\n"
  "void main(){ w = area.xy + p * area.zw; gl_Position = mvp * vec4(w, 0.0, 1.0); }\n";
static const char* fsFloor =
  "#version 330 core\n"
  "in vec2 w; uniform sampler2D floorTex; uniform sampler2D fillTex; uniform int debug;\n"
  "out vec4 o;\n"
  "void main(){\n"
  "  bool inside = w.x >= 0.0 && w.y >= 0.0 && w.x < 1024.0 && w.y < 1024.0;\n"
  "  vec4 c = inside ? texture(floorTex, w / 1024.0) : texture(fillTex, w / 8.0);\n"
  "  if(debug != 0) c.rgb = mix(c.rgb, vec3(0.2, 0.5, 1.0), 0.25);\n"
  "  o = vec4(c.rgb, 1.0);\n"
  "}\n";

static const char* vsMesh =
  "#version 330 core\n"
  "layout(location=0) in vec3 p; layout(location=1) in vec2 st; layout(location=2) in float shade;\n"
  "uniform mat4 mvp; out vec2 w; out float sh;\n"
  "void main(){ w = st; sh = shade; gl_Position = mvp * vec4(p, 1.0); }\n";
static const char* fsMesh =
  "#version 330 core\n"
  "in vec2 w; in float sh; uniform sampler2D floorTex; uniform int debug; out vec4 o;\n"
  "void main(){\n"
  "  vec4 c = texture(floorTex, w / 1024.0);\n"
  "  if(debug != 0) c.rgb = mix(c.rgb, vec3(1.0, 0.4, 0.2), 0.3);\n"
  "  o = vec4(c.rgb * sh, 1.0);\n"
  "}\n";

static const char* fsBright =
  "#version 330 core\n"
  "in vec2 uv; uniform sampler2D tex; out vec4 o;\n"
  "void main(){ vec3 c = texture(tex, uv).rgb; float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
  "  o = vec4(c, 1.0) * vec4(vec3(1.0), 1.0); o.a = smoothstep(0.55, 0.95, l); }\n";
static const char* fsBlur =
  "#version 330 core\n"
  "in vec2 uv; uniform sampler2D tex; uniform vec2 dir; out vec4 o;\n"
  "void main(){ const float w[5] = float[](0.227, 0.194, 0.121, 0.054, 0.016);\n"
  "  vec4 s = texture(tex, uv) * w[0];\n"
  "  for(int i = 1; i < 5; i++){ s += texture(tex, uv + dir * float(i)) * w[i]; s += texture(tex, uv - dir * float(i)) * w[i]; }\n"
  "  o = s; }\n";
// tilt-shift: inside each 3D view, blur grows towards the horizon (= far away); bloom from the
// bright parts; vignette and a gentle warm grade.
static const char* fsPost =
  "#version 330 core\n"
  "in vec2 uv; uniform sampler2D scene; uniform sampler2D blur; uniform int nbands; uniform float bands[8];\n"
  "uniform float dof, bloom, vignette; out vec4 o;\n"
  "void main(){\n"
  "  vec3 c = texture(scene, uv).rgb; vec4 b = texture(blur, uv);\n"
  "  float m = 0.0;\n"
  "  for(int i = 0; i < nbands; i++){ float t0 = bands[i*2], t1 = bands[i*2+1];\n"
  "    if(uv.y <= t0 && uv.y >= t1){ float t = (t0 - uv.y) / max(t0 - t1, 1e-4); m = max(m, 1.0 - smoothstep(0.0, 0.45, t)); } }\n"
  "  c = mix(c, b.rgb, m * dof);\n"
  "  c += b.rgb * b.a * bloom;\n"
  "  vec2 d = uv - 0.5; c *= 1.0 - vignette * dot(d, d) * 1.6;\n"
  "  c = pow(c, vec3(0.95)) * vec3(1.03, 1.0, 0.96);\n"
  "  o = vec4(c, 1.0);\n"
  "}\n";

static GLuint compile(GLenum type, const char* src) {
  GLuint s = p_glCreateShader(type);
  p_glShaderSource(s, 1, &src, NULL);
  p_glCompileShader(s);
  GLint ok = 0; p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if(!ok) { char log[2048]; p_glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "hd2d shader: %s\n", log); return 0; }
  return s;
}

static GLuint link(const char* vs, const char* fs) {
  GLuint p = p_glCreateProgram(), a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
  if(!a || !b) return 0;
  p_glAttachShader(p, a); p_glAttachShader(p, b); p_glLinkProgram(p);
  GLint ok = 0; p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if(!ok) { char log[2048]; p_glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "hd2d link: %s\n", log); return 0; }
  return p;
}

static GLuint makeTex(int w, int h, bool mip, GLenum magFilter, GLenum wrap) {
  GLuint t; p_glGenTextures(1, &t); p_glBindTexture(GL_TEXTURE_2D, t);
  p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : magFilter);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  return t;
}

Hd2d* hd2d_init(void* (*getProc)(const char*)) {
#define X(ret, name, ...) p_##name = (ret (*)(__VA_ARGS__))getProc(#name); \
  if(!p_##name) { fprintf(stderr, "hd2d: missing GL function %s\n", #name); return NULL; }
  GLFUNCS
#undef X
  Hd2d* h = calloc(1, sizeof(Hd2d));
  h->base = calloc(800 * 480, 4);
  h->obj = calloc(VW * VH, 4);
  h->layer = calloc(VW * VH, 1);
  h->floorRGBA = calloc(1024 * 1024, 4);
  h->progQuad = link(vsQuad, fsQuad);
  h->progFloor = link(vsFloor, fsFloor);
  if(!h->progQuad || !h->progFloor) { hd2d_free(h); return NULL; }
  float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
  p_glGenVertexArrays(1, &h->vao); p_glBindVertexArray(h->vao);
  p_glGenBuffers(1, &h->vbo); p_glBindBuffer(GL_ARRAY_BUFFER, h->vbo);
  p_glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  p_glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); p_glEnableVertexAttribArray(0);
  h->progMesh = link(vsMesh, fsMesh);
  if(!h->progMesh) { hd2d_free(h); return NULL; }
  p_glGenVertexArrays(1, &h->vaoMesh); p_glBindVertexArray(h->vaoMesh);
  p_glGenBuffers(1, &h->vboMesh); p_glBindBuffer(GL_ARRAY_BUFFER, h->vboMesh);
  p_glVertexAttribPointer(0, 3, GL_FLOAT, 0, 6 * 4, (void*)0); p_glEnableVertexAttribArray(0);
  p_glVertexAttribPointer(1, 2, GL_FLOAT, 0, 6 * 4, (void*)12); p_glEnableVertexAttribArray(1);
  p_glVertexAttribPointer(2, 1, GL_FLOAT, 0, 6 * 4, (void*)20); p_glEnableVertexAttribArray(2);
  h->wallHeight = 6.0f;
  h->progBright = link(vsQuad, fsBright); h->progBlur = link(vsQuad, fsBlur); h->progPost = link(vsQuad, fsPost);
  if(!h->progBright || !h->progBlur || !h->progPost) { hd2d_free(h); return NULL; }
  h->post = true;
  p_glBindVertexArray(h->vao);
  h->texBase = makeTex(800, 448, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
  h->texBase43 = makeTex(512, 448, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
  h->texOverlay = makeTex(256, 224, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
  h->base43 = calloc(512 * 480, 4);
  h->walls = true;
  h->texObj = makeTex(VW, VH, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
  h->texFloor = makeTex(1024, 1024, true, GL_NEAREST, GL_CLAMP_TO_EDGE);
  p_glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);   // ignored if unsupported
  h->texFill = makeTex(8, 8, false, GL_NEAREST, GL_REPEAT);
  p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  h->on3d = true;
  return h;
}

void hd2d_free(Hd2d* h) {
  if(!h) return;
  free(h->base); free(h->base43); free(h->obj); free(h->layer); free(h->floorRGBA); free(h->meshData); free(h);
}

void hd2d_setEnabled3D(Hd2d* h, bool on) { h->on3d = on; }
bool hd2d_enabled3D(Hd2d* h) { return h->on3d; }
void hd2d_setDebug(Hd2d* h, bool on) { h->debug = on; }
int hd2d_lastViews(Hd2d* h) { return h->nviews; }
void hd2d_setWalls(Hd2d* h, bool on) { h->walls = on; }
void hd2d_setOverlay(Hd2d* h, const uint32_t* rgba) {
  h->overlayOn = rgba != NULL;
  if(!rgba) return;
  p_glBindTexture(GL_TEXTURE_2D, h->texOverlay);
  p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 224, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

// ------------------------------------------------------------------ camera math (texel space)
typedef struct { double N[3], R[3], U[3], E[3]; double f; } Cam;

static void camFrom(const Dsp1TapCamera* t, Cam* c) {
  double a = t->aas * M_PI / 32768.0, z = t->azs * M_PI / 32768.0;
  double sa = sin(a), ca = cos(a), sz = sin(z), cz = cos(z);
  c->N[0] = -sz * sa; c->N[1] = sz * ca; c->N[2] = cz;
  c->R[0] = ca; c->R[1] = sa; c->R[2] = 0;
  c->U[0] = sa * cz; c->U[1] = -ca * cz; c->U[2] = sz;
  for(int i = 0; i < 3; i++) c->E[i] = ((i == 0 ? t->fx : i == 1 ? t->fy : t->fz) + t->lfe * c->N[i]) / 4.0;
  c->f = t->les;
}

static bool groundHit(const Cam* c, double px, double py, double x0, double y0, double* gx, double* gy) {
  double d[3];
  for(int i = 0; i < 3; i++) d[i] = -c->N[i] * c->f + c->R[i] * (px - x0) - c->U[i] * (py - y0);
  if(d[2] >= -1e-9) return false;
  double t = -c->E[2] / d[2];
  *gx = c->E[0] + t * d[0]; *gy = c->E[1] + t * d[1];
  return true;
}

// texel the PPU samples at screen (x, line) from that line's registers (Mode 7 start formula, no flips)
static void ppuTexel(const int16_t* r, int line, int x, double* tx, double* ty) {
  int A = r[0], B = r[1], C = r[2], D = r[3];
  int hs = ((int16_t)(r[6] << 3)) >> 3, vs = ((int16_t)(r[7] << 3)) >> 3;
  int xc = ((int16_t)(r[4] << 3)) >> 3, yc = ((int16_t)(r[5] << 3)) >> 3;
  int ch = hs - xc, cv = vs - yc;
  ch = (ch & 0x2000) ? (ch | ~1023) : (ch & 1023);
  cv = (cv & 0x2000) ? (cv | ~1023) : (cv & 1023);
  int ry = line;
  int sx = ((A * ch) & ~63) + ((B * ry) & ~63) + ((B * cv) & ~63) + (xc << 8);
  int sy = ((C * ch) & ~63) + ((D * ry) & ~63) + ((D * cv) & ~63) + (yc << 8);
  *tx = (sx + A * x) / 256.0; *ty = (sy + C * x) / 256.0;
}

// Pair each Mode 7 block of lines with the DSP-1 camera that reproduces it, and find the
// principal row (the game puts the camera focus somewhere in the lower part of each view).
static void matchViews(Hd2d* h, Ppu* ppu) {
  h->nviews = 0;
  int l = 1;
  while(l <= 224 && h->nviews < MAXVIEWS) {
    if(!ppu->lineIsM7[l]) { l++; continue; }
    int s = l;
    while(l <= 224 && ppu->lineIsM7[l]) l++;
    int e = l - 1;
    if(e - s < 8) continue;
    View best = {0}; best.err = 1e30f; best.cam = -1;
    for(int ci = 0; ci < h->ncams; ci++) {
      Cam c; camFrom(&h->cams[ci], &c);
      // sample lines in the lower 60% of the block (near the camera: well conditioned)
      int ls[4]; for(int k = 0; k < 4; k++) ls[k] = e - (e - s) * k * 15 / 100;
      for(double y0 = s - 60; y0 < e + 160; y0 += 0.5) {
        double err = 0; bool bad = false;
        for(int k = 0; k < 4 && !bad; k++) {
          for(int xi = 0; xi < 3; xi++) {
            int px = xi == 0 ? 32 : xi == 1 ? 128 : 224;
            double tx, ty, gx, gy;
            ppuTexel(ppu->lineM7[ls[k]], ls[k], px, &tx, &ty);
            if(!groundHit(&c, px + 0.5, ls[k] - 1 + 0.5, 128.0, y0, &gx, &gy)) { bad = true; break; }
            err += (gx - tx) * (gx - tx) + (gy - ty) * (gy - ty);
          }
        }
        if(!bad && err < best.err) { best.err = err; best.y0 = y0; best.cam = ci; }
      }
    }
    if(best.cam >= 0 && best.err / 12 < 64.0) {      // rms < 8 texels: accept
      best.row0 = s - 1; best.row1 = e - 1;
      h->views[h->nviews++] = best;
    }
  }
}

// ------------------------------------------------------------------ capture
static uint32_t hash32(const void* p, size_t n) {
  const uint8_t* b = p; uint32_t x = 2166136261u;
  for(size_t i = 0; i < n; i++) { x ^= b[i]; x *= 16777619u; }
  return x;
}

static uint32_t cgramRGBA(uint16_t c) {
  int r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
  return 0xff000000u | b << 16 | g << 8 | r;
}

// ------------------------------------------------------------------ environment mesh
static void vtx(Hd2d* h, float x, float y, float z, float s, float t, float shade) {
  if(h->meshVerts + 1 > h->meshCap) { h->meshCap = h->meshCap ? h->meshCap * 2 : 65536; h->meshData = realloc(h->meshData, h->meshCap * 6 * 4); }
  float* v = &h->meshData[h->meshVerts++ * 6];
  v[0] = x; v[1] = y; v[2] = z; v[3] = s; v[4] = t; v[5] = shade;
}
// quad a-b-c-d (two triangles)
static void quad(Hd2d* h, const float* a, const float* b, const float* c, const float* d, float shade) {
  const float* q[6] = {a, b, c, a, c, d};
  for(int i = 0; i < 6; i++) vtx(h, q[i][0], q[i][1], q[i][2], q[i][3], q[i][4], shade);
}

// Height of a tile from its surface type. $80+ = solid (walls/borders) in every track type
// seen so far; everything else stays on the ground for now.
static float tileHeight(Hd2d* h, uint8_t type) { return (type & 0x80) ? h->wallHeight : 0.0f; }

static void buildEnvironment(Hd2d* h, const uint8_t* wram) {
  const uint8_t* types = wram + 0x0B00;       // surface type per tile index
  const uint8_t* map = wram + 0x10000;        // 128x128 tilemap copy ($7F0000)
  h->meshVerts = 0;
  for(int ty = 0; ty < 128; ty++) for(int tx = 0; tx < 128; tx++) {
    float z = tileHeight(h, types[map[ty * 128 + tx]]);
    if(z <= 0) continue;
    float x0 = tx * 8, y0 = ty * 8, x1 = x0 + 8, y1 = y0 + 8;
    // top: same texels as the original floor tile
    float a[5] = {x0, y0, z, x0, y0}, b[5] = {x1, y0, z, x1, y0}, c[5] = {x1, y1, z, x1, y1}, d[5] = {x0, y1, z, x0, y1};
    quad(h, a, b, c, d, 1.0f);
    // sides towards lower neighbours: texture = the tile's edge texels stretched down (stripes)
    static const int nb[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static const float shades[4] = {0.85f, 0.70f, 0.60f, 0.75f};  // light from the north-west
    for(int k = 0; k < 4; k++) {
      int nx = tx + nb[k][0], ny = ty + nb[k][1];
      float nz = (nx < 0 || ny < 0 || nx > 127 || ny > 127) ? 0 : tileHeight(h, types[map[ny * 128 + nx]]);
      if(nz >= z) continue;
      float ex0, ey0, ex1, ey1;      // edge endpoints
      switch(k) {
        case 0: ex0 = x0; ey0 = y0; ex1 = x1; ey1 = y0; break;
        case 1: ex0 = x1; ey0 = y0; ex1 = x1; ey1 = y1; break;
        case 2: ex0 = x1; ey0 = y1; ex1 = x0; ey1 = y1; break;
        default: ex0 = x0; ey0 = y1; ex1 = x0; ey1 = y0; break;
      }
      // texel just inside the edge so the stripe picks up the border colours
      // 2.5 texels in: tile edges often carry a dark outline pixel that would turn the whole side black
      float in0x = ex0 + (k == 1 ? -2.5f : k == 3 ? 2.5f : 0), in0y = ey0 + (k == 0 ? 2.5f : k == 2 ? -2.5f : 0);
      float in1x = ex1 + (k == 1 ? -2.5f : k == 3 ? 2.5f : 0), in1y = ey1 + (k == 0 ? 2.5f : k == 2 ? -2.5f : 0);
      float p0[5] = {ex0, ey0, z, in0x, in0y}, p1[5] = {ex1, ey1, z, in1x, in1y};
      float p2[5] = {ex1, ey1, nz, in1x, in1y}, p3[5] = {ex0, ey0, nz, in0x, in0y};
      quad(h, p0, p1, p2, p3, shades[k]);
    }
  }
  p_glBindBuffer(GL_ARRAY_BUFFER, h->vboMesh);
  p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)h->meshVerts * 6 * 4, h->meshData, GL_DYNAMIC_DRAW);
}

void hd2d_capture(Hd2d* h, Snes* snes) {
  Ppu* ppu = snes->ppu;
  h->wide = ppu->widescreen;
  if(!h->wide) {                    // classic 4:3 picture through the same presentation path
    h->nviews = 0;
    snes_setPixels(snes, h->base43);
    for(int i = 16 * 512; i < 464 * 512; i++) {
      uint8_t* p = &h->base43[i * 4];
      uint8_t r = p[3], g = p[2], b = p[1];
      p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
    }
    p_glBindTexture(GL_TEXTURE_2D, h->texBase43);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 448, GL_RGBA, GL_UNSIGNED_BYTE, h->base43 + 16 * 512 * 4);
    return;
  }
  snes_setPixelsWide(snes, h->base);
  int half = ppu->evenFrame ? 0 : 239;
  for(int y = 0; y < VH; y++) {
    memcpy(&h->obj[y * VW], ppu->objLayer[y + half], VW * 4);
    memcpy(&h->layer[y * VW], ppu->layerTap[y + half], VW);
  }
  // colour-math pixels (alpha $FE) are kept only where they form small shapes (the position
  // number); rows where most of the picture has colour math are a full-screen effect, not an overlay
  for(int y = 0; y < VH; y++) {
    int n = 0;
    for(int x = 0; x < VW; x++) n += (h->obj[y * VW + x] >> 24) == 0xfe;
    if(n > VW / 3) for(int x = 0; x < VW; x++) if((h->obj[y * VW + x] >> 24) == 0xfe) h->obj[y * VW + x] = 0;
  }
  h->ncams = dsp1_tapNumCameras;
  memcpy(h->cams, dsp1_tapCameras, sizeof(h->cams));
  if(h->on3d && ppu->hd2dTaps) matchViews(h, ppu); else h->nviews = 0;
  // floor texture (only when VRAM tiles/map or the palette changed)
  uint32_t vh = hash32(ppu->vram, 0x8000 * 2), ch = hash32(ppu->cgram, 512);
  if(h->nviews && (vh != h->vramHash || ch != h->cgramHash)) {
    h->vramHash = vh; h->cgramHash = ch;
    uint32_t pal[256];
    for(int i = 0; i < 256; i++) pal[i] = cgramRGBA(ppu->cgram[i]);
    for(int ty = 0; ty < 128; ty++) for(int tx = 0; tx < 128; tx++) {
      int tile = ppu->vram[ty * 128 + tx] & 0xff;
      for(int py = 0; py < 8; py++) for(int px = 0; px < 8; px++)
        h->floorRGBA[(ty * 8 + py) * 1024 + tx * 8 + px] = pal[ppu->vram[tile * 64 + py * 8 + px] >> 8];
    }
    for(int i = 0; i < 64; i++) h->fill[i] = pal[ppu->vram[i] >> 8];
    p_glBindTexture(GL_TEXTURE_2D, h->texFloor);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1024, GL_RGBA, GL_UNSIGNED_BYTE, h->floorRGBA);
    p_glGenerateMipmap(GL_TEXTURE_2D);
    p_glBindTexture(GL_TEXTURE_2D, h->texFill);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, h->fill);
  }
  if(h->nviews) {
    uint32_t eh = hash32(snes->ram + 0x0B00, 0x100) ^ (hash32(snes->ram + 0x10000, 0x4000) * 31u);
    if(eh != h->envHash) { h->envHash = eh; buildEnvironment(h, snes->ram); }
  }
  // base picture: rows 16..463 of the 800x480 widescreen output, stored BGRX -> convert to RGBA
  for(int i = 16 * 800; i < 464 * 800; i++) {
    uint8_t* p = &h->base[i * 4];
    uint8_t r = p[3], g = p[2], b = p[1];
    p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
  }
  // Sky band: the game only streams the visible 256 columns of its panorama into VRAM, so the
  // widescreen side columns of non-3D rows are empty. Mirror the picture at the 4:3 edges there.
  if(h->nviews) {
    int top3d = 224;
    for(int i = 0; i < h->nviews; i++) if(h->views[i].row0 < top3d) top3d = h->views[i].row0;
    for(int r = 0; r < 224; r++) {
      bool in3d = false;
      for(int i = 0; i < h->nviews; i++) if(r >= h->views[i].row0 && r <= h->views[i].row1) in3d = true;
      if(in3d) continue;
      for(int half = 0; half < 2; half++) {
        uint32_t* line = (uint32_t*)(h->base + ((16 + r * 2 + half) * 800) * 4);
        int L = PPU_WS_EXT * 2, Rr = (PPU_WS_EXT + 256) * 2;       // output pixels (2 per SNES pixel)
        // fill the sides with this row's typical sky colour (component medians of the 4:3 part),
        // fading in from the edge pixel so there's no seam. Copying content would also copy HUD text.
        int hist[3][256] = {{0}};
        for(int x = L; x < Rr; x++) { uint32_t c = line[x]; hist[0][c & 255]++; hist[1][(c >> 8) & 255]++; hist[2][(c >> 16) & 255]++; }
        uint32_t med = 0xff000000u;
        for(int ch = 0; ch < 3; ch++) { int acc = 0, k = 0; while(k < 255 && (acc += hist[ch][k]) < (Rr - L) / 2) k++; med |= (uint32_t)k << (8 * ch); }
        for(int side = 0; side < 2; side++) {
          uint32_t edge = side ? line[Rr - 1] : line[L];
          for(int i = 0; i < L; i++) {        // i = distance from the 4:3 edge
            float t = i < 32 ? i / 32.0f : 1.0f;
            uint32_t c = 0xff000000u;
            for(int ch = 0; ch < 3; ch++) {
              float e = (edge >> (8 * ch)) & 255, m = (med >> (8 * ch)) & 255;
              c |= (uint32_t)(e + (m - e) * t) << (8 * ch);
            }
            line[side ? Rr + i : L - 1 - i] = c;
          }
        }
      }
    }
  }
  p_glBindTexture(GL_TEXTURE_2D, h->texBase);
  p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 800, 448, GL_RGBA, GL_UNSIGNED_BYTE, h->base + 16 * 800 * 4);
  p_glBindTexture(GL_TEXTURE_2D, h->texObj);
  p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, h->obj);
}

// ------------------------------------------------------------------ render
static void drawQuad(Hd2d* h, GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
  p_glUseProgram(h->progQuad);
  p_glUniform4f(p_glGetUniformLocation(h->progQuad, "rect"), x0, y0, x1 - x0, y1 - y0);
  p_glUniform4f(p_glGetUniformLocation(h->progQuad, "uvRect"), u0, v0, u1 - u0, v1 - v0);
  p_glUniform1i(p_glGetUniformLocation(h->progQuad, "tex"), 0);
  p_glActiveTexture(GL_TEXTURE0); p_glBindTexture(GL_TEXTURE_2D, tex);
  p_glBindVertexArray(h->vao);
  p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void makeFbo(GLuint* fbo, GLuint* tex, int w, int hgt, GLuint* depth) {
  if(*fbo) { p_glDeleteFramebuffers(1, fbo); p_glDeleteTextures(1, tex); if(depth && *depth) p_glDeleteRenderbuffers(1, depth); }
  *tex = makeTex(w, hgt, false, GL_LINEAR, GL_CLAMP_TO_EDGE);
  p_glGenFramebuffers(1, fbo); p_glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
  p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
  if(depth) {
    p_glGenRenderbuffers(1, depth); p_glBindRenderbuffer(GL_RENDERBUFFER, *depth);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, hgt);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth);
  }
  if(p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "hd2d: framebuffer incomplete\n");
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void drawWith(Hd2d* h, GLuint prog, GLuint tex) {
  p_glUseProgram(prog);
  p_glUniform4f(p_glGetUniformLocation(prog, "rect"), -1, -1, 2, 2);
  p_glUniform4f(p_glGetUniformLocation(prog, "uvRect"), 0, 0, 1, 1);
  p_glUniform1i(p_glGetUniformLocation(prog, "tex"), 0);
  p_glActiveTexture(GL_TEXTURE0); p_glBindTexture(GL_TEXTURE_2D, tex);
  p_glBindVertexArray(h->vao);
  p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void renderScene(Hd2d* h, int w, int hh);

void hd2d_setPost(Hd2d* h, bool on) { h->post = on; }
bool hd2d_post(Hd2d* h) { return h->post; }

static void drawOverlay(Hd2d* h, int w, int hh) {
  if(!h->overlayOn) return;
  int vwid = h->wide ? VW : 256;
  float scale = fminf((float)w / vwid, (float)hh / VH);
  int vw = (int)(vwid * scale), vh = (int)(VH * scale);
  int vx = (w - vw) / 2, vy = (hh - vh) / 2;
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  p_glViewport(vx, vy, vw, vh);
  p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_SCISSOR_TEST);
  p_glEnable(GL_BLEND); p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  float x0 = h->wide ? -1.0f + 2.0f * PPU_WS_EXT / VW : -1.0f, x1 = -x0;   // the 4:3 middle of the picture
  drawQuad(h, h->texOverlay, x0, 1, x1, -1, 0, 0, 1, 1);
  p_glDisable(GL_BLEND);
}

static void renderPost(Hd2d* h, int w, int hh);

void hd2d_render(Hd2d* h, int w, int hh) {
  if(!h->post || !h->nviews) { p_glBindFramebuffer(GL_FRAMEBUFFER, 0); renderScene(h, w, hh); }
  else renderPost(h, w, hh);
  drawOverlay(h, w, hh);
}

static void renderPost(Hd2d* h, int w, int hh) {
  if(w != h->fw || hh != h->fh) {
    h->fw = w; h->fh = hh;
    makeFbo(&h->fboScene, &h->texScene, w, hh, &h->rboDepth);
    makeFbo(&h->fboA, &h->texA, w / 4 + 1, hh / 4 + 1, NULL);
    makeFbo(&h->fboB, &h->texB, w / 4 + 1, hh / 4 + 1, NULL);
  }
  p_glBindFramebuffer(GL_FRAMEBUFFER, h->fboScene);
  renderScene(h, w, hh);
  // bright/blur chain at quarter resolution
  int qw = w / 4 + 1, qh = hh / 4 + 1;
  p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_BLEND); p_glDisable(GL_SCISSOR_TEST);
  p_glBindFramebuffer(GL_FRAMEBUFFER, h->fboA); p_glViewport(0, 0, qw, qh);
  drawWith(h, h->progBright, h->texScene);
  for(int pass = 0; pass < 2; pass++) {
    p_glBindFramebuffer(GL_FRAMEBUFFER, h->fboB);
    p_glUseProgram(h->progBlur); p_glUniform2f(p_glGetUniformLocation(h->progBlur, "dir"), 1.0f / qw, 0);
    drawWith(h, h->progBlur, h->texA);
    p_glBindFramebuffer(GL_FRAMEBUFFER, h->fboA);
    p_glUseProgram(h->progBlur); p_glUniform2f(p_glGetUniformLocation(h->progBlur, "dir"), 0, 1.0f / qh);
    drawWith(h, h->progBlur, h->texB);
  }
  // compose to the window
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0); p_glViewport(0, 0, w, hh);
  p_glUseProgram(h->progPost);
  float bands[8] = {0}; int nb = 0;
  float scale = fminf((float)w / VW, (float)hh / VH);
  int vh = (int)(VH * scale), vy = (hh - vh) / 2;
  for(int i = 0; i < h->nviews && nb < 4; i++) {
    if(h->cams[h->views[i].cam].lfe >= 0x1000) continue;          // no tilt-shift on the overview map
    // uv.y = 0 at the bottom of the window; band from the view's top row (far) to its bottom row (near)
    bands[nb * 2] = (vy + (VH - h->views[i].row0) * scale) / hh;
    bands[nb * 2 + 1] = (vy + (VH - (h->views[i].row1 + 1)) * scale) / hh;
    nb++;
  }
  p_glUniform1i(p_glGetUniformLocation(h->progPost, "nbands"), nb);
  p_glUniform1fv(p_glGetUniformLocation(h->progPost, "bands"), 8, bands);
  p_glUniform1f(p_glGetUniformLocation(h->progPost, "dof"), 0.85f);
  p_glUniform1f(p_glGetUniformLocation(h->progPost, "bloom"), 0.35f);
  p_glUniform1f(p_glGetUniformLocation(h->progPost, "vignette"), 0.35f);
  p_glUniform1i(p_glGetUniformLocation(h->progPost, "scene"), 0);
  p_glUniform1i(p_glGetUniformLocation(h->progPost, "blur"), 1);
  p_glUniform4f(p_glGetUniformLocation(h->progPost, "rect"), -1, -1, 2, 2);
  p_glUniform4f(p_glGetUniformLocation(h->progPost, "uvRect"), 0, 0, 1, 1);
  p_glActiveTexture(GL_TEXTURE0 + 1); p_glBindTexture(GL_TEXTURE_2D, h->texA);
  p_glActiveTexture(GL_TEXTURE0); p_glBindTexture(GL_TEXTURE_2D, h->texScene);
  p_glBindVertexArray(h->vao);
  p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void renderScene(Hd2d* h, int w, int hh) {
  // letterbox the 400x224 (or 256x224 classic) virtual screen
  int vwid = h->wide ? VW : 256;
  float scale = fminf((float)w / vwid, (float)hh / VH);
  int vw = (int)(vwid * scale), vh = (int)(VH * scale);
  int vx = (w - vw) / 2, vy = (hh - vh) / 2;
  p_glViewport(0, 0, w, hh);
  p_glClearColor(0, 0, 0, 1);
  p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  p_glViewport(vx, vy, vw, vh);
  p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_BLEND);
  // 1. classic picture (sky, HUD, map...): texture row 0 = top of screen
  drawQuad(h, h->wide ? h->texBase : h->texBase43, -1, 1, 1, -1, 0, 0, 1, 1);
  if(!h->nviews) return;
  // 2. each Mode 7 view re-rendered as a 3D plane at full resolution
  p_glEnable(GL_SCISSOR_TEST);
  p_glEnable(GL_DEPTH_TEST); p_glDepthFunc(GL_LEQUAL);
  for(int vi = 0; vi < h->nviews; vi++) {
    View* v = &h->views[vi];
    Cam c; camFrom(&h->cams[v->cam], &c);
    int sy0 = vy + (int)floorf((VH - (v->row1 + 1)) * scale), sy1 = vy + (int)ceilf((VH - v->row0) * scale);
    p_glScissor(vx, sy0, vw, sy1 - sy0);
    p_glClear(GL_DEPTH_BUFFER_BIT);
    // projection: pinhole f, principal point (x0,y0) on the 400x224 virtual screen
    double f = c.f, x0 = 128.0 + PPU_WS_EXT, y0 = v->y0, n = 0.5, fa = 16384.0;
    double P[16] = {0}, V[16] = {0}, M[16];
    P[0] = 2 * f / VW;  P[8] = 2 * x0 / VW - 1;
    P[5] = 2 * f / VH;  P[9] = 1 - 2 * y0 / VH;
    P[10] = (fa + n) / (fa - n); P[14] = -2 * fa * n / (fa - n);
    P[11] = 1;
    // view: rows R, U, -N (depth = forward), column-major
    double F[3] = {-c.N[0], -c.N[1], -c.N[2]};
    for(int i = 0; i < 3; i++) { V[i * 4 + 0] = c.R[i]; V[i * 4 + 1] = c.U[i]; V[i * 4 + 2] = F[i]; }
    V[12] = -(c.R[0] * c.E[0] + c.R[1] * c.E[1] + c.R[2] * c.E[2]);
    V[13] = -(c.U[0] * c.E[0] + c.U[1] * c.E[1] + c.U[2] * c.E[2]);
    V[14] = -(F[0] * c.E[0] + F[1] * c.E[1] + F[2] * c.E[2]);
    V[15] = 1;
    for(int col = 0; col < 4; col++) for(int row = 0; row < 4; row++) {
      double s = 0; for(int k = 0; k < 4; k++) s += P[k * 4 + row] * V[col * 4 + k];
      M[col * 4 + row] = s;
    }
    float Mf[16]; for(int i = 0; i < 16; i++) Mf[i] = (float)M[i];
    p_glUseProgram(h->progFloor);
    p_glUniformMatrix4fv(p_glGetUniformLocation(h->progFloor, "mvp"), 1, 0, Mf);
    float R = 6144.0f;
    p_glUniform4f(p_glGetUniformLocation(h->progFloor, "area"), (float)c.E[0] - R, (float)c.E[1] - R, 2 * R, 2 * R);
    p_glUniform1i(p_glGetUniformLocation(h->progFloor, "floorTex"), 0);
    p_glUniform1i(p_glGetUniformLocation(h->progFloor, "fillTex"), 1);
    p_glUniform1i(p_glGetUniformLocation(h->progFloor, "debug"), h->debug);
    p_glActiveTexture(GL_TEXTURE0 + 1); p_glBindTexture(GL_TEXTURE_2D, h->texFill);
    p_glActiveTexture(GL_TEXTURE0); p_glBindTexture(GL_TEXTURE_2D, h->texFloor);
    p_glBindVertexArray(h->vao);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if(h->walls && h->meshVerts && h->cams[v->cam].lfe < 0x1000) {     // walls in the driving views (not the overview map)
      p_glUseProgram(h->progMesh);
      p_glUniformMatrix4fv(p_glGetUniformLocation(h->progMesh, "mvp"), 1, 0, Mf);
      p_glUniform1i(p_glGetUniformLocation(h->progMesh, "floorTex"), 0);
      p_glUniform1i(p_glGetUniformLocation(h->progMesh, "debug"), h->debug);
      p_glBindVertexArray(h->vaoMesh);
      p_glDrawArrays(GL_TRIANGLES, 0, h->meshVerts);
    }
  }
  p_glDisable(GL_DEPTH_TEST);
  // 3. the original sprites on top (screen space, phase 1)
  p_glEnable(GL_BLEND); p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for(int vi = 0; vi < h->nviews; vi++) {
    View* v = &h->views[vi];
    int sy0 = vy + (int)floorf((VH - (v->row1 + 1)) * scale), sy1 = vy + (int)ceilf((VH - v->row0) * scale);
    p_glScissor(vx, sy0, vw, sy1 - sy0);
    drawQuad(h, h->texObj, -1, 1, 1, -1, 0, 0, 1, 1);
  }
  p_glDisable(GL_BLEND); p_glDisable(GL_SCISSOR_TEST);
}
