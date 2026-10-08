#ifndef DSP1_H
#define DSP1_H

// NEC uPD77C25 "DSP-1" math coprocessor, high-level emulation.
// Super Mario Kart (HiROM) maps it at banks 00-1F/80-9F: $6000-$6FFF = DR, $7000-$7FFF = SR.
//
// This is a clean-room HLE written from the publicly documented command set.
// The math is fixed-point where the algorithm is well known and float-assisted
// elsewhere, so results can differ from real hardware by an LSB or two.
// An exact LLE path (uPD7725 core + user-supplied dsp1b.rom) is on the roadmap.

#include <stdint.h>
#include <stdbool.h>

typedef struct Dsp1 Dsp1;

Dsp1* dsp1_init(void);
void dsp1_free(Dsp1* d);
void dsp1_reset(Dsp1* d);
uint8_t dsp1_readDr(Dsp1* d);
void dsp1_writeDr(Dsp1* d, uint8_t val);
uint8_t dsp1_readSr(Dsp1* d);
void dsp1_handleState(Dsp1* d, void* sh); // StateHandler*

// usage statistics: how many times each command (0x00-0x3f) was issued
extern uint32_t dsp1_cmdCount[64];
#include <stdio.h>
extern FILE* dsp1_log; // optional command log

// HD-2D taps (render-only): every Parameter (camera) and Project call of the current frame,
// in call order. The frontend clears them after each frame with dsp1_tapReset().
typedef struct {
  int16_t fx, fy, fz, lfe, les, aas, azs;   // inputs (world units = 4 x Mode 7 texels)
  int16_t vof, vva, cx, cy;                  // outputs
  int firstProject, numProjects;             // range in dsp1_tapProjects belonging to this camera
} Dsp1TapCamera;
typedef struct { int16_t x, y, z, h, v, m; int camera; } Dsp1TapProject;
#define DSP1_TAP_MAX_CAMERAS 8
#define DSP1_TAP_MAX_PROJECTS 128
extern Dsp1TapCamera dsp1_tapCameras[DSP1_TAP_MAX_CAMERAS];
extern Dsp1TapProject dsp1_tapProjects[DSP1_TAP_MAX_PROJECTS];
extern int dsp1_tapNumCameras, dsp1_tapNumProjects;
void dsp1_tapReset(void);

#endif
