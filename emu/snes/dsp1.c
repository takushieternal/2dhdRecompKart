// DSP-1 high-level emulation. See dsp1.h for scope/accuracy notes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dsp1.h"
#include "statehandler.h"

uint32_t dsp1_cmdCount[64];
Dsp1TapCamera dsp1_tapCameras[DSP1_TAP_MAX_CAMERAS];
Dsp1TapProject dsp1_tapProjects[DSP1_TAP_MAX_PROJECTS];
int dsp1_tapNumCameras, dsp1_tapNumProjects;
void dsp1_tapReset(void) { dsp1_tapNumCameras = 0; dsp1_tapNumProjects = 0; }

struct Dsp1 {
  // host interface
  bool waitCmd;
  uint8_t cmd;
  int inCount;       // bytes still expected
  int inIndex;       // bytes received
  uint8_t in[16];
  int outCount;      // bytes still readable
  int outIndex;
  uint8_t out[2048];
  // persistent state set by commands
  int16_t matrix[3][3][3];        // attitude A/B/C
  // projection (cmd 02)
  int16_t sinAas, cosAas, sinAzs, cosAzs, sinAZS, cosAZS;
  int16_t secC2, secE2;
  int16_t nx, ny, nz, gx, gy, gz, les, vOffset, vPlaneC, vPlaneE;
  int16_t camX, camY, camZ, cx, cy;
  int16_t rasterVs;
};

// --------------------------------------------------------------- fixed point helpers
static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
static int16_t mul15(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 15); }

static int16_t dsin(int16_t a) {
  if(a == -32768) return 0;
  double s = sin((double)a * M_PI / 32768.0) * 32768.0;
  return sat16((int32_t)s);
}
static int16_t dcos(int16_t a) {
  double c = cos((double)a * M_PI / 32768.0) * 32768.0;
  return sat16((int32_t)c);
}

static void normalize(int16_t m, int16_t* c, int16_t* e) {
  int16_t i = 0x4000; int e2 = 0;
  if(m < 0) { while((m & i) && i) { i >>= 1; e2++; } }
  else      { while(!(m & i) && i) { i >>= 1; e2++; } }
  *c = e2 > 0 ? (int16_t)(m << e2) : m;
  *e -= e2;
}

static int16_t truncateCE(int16_t c, int16_t e) {
  if(e > 0) {
    if(c > 0) return 32767;
    if(c < 0) return -32767;
    return 0;
  }
  if(e < 0) {
    if(e < -15) return c < 0 ? -1 : 0;
    return (int16_t)(((int32_t)c * (1 << (15 + e))) >> 15);
  }
  return c;
}

static void inverse(int16_t coef, int16_t exp, int16_t* ic, int16_t* ie) {
  int sign = 1;
  int32_t c = coef;
  if(c < 0) { if(c < -32767) c = -32767; c = -c; sign = -1; }
  if(c == 0) { *ic = 0x7fff; *ie = 0x002f; return; }
  while(c < 0x4000) { c <<= 1; exp--; }
  if(c == 0x4000) {
    if(sign == 1) *ic = 0x7fff;
    else { *ic = -0x4000; exp--; }
  } else {
    int32_t i = (1 << 29) / c;
    if(i > 0x7fff) i = 0x7fff;
    *ic = (int16_t)(i * sign);
  }
  *ie = 1 - exp;
}

// --------------------------------------------------------------- commands
static const int16_t maxAzsExp[16] = {
  0x38b4, 0x38b7, 0x38ba, 0x38be, 0x38c0, 0x38c4, 0x38c7, 0x38ca,
  0x38ce, 0x38d0, 0x38d4, 0x38d7, 0x38da, 0x38dd, 0x38e0, 0x38e4
};

