// options.c: see options.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "options.h"
#include "font5x7.h"

// ------------------------------------------------------------------ settings file
void opt_defaults(Options* o) {
  memset(o, 0, sizeof(*o));
  o->hd2dWalls = true;
  o->hd2dPost = true;
  o->recompiled = true;
  o->scale = 3;
}

bool opt_load(Options* o, const char* path) {
  FILE* f = fopen(path, "r");
  if(!f) return false;
  char line[256];
  while(fgets(line, sizeof line, f)) {
    char key[64]; int v;
    if(sscanf(line, " %63[a-zA-Z0-9_] = %d", key, &v) != 2) continue;
    if(!strcmp(key, "widescreen")) o->widescreen = v;
    else if(!strcmp(key, "hd2d")) o->hd2d = v;
    else if(!strcmp(key, "hd2d_walls")) o->hd2dWalls = v;
    else if(!strcmp(key, "hd2d_post")) o->hd2dPost = v;
    else if(!strcmp(key, "hd_mode7")) o->hdMode7 = v;
    else if(!strcmp(key, "recompiled")) o->recompiled = v;
    else if(!strcmp(key, "fullscreen")) o->fullscreen = v;
    else if(!strcmp(key, "scale")) o->scale = v < 2 ? 2 : v > 6 ? 6 : v;
  }
  fclose(f);
  return true;
}

bool opt_save(const Options* o, const char* path) {
  FILE* f = fopen(path, "w");
  if(!f) return false;
  fprintf(f, "# smkplay settings (edited by the in-game Enhancements menu)\n");
  fprintf(f, "widescreen = %d\nhd2d = %d\nhd2d_walls = %d\nhd2d_post = %d\nhd_mode7 = %d\nrecompiled = %d\nfullscreen = %d\nscale = %d\n",
          o->widescreen, o->hd2d, o->hd2dWalls, o->hd2dPost, o->hdMode7, o->recompiled, o->fullscreen, o->scale);
  fclose(f);
  return true;
}

// ------------------------------------------------------------------ menu model
enum { IT_WIDE, IT_HD2D, IT_WALLS, IT_POST, IT_HDM7, IT_ENGINE, IT_FULL, IT_SCALE, IT_RESUME, IT_QUIT, IT_COUNT };

struct Menu {
  bool open;
  int sel;
  uint16_t prev;
  int repeat;           // auto-repeat counter for held Up/Down
};

Menu* menu_create(void) { return calloc(1, sizeof(Menu)); }
void menu_free(Menu* m) { free(m); }
bool menu_isOpen(const Menu* m) { return m->open; }
void menu_open(Menu* m) { m->open = true; m->sel = 0; m->prev = 0xffff; m->repeat = 0; }  // prev: ignore the opening press
void menu_close(Menu* m) { m->open = false; }

static bool itemEnabled(int it, const Options* o, const MenuCaps* c) {
  switch(it) {
    case IT_HD2D: return c->glAvailable;
    case IT_WALLS: case IT_POST: return c->glAvailable && o->hd2d;
    case IT_HDM7: return !o->hd2d;
    case IT_ENGINE: return c->recompAvailable && !c->netplay;
  }
  return true;
}

#define B_B 0x0001
#define B_Y 0x0002
#define B_SELECT 0x0004
#define B_START 0x0008
#define B_UP 0x0010
#define B_DOWN 0x0020
#define B_LEFT 0x0040
#define B_RIGHT 0x0080
#define B_A 0x0100
#define B_X 0x0200

int menu_update(Menu* m, uint16_t pad, Options* o, const MenuCaps* c) {
  if(!m->open) return MENU_NONE;
  uint16_t pressed = pad & ~m->prev;
  m->prev = pad;
  // held up/down repeat after ~1/3 s
  if(pad & (B_UP | B_DOWN)) { if(++m->repeat > 20 && (m->repeat % 6) == 0) pressed |= pad & (B_UP | B_DOWN); }
  else m->repeat = 0;
  if(pressed & (B_START | B_SELECT | B_X)) { m->open = false; return MENU_CLOSED; }
  if(pressed & B_UP) { do m->sel = (m->sel + IT_COUNT - 1) % IT_COUNT; while(!itemEnabled(m->sel, o, c)); }
  if(pressed & B_DOWN) { do m->sel = (m->sel + 1) % IT_COUNT; while(!itemEnabled(m->sel, o, c)); }
  bool toggle = pressed & (B_A | B_B | B_LEFT | B_RIGHT | B_Y);
  if(!toggle || !itemEnabled(m->sel, o, c)) return MENU_NONE;
  switch(m->sel) {
    case IT_WIDE: o->widescreen = !o->widescreen; if(!o->widescreen) o->hd2d = false; break;
    case IT_HD2D: o->hd2d = !o->hd2d; if(o->hd2d) o->widescreen = true; break;
    case IT_WALLS: o->hd2dWalls = !o->hd2dWalls; break;
    case IT_POST: o->hd2dPost = !o->hd2dPost; break;
    case IT_HDM7: o->hdMode7 = !o->hdMode7; break;
    case IT_ENGINE: o->recompiled = !o->recompiled; break;
    case IT_FULL: o->fullscreen = !o->fullscreen; break;
    case IT_SCALE:
      if(pressed & B_LEFT) o->scale = o->scale <= 2 ? 6 : o->scale - 1;
      else o->scale = o->scale >= 6 ? 2 : o->scale + 1;
      break;
    case IT_RESUME: m->open = false; return MENU_CLOSED;
    case IT_QUIT: if(pressed & (B_A | B_B)) return MENU_QUIT; return MENU_NONE;
  }
  return MENU_CHANGED;
}

