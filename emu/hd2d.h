// hd2d: HD-2D renderer (phase 1). Re-renders the Mode 7 floor of every view as a real 3D plane,
// using the camera the game hands to the DSP-1, at the window's native resolution, and composites
// the original sprites/HUD/sky on top. Render-only: never writes game state.
#ifndef HD2D_H
#define HD2D_H
#include <stdbool.h>
#include <stdint.h>
#include "snes.h"

typedef struct Hd2d Hd2d;

// Call with a current OpenGL 3.3 core context. Returns NULL (and prints why) if GL setup fails.
Hd2d* hd2d_init(void* (*getProc)(const char*));
// After snes_runFrame: pull this frame's data (PPU taps, DSP-1 camera taps, VRAM/CGRAM).
void hd2d_capture(Hd2d* h, Snes* snes);
// Draw into the current framebuffer (window drawable size w x h).
void hd2d_render(Hd2d* h, int w, int hgt);
// Options
void hd2d_setEnabled3D(Hd2d* h, bool on);     // off = draw the classic picture through the same path
bool hd2d_enabled3D(Hd2d* h);
void hd2d_setDebug(Hd2d* h, bool on);         // tint the 3D floor so it's visible what got replaced
void hd2d_setPost(Hd2d* h, bool on);       // HD-2D post-processing (tilt-shift, bloom, vignette)
bool hd2d_post(Hd2d* h);
void hd2d_setWalls(Hd2d* h, bool on);      // extruded walls on/off
// Menu/hint overlay: 256x224 RGBA (0xAABBGGRR) drawn over the 4:3 middle of the picture; NULL = none
void hd2d_setOverlay(Hd2d* h, const uint32_t* rgba);
void hd2d_free(Hd2d* h);

// Stats for the HUD/title bar
int hd2d_lastViews(Hd2d* h);                  // how many Mode 7 views were re-rendered last frame

#endif