static void cmdParameter(Dsp1* d, int16_t fx, int16_t fy, int16_t fz, int16_t lfe, int16_t les,
                         int16_t aas, int16_t azs, int16_t* vof, int16_t* vva, int16_t* cx, int16_t* cy) {
  int16_t c, e, maxAZS, AZS = azs;
  d->sinAas = dsin(aas); d->cosAas = dcos(aas);
  d->sinAzs = dsin(azs); d->cosAzs = dcos(azs);
  d->nx = mul15(d->sinAzs, -d->sinAas);
  d->ny = mul15(d->sinAzs, d->cosAas);
  d->nz = mul15(d->cosAzs, 0x7fff);
  int16_t centreX = fx + mul15(lfe, d->nx);
  int16_t centreY = fy + mul15(lfe, d->ny);
  int16_t centreZ = fz + mul15(lfe, d->nz);
  d->camX = centreX; d->camY = centreY; d->camZ = centreZ;
  d->gx = centreX - mul15(les, d->nx);
  d->gy = centreY - mul15(les, d->ny);
  d->gz = centreZ - mul15(les, d->nz);
  d->les = les;
  e = 0; normalize(centreZ, &c, &e);
  d->vPlaneC = c; d->vPlaneE = e;
  int idx = -e; if(idx < 0) idx = 0; if(idx > 15) idx = 15;
  maxAZS = maxAzsExp[idx];
  if(AZS < 0) { maxAZS = -maxAZS; if(AZS < maxAZS + 1) AZS = maxAZS + 1; }
  else if(AZS > maxAZS) AZS = maxAZS;
  d->sinAZS = dsin(AZS); d->cosAZS = dcos(AZS);
  int16_t sc1, se1;
  inverse(d->cosAZS, 0, &sc1, &se1);
  normalize(mul15(c, sc1), &c, &e);
  e += se1;
  c = mul15(truncateCE(c, e), d->sinAZS);
  centreX += mul15(c, d->sinAas);
  centreY -= mul15(c, d->cosAas);
  *cx = centreX; *cy = centreY;
  d->cx = centreX; d->cy = centreY;
  *vof = 0;
  d->vOffset = mul15(les, d->cosAZS);
  int16_t csec;
  inverse(d->sinAZS, 0, &csec, &e);
  normalize(d->vOffset, &c, &e);
  normalize(mul15(c, csec), &c, &e);
  if(c == -32768) { c >>= 1; e++; }
  *vva = truncateCE(-c, e);
  inverse(d->cosAZS, 0, &d->secC2, &d->secE2);
}

static void cmdRaster(Dsp1* d, int16_t vs, int16_t* an, int16_t* bn, int16_t* cn, int16_t* dn) {
  int16_t c, e, c1, e1;
  inverse(mul15(vs, d->sinAZS) + d->vOffset, 7, &c, &e);
  e += d->vPlaneE;
  c1 = mul15(c, d->vPlaneC);
  e1 = e + d->secE2;
  normalize(c1, &c, &e);
  c = truncateCE(c, e);
  *an = mul15(c, d->cosAas);
  *cn = mul15(c, d->sinAas);
  normalize(mul15(c1, d->secC2), &c, &e1);
  c = truncateCE(c, e1);
  *bn = mul15(c, -d->sinAas);
  *dn = mul15(c, d->cosAas);
}

// camera basis in floats (unit vectors)
static void basis(Dsp1* d, double n[3], double r[3], double u[3]) {
  double sa = d->sinAas / 32768.0, ca = d->cosAas / 32768.0;
  double sz = d->sinAzs / 32768.0, cz = d->cosAzs / 32768.0;
  n[0] = -sz * sa; n[1] = sz * ca; n[2] = cz;
  r[0] = ca; r[1] = sa; r[2] = 0;
  u[0] = sa * cz; u[1] = -ca * cz; u[2] = sz;
}

static void cmdProject(Dsp1* d, int16_t x, int16_t y, int16_t z, int16_t* h, int16_t* v, int16_t* m) {
  double n[3], r[3], u[3];
  basis(d, n, r, u);
  double dx = x - d->camX, dy = y - d->camY, dz = z - d->camZ;
  double depth = -(dx * n[0] + dy * n[1] + dz * n[2]);
  if(depth < 1) depth = 1;
  double sh = d->les * (dx * r[0] + dy * r[1] + dz * r[2]) / depth;
  double sv = -d->les * (dx * u[0] + dy * u[1] + dz * u[2]) / depth;
  double sm = 256.0 * d->les / depth;
  *h = sat16((int32_t)sh); *v = sat16((int32_t)sv); *m = sat16((int32_t)sm);
}