// ------------------------------------------------------------------ drawing
#define RGBA(r, g, b, a) ((uint32_t)(r) | (uint32_t)(g) << 8 | (uint32_t)(b) << 16 | (uint32_t)(a) << 24)

static void fill(uint32_t* p, int x, int y, int w, int h, uint32_t c) {
  for(int j = y; j < y + h; j++) for(int i = x; i < x + w; i++)
    if(i >= 0 && j >= 0 && i < MENU_W && j < MENU_H) p[j * MENU_W + i] = c;
}

static int text(uint32_t* p, int x, int y, const char* s, uint32_t c, bool shadow) {
  for(; *s; s++, x += 6) {
    unsigned ch = (unsigned char)*s;
    if(ch < 32 || ch > 127) ch = '?';
    const uint8_t* g = font5x7[ch - 32];
    for(int r = 0; r < 7; r++) for(int b = 0; b < 5; b++) if(g[r] & (0x10 >> b)) {
      if(shadow) fill(p, x + b + 1, y + r + 1, 1, 1, RGBA(0, 0, 0, 255));
      fill(p, x + b, y + r, 1, 1, c);
    }
  }
  return x;
}

void menu_draw(const Menu* m, const Options* o, const MenuCaps* c, uint32_t* p) {
  memset(p, 0, MENU_W * MENU_H * 4);
  if(!m->open) return;
  const int px = 14, py = 20, pw = MENU_W - 28, ph = 184;
  fill(p, px, py, pw, ph, RGBA(12, 16, 40, 225));
  fill(p, px, py, pw, 1, RGBA(255, 210, 60, 255)); fill(p, px, py + ph - 1, pw, 1, RGBA(255, 210, 60, 255));
  fill(p, px, py, 1, ph, RGBA(255, 210, 60, 255)); fill(p, px + pw - 1, py, 1, ph, RGBA(255, 210, 60, 255));
  text(p, px + (pw - 12 * 6) / 2, py + 7, "ENHANCEMENTS", RGBA(255, 210, 60, 255), true);
  static const char* names[IT_COUNT] = {
    "WIDESCREEN 16:9", "HD-2D 3D WORLD", "  3D WALLS", "  TILT-SHIFT + BLOOM", "HD MODE 7 (2D)",
    "ENGINE", "FULLSCREEN", "WINDOW SIZE", "RESUME", "QUIT GAME"
  };
  for(int i = 0; i < IT_COUNT; i++) {
    int y = py + 24 + i * 14 + (i >= IT_RESUME ? 6 : 0);
    bool en = itemEnabled(i, o, c);
    bool sel = i == m->sel;
    if(sel) fill(p, px + 4, y - 3, pw - 8, 13, RGBA(60, 80, 160, 255));
    uint32_t col = !en ? RGBA(110, 110, 120, 255) : sel ? RGBA(255, 255, 255, 255) : RGBA(200, 210, 230, 255);
    text(p, px + 8, y, sel ? ">" : " ", RGBA(255, 210, 60, 255), true);
    text(p, px + 16, y, names[i], col, true);
    const char* val = NULL; bool on = false; char buf[16];
    switch(i) {
      case IT_WIDE: on = o->widescreen; val = on ? "ON" : "OFF"; break;
      case IT_HD2D: on = o->hd2d; val = !c->glAvailable ? "N/A" : on ? "ON" : "OFF"; break;
      case IT_WALLS: on = o->hd2dWalls; val = on ? "ON" : "OFF"; break;
      case IT_POST: on = o->hd2dPost; val = on ? "ON" : "OFF"; break;
      case IT_HDM7: on = o->hdMode7; val = on ? "ON" : "OFF"; break;
      case IT_ENGINE: on = true; val = !c->recompAvailable ? "INTERP" : o->recompiled ? "RECOMP" : "INTERP"; break;
      case IT_FULL: on = o->fullscreen; val = on ? "ON" : "OFF"; break;
      case IT_SCALE: on = true; snprintf(buf, sizeof buf, "< X%d >", o->scale); val = buf; break;
    }
    if(val) {
      uint32_t vc = !en ? RGBA(110, 110, 120, 255) : on ? RGBA(120, 240, 120, 255) : RGBA(240, 120, 110, 255);
      text(p, px + pw - 8 - (int)strlen(val) * 6, y, val, vc, true);
    }
  }
  const char* help = c->netplay ? "NETPLAY: GAME KEEPS RUNNING" : "A/B: CHANGE   START: CLOSE";
  text(p, px + (pw - (int)strlen(help) * 6) / 2, py + ph - 12, help, RGBA(170, 180, 210, 255), true);
}

void menu_drawHint(uint32_t* p, int frame) {
  memset(p, 0, MENU_W * MENU_H * 4);
  if((frame / 40) % 4 == 3) return;               // gentle blink
  const char* s = "SELECT: ENHANCEMENTS";
  int w = (int)strlen(s) * 6 + 8, x = (MENU_W - w) / 2, y = 210;
  fill(p, x, y - 2, w, 11, RGBA(12, 16, 40, 170));
  text(p, x + 4, y, s, RGBA(255, 230, 120, 255), true);
}
