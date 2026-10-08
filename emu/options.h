// options: the Enhancements menu (opened from the title screen with SELECT, or anywhere with
// SELECT+START / Esc) and the settings it edits, saved to smkplay.cfg next to the executable.
#ifndef OPTIONS_H
#define OPTIONS_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  bool widescreen;      // 16:9 picture
  bool hd2d;            // HD-2D: Mode 7 views re-rendered as a 3D world (needs OpenGL 3.3)
  bool hd2dWalls;       // HD-2D: extrude walls from the track's surface data
  bool hd2dPost;        // HD-2D: tilt-shift, bloom, vignette
  bool hdMode7;         // 2D renderer: 2x2 samples per Mode 7 pixel
  bool recompiled;      // run the recompiled C (off = interpreter)
  bool fullscreen;
  int scale;            // window size multiplier, 2..6
} Options;

typedef struct {
  bool glAvailable;     // HD-2D possible
  bool recompAvailable; // build includes recompiled code
  bool netplay;         // in a netplay session (engine switch and pause are locked)
} MenuCaps;

enum { MENU_NONE = 0, MENU_CHANGED, MENU_CLOSED, MENU_QUIT };

void opt_defaults(Options* o);
bool opt_load(Options* o, const char* path);
bool opt_save(const Options* o, const char* path);

typedef struct Menu Menu;
Menu* menu_create(void);
void menu_free(Menu* m);
bool menu_isOpen(const Menu* m);
void menu_open(Menu* m);
void menu_close(Menu* m);
// Feed the combined controller state (SNES bit layout: 0 B, 1 Y, 2 Select, 3 Start, 4 Up, 5 Down,
// 6 Left, 7 Right, 8 A, 9 X, 10 L, 11 R) once per frame while open.
int menu_update(Menu* m, uint16_t pad, Options* o, const MenuCaps* caps);
// Draw into an RGBA (0xAABBGGRR) overlay of 256x224 virtual pixels. Clears it first.
void menu_draw(const Menu* m, const Options* o, const MenuCaps* caps, uint32_t* rgba);
// Title-screen hint ("SELECT: ENHANCEMENTS") into the same overlay format.
void menu_drawHint(uint32_t* rgba, int frame);

#define MENU_W 256
#define MENU_H 224

#endif