static void cmdTarget(Dsp1* d, int16_t h, int16_t v, int16_t* x, int16_t* y) {
  double n[3], r[3], u[3];
  basis(d, n, r, u);
  // ray from camera through screen point (h, v) [v positive down]
  double dir[3];
  for(int i = 0; i < 3; i++) dir[i] = -d->les * n[i] + h * r[i] - v * u[i];
  if(dir[2] >= -1e-6) { *x = d->camX; *y = d->camY; return; }
  double t = -(double)d->camZ / dir[2];
  *x = sat16((int32_t)(d->camX + t * dir[0]));
  *y = sat16((int32_t)(d->camY + t * dir[1]));
}

static void cmdAttitude(Dsp1* d, int which, int16_t m, int16_t zr, int16_t yr, int16_t xr) {
  int16_t sz = dsin(zr), cz = dcos(zr), sy = dsin(yr), cy = dcos(yr), sx = dsin(xr), cx = dcos(xr);
  int16_t (*M)[3] = d->matrix[which];
  m >>= 1;
  M[0][0] = mul15(mul15(m, cz), cy);
  M[0][1] = -mul15(mul15(m, sz), cy);
  M[0][2] = mul15(m, sy);
  M[1][0] = mul15(mul15(m, sz), cx) + mul15(mul15(mul15(m, cz), sx), sy);
  M[1][1] = mul15(mul15(m, cz), cx) - mul15(mul15(mul15(m, sz), sx), sy);
  M[1][2] = -mul15(mul15(m, sx), cy);
  M[2][0] = mul15(mul15(m, sz), sx) - mul15(mul15(mul15(m, cz), cx), sy);
  M[2][1] = mul15(mul15(m, cz), sx) + mul15(mul15(mul15(m, sz), cx), sy);
  M[2][2] = mul15(mul15(m, cx), cy);
}

static void pushw(Dsp1* d, int16_t w) {
  d->out[d->outCount++] = w & 0xff;
  d->out[d->outCount++] = (w >> 8) & 0xff;
}

static int16_t inw(Dsp1* d, int i) { return (int16_t)(d->in[i * 2] | (d->in[i * 2 + 1] << 8)); }

// number of 16-bit input words per command, -1 = unknown
static int inputWords(uint8_t c) {
  switch(c) {
    case 0x00: case 0x20: case 0x10: case 0x30: case 0x04: case 0x24: case 0x0e: case 0x1e: case 0x2e: case 0x3e: return 2;
    case 0x08: case 0x28: case 0x0c: case 0x2c: case 0x06: case 0x16: case 0x26: case 0x36:
    case 0x0d: case 0x09: case 0x39: case 0x3d: case 0x1d: case 0x19: case 0x2d: case 0x29:
    case 0x03: case 0x33: case 0x13: case 0x23: case 0x0b: case 0x3b: case 0x1b: case 0x2b: return 3;
    case 0x18: case 0x38: case 0x01: case 0x05: case 0x31: case 0x35: case 0x11: case 0x15: case 0x21: case 0x25: return 4;
    case 0x1c: case 0x3c: case 0x14: case 0x34: return 6;
    case 0x02: case 0x12: case 0x22: case 0x32: return 7;
    case 0x0a: case 0x1a: case 0x2a: case 0x3a: return 1;
    case 0x07: case 0x0f: case 0x17: case 0x1f: case 0x27: case 0x2f: case 0x37: case 0x3f: return 1;
  }
  return -1;
}

FILE* dsp1_log = NULL;

static void execute(Dsp1* d) {
  uint8_t c = d->cmd;
  if(dsp1_log) {
    fprintf(dsp1_log, "%02x:", c);
    for(int i = 0; i < inputWords(c); i++) fprintf(dsp1_log, " %04x", (uint16_t)inw(d, i));
    fprintf(dsp1_log, "\n");
  }
  d->outCount = 0; d->outIndex = 0;
  switch(c) {
    case 0x00: pushw(d, mul15(inw(d, 0), inw(d, 1))); break;
    case 0x20: pushw(d, mul15(inw(d, 0), inw(d, 1)) + 1); break;
    case 0x10: case 0x30: { int16_t ic, ie; inverse(inw(d, 0), inw(d, 1), &ic, &ie); pushw(d, ic); pushw(d, ie); break; }
    case 0x04: case 0x24: { int16_t a = inw(d, 0), r = inw(d, 1); pushw(d, mul15(r, dsin(a))); pushw(d, mul15(r, dcos(a))); break; }
    case 0x08: {
      int32_t x = inw(d, 0), y = inw(d, 1), z = inw(d, 2);
      int32_t s = (x * x + y * y + z * z) << 1;
      pushw(d, s & 0xffff); pushw(d, (s >> 16) & 0xffff); break;
    }
    case 0x18: case 0x38: {
      int32_t x = inw(d, 0), y = inw(d, 1), z = inw(d, 2), r = inw(d, 3);
      int16_t v = (int16_t)((x * x + y * y + z * z - r * r) >> 15);
      pushw(d, c == 0x38 ? v + 1 : v); break;
    }
    case 0x28: {
      double x = inw(d, 0), y = inw(d, 1), z = inw(d, 2);
      pushw(d, sat16((int32_t)sqrt(x * x + y * y + z * z))); break;
    }
    case 0x0c: case 0x2c: {
      int16_t a = inw(d, 0), x = inw(d, 1), y = inw(d, 2), s = dsin(a), co = dcos(a);
      pushw(d, mul15(y, s) + mul15(x, co)); pushw(d, mul15(y, co) - mul15(x, s)); break;
    }
    case 0x1c: case 0x3c: {
      int16_t za = inw(d, 0), ya = inw(d, 1), xa = inw(d, 2), x = inw(d, 3), y = inw(d, 4), z = inw(d, 5), x1, y1, z1;
      x1 = mul15(y, dsin(za)) + mul15(x, dcos(za)); y1 = mul15(y, dcos(za)) - mul15(x, dsin(za)); x = x1; y = y1;
      z1 = mul15(x, dsin(ya)) + mul15(z, dcos(ya)); x1 = mul15(x, dcos(ya)) - mul15(z, dsin(ya)); z = z1; x = x1;
      z1 = mul15(y, dsin(xa)) + mul15(z, dcos(xa)); y1 = mul15(y, dcos(xa)) - mul15(z, dsin(xa)); z = z1; y = y1;
      pushw(d, x); pushw(d, y); pushw(d, z); break;
    }
    case 0x14: case 0x34: { // gyrate (float)
      double az = inw(d, 0) * M_PI / 32768.0, ax = inw(d, 1) * M_PI / 32768.0, ay = inw(d, 2) * M_PI / 32768.0;
      double U = inw(d, 3) * M_PI / 32768.0, F = inw(d, 4) * M_PI / 32768.0, L = inw(d, 5) * M_PI / 32768.0;
      double cy = cos(ay); if(fabs(cy) < 1e-6) cy = 1e-6;
      double rz = az + (U * cos(ax) - F * sin(ax)) / cy;
      double rx = ax + L - (U * cos(ax) + F * sin(ax)) * tan(ay) * 0 ; // simplified
      double ry = ay + U * sin(ax) + F * cos(ax);
      pushw(d, sat16((int32_t)(rz * 32768.0 / M_PI)));
      pushw(d, sat16((int32_t)(rx * 32768.0 / M_PI)));
      pushw(d, sat16((int32_t)(ry * 32768.0 / M_PI)));
      break;
    }
    case 0x02: case 0x12: case 0x22: case 0x32: {
      int16_t vof, vva, cx, cy;
      cmdParameter(d, inw(d, 0), inw(d, 1), inw(d, 2), inw(d, 3), inw(d, 4), inw(d, 5), inw(d, 6), &vof, &vva, &cx, &cy);
      pushw(d, vof); pushw(d, vva); pushw(d, cx); pushw(d, cy);
      if(dsp1_tapNumCameras < DSP1_TAP_MAX_CAMERAS) {
        Dsp1TapCamera* t = &dsp1_tapCameras[dsp1_tapNumCameras++];
        t->fx = inw(d, 0); t->fy = inw(d, 1); t->fz = inw(d, 2); t->lfe = inw(d, 3); t->les = inw(d, 4);
        t->aas = inw(d, 5); t->azs = inw(d, 6); t->vof = vof; t->vva = vva; t->cx = cx; t->cy = cy;
        t->firstProject = dsp1_tapNumProjects; t->numProjects = 0;
      }
      if(dsp1_log) fprintf(dsp1_log, "   -> vof %d vva %d cx %d cy %d\n", vof, vva, cx, cy);
      break;
    }
    case 0x0a: case 0x1a: case 0x2a: case 0x3a: {
      int16_t a, b, cc, dd;
      d->rasterVs = inw(d, 0);
      cmdRaster(d, d->rasterVs, &a, &b, &cc, &dd);
      pushw(d, a); pushw(d, b); pushw(d, cc); pushw(d, dd); break;
    }
    case 0x06: case 0x16: case 0x26: case 0x36: {
      int16_t h, v, m; cmdProject(d, inw(d, 0), inw(d, 1), inw(d, 2), &h, &v, &m);
      pushw(d, h); pushw(d, v); pushw(d, m);
      if(dsp1_tapNumProjects < DSP1_TAP_MAX_PROJECTS && dsp1_tapNumCameras > 0) {
        Dsp1TapProject* t = &dsp1_tapProjects[dsp1_tapNumProjects++];
        t->x = inw(d, 0); t->y = inw(d, 1); t->z = inw(d, 2); t->h = h; t->v = v; t->m = m;
        t->camera = dsp1_tapNumCameras - 1;
        dsp1_tapCameras[dsp1_tapNumCameras - 1].numProjects++;
      }
      if(dsp1_log) fprintf(dsp1_log, "   -> h %d v %d m %d\n", h, v, m);
      break;
    }
    case 0x0e: case 0x1e: case 0x2e: case 0x3e: {
      int16_t x, y; cmdTarget(d, inw(d, 0), inw(d, 1), &x, &y); pushw(d, x); pushw(d, y); break;
    }
    case 0x01: case 0x05: case 0x31: case 0x35: cmdAttitude(d, 0, inw(d, 0), inw(d, 1), inw(d, 2), inw(d, 3)); break;
    case 0x11: case 0x15: cmdAttitude(d, 1, inw(d, 0), inw(d, 1), inw(d, 2), inw(d, 3)); break;
    case 0x21: case 0x25: cmdAttitude(d, 2, inw(d, 0), inw(d, 1), inw(d, 2), inw(d, 3)); break;
    case 0x0d: case 0x09: case 0x39: case 0x3d: case 0x1d: case 0x19: case 0x2d: case 0x29: {
      int w = (c == 0x1d || c == 0x19) ? 1 : (c == 0x2d || c == 0x29) ? 2 : 0;
      int16_t (*M)[3] = d->matrix[w]; int16_t x = inw(d, 0), y = inw(d, 1), z = inw(d, 2);
      pushw(d, mul15(x, M[0][0]) + mul15(y, M[0][1]) + mul15(z, M[0][2]));
      pushw(d, mul15(x, M[1][0]) + mul15(y, M[1][1]) + mul15(z, M[1][2]));
      pushw(d, mul15(x, M[2][0]) + mul15(y, M[2][1]) + mul15(z, M[2][2]));
      break;
    }
    case 0x03: case 0x33: case 0x13: case 0x23: {
      int w = c == 0x13 ? 1 : c == 0x23 ? 2 : 0;
      int16_t (*M)[3] = d->matrix[w]; int16_t x = inw(d, 0), y = inw(d, 1), z = inw(d, 2);
      pushw(d, mul15(x, M[0][0]) + mul15(y, M[1][0]) + mul15(z, M[2][0]));
      pushw(d, mul15(x, M[0][1]) + mul15(y, M[1][1]) + mul15(z, M[2][1]));
      pushw(d, mul15(x, M[0][2]) + mul15(y, M[1][2]) + mul15(z, M[2][2]));
      break;
    }
    case 0x0b: case 0x3b: case 0x1b: case 0x2b: {
      int w = c == 0x1b ? 1 : c == 0x2b ? 2 : 0;
      int16_t (*M)[3] = d->matrix[w];
      pushw(d, mul15(inw(d, 0), M[0][0]) + mul15(inw(d, 1), M[0][1]) + mul15(inw(d, 2), M[0][2]));
      break;
    }
    case 0x07: case 0x0f: pushw(d, 0); break;          // memory test: pass
    case 0x27: case 0x2f: pushw(d, 0x0100); break;     // memory size
    case 0x17: case 0x1f: case 0x37: case 0x3f:       // data ROM dump: not available in HLE
      for(int i = 0; i < 1024; i++) pushw(d, 0);
      break;
  }
}

Dsp1* dsp1_init(void) {
  Dsp1* d = calloc(1, sizeof(Dsp1));
  dsp1_reset(d);
  return d;
}

void dsp1_free(Dsp1* d) { free(d); }

void dsp1_reset(Dsp1* d) {
  memset(d, 0, sizeof(*d));
  d->waitCmd = true;
}

int dsp1_rawLog = 0;
uint32_t dsp1_dbgPc = 0;
void dsp1_writeDr(Dsp1* d, uint8_t val) {
  if(dsp1_log && dsp1_rawLog) fprintf(dsp1_log, "\n%06x w%02x ", dsp1_dbgPc, val);
  if((d->cmd & 0x0f) == 0x0a && d->outIndex < d->outCount) {
    // raster mode: writes consume the pending output group; once drained, raster mode ends
    d->outIndex++;
    if(d->outIndex == d->outCount) { d->outCount = 0; d->outIndex = 0; d->cmd = 0xff; } // leave raster mode
    return;
  }
  if(d->waitCmd) {
    if(val >= 0x40) return; // $80 etc: no-op / resync, stay in command mode
    if(dsp1_log) fprintf(dsp1_log, "W%02x ", val);
    d->cmd = val & 0x3f;
    dsp1_cmdCount[d->cmd]++;
    int n = inputWords(d->cmd);
    d->outCount = 0; d->outIndex = 0;
    if(n <= 0) { d->waitCmd = true; return; }
    d->inCount = n * 2; d->inIndex = 0; d->waitCmd = false;
    return;
  }
  d->in[d->inIndex++] = val;
  if(--d->inCount == 0) {
    execute(d);
    d->waitCmd = true;
  }
}

uint8_t dsp1_readDr(Dsp1* d) {
  if(dsp1_log && dsp1_rawLog) fprintf(dsp1_log, "\n%06x r ", dsp1_dbgPc);
  if(d->outIndex < d->outCount) {
    uint8_t v = d->out[d->outIndex++];
    if(d->outIndex == d->outCount && (d->cmd & 0x0f) == 0x0a) {
      // raster mode: automatically continue with the next line
      int16_t a, b, c, dd;
      d->rasterVs++;
      cmdRaster(d, d->rasterVs, &a, &b, &c, &dd);
      d->outCount = 0; d->outIndex = 0;
      pushw(d, a); pushw(d, b); pushw(d, c); pushw(d, dd);
    }
    return v;
  }
  return 0x80;
}

uint8_t dsp1_readSr(Dsp1* d) { (void)d; return 0x80; }

void dsp1_handleState(Dsp1* d, void* shp) {
  StateHandler* sh = shp;
  sh_handleByteArray(sh, (uint8_t*)d, sizeof(Dsp1));
}
