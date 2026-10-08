// smkplay: plays Super Mario Kart on PC (SDL2). This is the Phase-1 runtime:
// the original 65816 code runs on an interpreter core with the HLE DSP-1.
// Phase 2 swaps the interpreter for recompiled C while keeping this frontend.
//
//   smkplay [rom.sfc] [--host [port]] [--join host[:port]] [--delay frames] [--interp] [--nogl]
//     rom defaults to smk.sfc next to the executable
//     --host / --join   2-player netplay over UDP (default port 7845); host = player 1
//     --delay N         netplay input delay in frames (host decides, default 3)
//     --wide            start in widescreen (16:9, toggle with F2)
//     --hd2d            HD-2D renderer (OpenGL 3.3): Mode 7 floors re-rendered in 3D at full resolution
//                       (F3 = 3D on/off, F4 = tint re-rendered geometry, F6 = HD-2D post effects on/off)
//     --hd              start with HD Mode 7 (2x2 samples per floor pixel; toggle with H)
//     --interp          run the original 65816 code on the interpreter instead of the recompiled C
//     --nogl            don't use OpenGL (classic SDL renderer; HD-2D unavailable)
//
// ENHANCEMENTS MENU: press SELECT on the title screen, SELECT+START anywhere, or Esc. Every option
// can be toggled with the controller; settings are saved to smkplay.cfg next to the executable.
// Command-line flags override the saved settings for that session.
//
// Keyboard (player 1): arrows = D-pad, Z = B (gas), X = A (item), A = Y (brake), S = X (rear view),
//   Q = L, W = R (hop), Enter = Start, RShift = Select.
// Gamepads: first two controllers are players 1 and 2 (SNES layout by button position).
// F1 reset, F5 save state, F9 load state, Tab fast-forward (hold), P pause,
// F11 or Alt+Enter fullscreen, 1-6 window scale, H HD Mode 7, F2 widescreen, F3 HD-2D, F6 HD-2D effects, Esc menu.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#include "snes.h"
#include "ppu.h"
#include "netplay.h"
#include "hd2d.h"
#include "dsp1.h"
#include "options.h"
#ifdef WITH_RECOMP
#include "recomp.h"
#endif

#define SMK_CRC32 0xCD80DB86u

static Snes* snes;
static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* texture;
static SDL_Texture* textureWide;
static Hd2d* hd2d;                 // HD-2D renderer (--hd2d): OpenGL presentation path
static SDL_GLContext glctx;
static void runFrameTapped(void);
static int scaleNow = 3;

static SDL_AudioDeviceID audioDev;
static int16_t audioBuf[48000 / 50 * 2];
static int audioFreq = 48000, samplesPerFrame = 800;
static char sramPath[1024], statePath[1024];
static uint8_t lastSram[0x800];
static SDL_GameController* pads[2];

static uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xffffffffu;
  for(size_t i = 0; i < n; i++) {
    c ^= d[i];
    for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1));
  }
  return ~c;
}

static uint8_t* readFile(const char* path, int* len) {
  FILE* f = fopen(path, "rb");
  if(!f) return NULL;
  fseek(f, 0, SEEK_END); *len = (int)ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* d = malloc(*len > 0 ? *len : 1);
  if(fread(d, 1, *len, f) != (size_t)*len) { free(d); d = NULL; }
  fclose(f);
  return d;
}

static void writeFile(const char* path, const void* d, int len) {
  FILE* f = fopen(path, "wb");
  if(!f) { fprintf(stderr, "cannot write %s\n", path); return; }
  fwrite(d, 1, len, f); fclose(f);
}

static void saveSram(bool force) {
  uint8_t buf[0x800];
  int n = snes_saveBattery(snes, NULL);
  if(n <= 0 || n > (int)sizeof buf) return;
  snes_saveBattery(snes, buf);
  if(!force && !memcmp(buf, lastSram, n)) return;
  memcpy(lastSram, buf, n);
  writeFile(sramPath, buf, n);
}

static uint16_t kbPad, gpPad[2];   // current local controller state (bit = SNES button index)
static void setButton(int player, int bit, bool on) {
  uint16_t* p = player == 0 ? &kbPad : &gpPad[player - 1];
  if(on) *p |= 1 << bit; else *p &= ~(1 << bit);
}
static void applyPads(uint16_t p1, uint16_t p2) {
  for(int i = 0; i < 12; i++) { snes_setButtonState(snes, 1, i, (p1 >> i) & 1); snes_setButtonState(snes, 2, i, (p2 >> i) & 1); }
}
static uint64_t wramHash(void) {
  uint64_t h = 0xcbf29ce484222325ull;
  for(int i = 0; i < 0x20000; i++) { h ^= snes->ram[i]; h *= 0x100000001b3ull; }
  return h;
}

static void handleKey(SDL_Keycode k, bool down) {
  switch(k) {
    case SDLK_z: setButton(0, 0, down); break;       // B
    case SDLK_a: setButton(0, 1, down); break;       // Y
    case SDLK_RSHIFT: setButton(0, 2, down); break;  // Select
    case SDLK_RETURN: setButton(0, 3, down); break;  // Start
    case SDLK_UP: setButton(0, 4, down); break;
    case SDLK_DOWN: setButton(0, 5, down); break;
    case SDLK_LEFT: setButton(0, 6, down); break;
    case SDLK_RIGHT: setButton(0, 7, down); break;
    case SDLK_x: setButton(0, 8, down); break;       // A
    case SDLK_s: setButton(0, 9, down); break;       // X
    case SDLK_q: setButton(0, 10, down); break;      // L
    case SDLK_w: setButton(0, 11, down); break;      // R
  }
}

static void handlePadButton(SDL_JoystickID id, int b, bool down) {
  int player = 0;
  for(int i = 0; i < 2; i++)
    if(pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) player = i + 1;
  if(!player) return;
  int bit = -1;
  switch(b) {
    case SDL_CONTROLLER_BUTTON_A: bit = 0; break;              // bottom = B
    case SDL_CONTROLLER_BUTTON_X: bit = 1; break;              // left = Y
    case SDL_CONTROLLER_BUTTON_BACK: bit = 2; break;
    case SDL_CONTROLLER_BUTTON_START: bit = 3; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: bit = 4; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: bit = 5; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: bit = 6; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = 7; break;
    case SDL_CONTROLLER_BUTTON_B: bit = 8; break;              // right = A
    case SDL_CONTROLLER_BUTTON_Y: bit = 9; break;              // top = X
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: bit = 10; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: bit = 11; break;
  }
  if(bit >= 0) setButton(player, bit, down);   // gamepad n -> gpPad[n-1]
}

static void openPads(void) {
  int n = 0;
  for(int i = 0; i < SDL_NumJoysticks() && n < 2; i++) {
    if(SDL_IsGameController(i)) {
      pads[n] = SDL_GameControllerOpen(i);
      if(pads[n]) printf("Controller %d: %s\n", n + 1, SDL_GameControllerName(pads[n]));
      n++;
    }
  }
}

static void runFrameTapped(void) { dsp1_tapReset(); snes_runFrame(snes); }

// ------------------------------------------------------------------ options
static Options opt;
static MenuCaps caps;
static char cfgPath[1024];
static SDL_Texture* overlayTex;          // SDL-renderer path: menu overlay
static uint32_t overlay[MENU_W * MENU_H];

static void applyOptions(bool resizeWindow) {
  if(!hd2d) opt.hd2d = false;
  if(opt.hd2d) opt.widescreen = true;      // the 3D world is drawn 16:9
  bool ws = opt.widescreen;
  snes->ppu->widescreen = ws;
  snes->ppu->hd2dTaps = opt.hd2d;
  snes->ppu->hdMode7 = opt.hdMode7 && !opt.hd2d;
  if(hd2d) { hd2d_setEnabled3D(hd2d, opt.hd2d); hd2d_setWalls(hd2d, opt.hd2dWalls); hd2d_setPost(hd2d, opt.hd2dPost); }
#ifdef WITH_RECOMP
  if(!caps.netplay) snes->cpu->useRecomp = opt.recompiled;
#endif
  if(renderer) SDL_RenderSetLogicalSize(renderer, ws ? PPU_WS_WIDTH * 2 : 512, 448);
  bool isFull = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
  if(opt.fullscreen != isFull) SDL_SetWindowFullscreen(window, opt.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if(resizeWindow && !opt.fullscreen) SDL_SetWindowSize(window, (ws ? PPU_WS_WIDTH : 256) * opt.scale, 224 * opt.scale);
}

static bool createGL(int w, int h) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  window = SDL_CreateWindow("Super Mario Kart", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL);
  if(!window) return false;
  glctx = SDL_GL_CreateContext(window);
  if(glctx) { SDL_GL_SetSwapInterval(1); hd2d = hd2d_init(SDL_GL_GetProcAddress); }
  if(hd2d) return true;
  fprintf(stderr, "OpenGL 3.3 not available (%s): using the classic renderer, HD-2D disabled\n", SDL_GetError());
  if(glctx) SDL_GL_DeleteContext(glctx);
  glctx = NULL;
  SDL_DestroyWindow(window); window = NULL;
  return false;
}

static void createClassic(int w, int h) {
  window = SDL_CreateWindow("Super Mario Kart", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if(!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_RenderSetLogicalSize(renderer, 512, 448);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);
  textureWide = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, PPU_WS_WIDTH * 2, 480);
  overlayTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, MENU_W, MENU_H);
  SDL_SetTextureBlendMode(overlayTex, SDL_BLENDMODE_BLEND);
}

int main(int argc, char** argv) {
  const char* romPath = NULL;
  const char* joinHost = NULL;
  int hostPort = 0, joinPort = 7845, delay = 3;
  bool interp = false, hdStart = false, wideStart = false, hd2dMode = false, noGL = false;
  char hostBuf[256];
  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "--host")) { hostPort = (i + 1 < argc && argv[i + 1][0] != '-' && atoi(argv[i + 1])) ? atoi(argv[++i]) : 7845; }
    else if(!strcmp(argv[i], "--join") && i + 1 < argc) {
      snprintf(hostBuf, sizeof hostBuf, "%s", argv[++i]);
      char* colon = strrchr(hostBuf, ':');
      if(colon) { *colon = 0; joinPort = atoi(colon + 1); }
      joinHost = hostBuf;
    }
    else if(!strcmp(argv[i], "--delay") && i + 1 < argc) delay = atoi(argv[++i]);
    else if(!strcmp(argv[i], "--interp")) interp = true;
    else if(!strcmp(argv[i], "--hd")) hdStart = true;
    else if(!strcmp(argv[i], "--wide")) wideStart = true;
    else if(!strcmp(argv[i], "--hd2d")) hd2dMode = true;
    else if(!strcmp(argv[i], "--nogl")) noGL = true;
    else romPath = argv[i];
  }
  char* basePath = SDL_GetBasePath();
  char defPath[1024];
  if(!romPath) {
    snprintf(defPath, sizeof defPath, "%s%s", basePath ? basePath : "", "smk.sfc");
    romPath = defPath;
  }
  snprintf(cfgPath, sizeof cfgPath, "%s%s", getenv("SMKPLAY_CFG") ? "" : (basePath ? basePath : ""),
           getenv("SMKPLAY_CFG") ? getenv("SMKPLAY_CFG") : "smkplay.cfg");
  if(basePath) SDL_free(basePath);
  opt_defaults(&opt);
  opt_load(&opt, cfgPath);
  if(interp) opt.recompiled = false;
  if(hdStart) opt.hdMode7 = true;
  if(wideStart) opt.widescreen = true;
  if(hd2dMode) opt.hd2d = true;

  int len = 0;
  uint8_t* rom = readFile(romPath, &len);
  if(!rom) {
    fprintf(stderr, "Could not open ROM '%s'. Put your Super Mario Kart (USA) ROM next to smkplay as smk.sfc,\n"
                    "or pass its path: smkplay path/to/rom.sfc\n", romPath);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "smkplay",
      "Could not find smk.sfc.\nPlace your Super Mario Kart (USA) ROM next to smkplay.exe as smk.sfc,\n"
      "or drag the ROM onto smkplay.exe.", NULL);
    return 1;
  }
  uint32_t crc = crc32(rom, len);
  if(crc != SMK_CRC32) fprintf(stderr, "warning: ROM CRC32 is %08X, expected %08X (Super Mario Kart USA)\n", crc, SMK_CRC32);

  snprintf(sramPath, sizeof sramPath, "%s", romPath);
  char* dot = strrchr(sramPath, '.');
  if(dot) *dot = 0;
  snprintf(statePath, sizeof statePath, "%s.state", sramPath);
  strncat(sramPath, ".srm", sizeof sramPath - strlen(sramPath) - 1);

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  bool wsStart = opt.widescreen || opt.hd2d;
  int ww = (wsStart ? PPU_WS_WIDTH : 256) * opt.scale, wh = 224 * opt.scale;
  if(noGL || getenv("SMKPLAY_NOGL") || !createGL(ww, wh)) createClassic(ww, wh);
  if(hd2d && getenv("SMKPLAY_NOPOST")) opt.hd2dPost = false;

  SDL_AudioSpec want = {0}, have;
  want.freq = audioFreq; want.format = AUDIO_S16; want.channels = 2; want.samples = 1024;
  audioDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if(audioDev) SDL_PauseAudioDevice(audioDev, 0);
  samplesPerFrame = audioFreq / 60;

  snes = snes_init();
  if(!snes_loadRom(snes, rom, len)) { fprintf(stderr, "ROM load failed\n"); return 1; }
  free(rom);
  int sl = 0;
  uint8_t* sram = readFile(sramPath, &sl);
  if(sram) { snes_loadBattery(snes, sram, sl); memcpy(lastSram, sram, sl < 0x800 ? sl : 0x800); free(sram); }
  caps.glAvailable = hd2d != NULL;
#ifdef WITH_RECOMP
  recomp_install();
  caps.recompAvailable = true;
#else
  opt.recompiled = false;
#endif
  caps.netplay = hostPort || joinHost;
  applyOptions(false);
  // test hooks (used by the automated checks)
  if(getenv("SMKPLAY_STATE")) {
    int n; uint8_t* d = readFile(getenv("SMKPLAY_STATE"), &n);
    if(!d || !snes_loadState(snes, d, n)) fprintf(stderr, "could not load %s\n", getenv("SMKPLAY_STATE"));
    free(d);
  }
  uint16_t testHold = getenv("SMKPLAY_HOLD") ? (uint16_t)strtol(getenv("SMKPLAY_HOLD"), NULL, 0) : 0;
  // SMKPLAY_MENUKEYS="frame:bits,frame:bits,...": menu-only controller presses for automated checks
  const char* menuKeys = getenv("SMKPLAY_MENUKEYS");
  openPads();
  Menu* menu = menu_create();
  if(getenv("SMKPLAY_MENU")) menu_open(menu);

  // ---- netplay setup: both sides power-cycle with the host's SRAM so they start identical
  Netplay* net = NULL;
  if(hostPort || joinHost) {
    uint8_t sbuf[0x800] = {0};
    int n = snes_saveBattery(snes, sbuf);
    char err[256] = "";
    char title[128];
    snprintf(title, sizeof title, hostPort ? "Super Mario Kart - waiting for player 2 (UDP %d)..." : "Super Mario Kart - connecting...", hostPort);
    SDL_SetWindowTitle(window, title);
    if(renderer) { SDL_RenderClear(renderer); SDL_RenderPresent(renderer); }
    net = hostPort ? net_host(hostPort, delay, crc, sbuf, n, 0, err, sizeof err)
                   : net_join(joinHost, joinPort, crc, sbuf, n, 15000, err, sizeof err);
    if(!net) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "smkplay netplay", err, window);
      return 1;
    }
    snes_loadBattery(snes, sbuf, n);
    snes_reset(snes, true);
    snprintf(title, sizeof title, "Super Mario Kart - netplay %s (player %d, delay %d)", net_isHost(net) ? "host" : "client",
             net_isHost(net) ? 1 : 2, net_delay(net));
    SDL_SetWindowTitle(window, title);
  }
  bool desyncShown = false;

  bool running = true, paused = false, ff = false;
  uint64_t freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
  double acc = 0, frameTime = 1.0 / 60.0988;
  uint32_t frames = 0, uiFrames = 0;
  uint16_t prevLocal = 0;
  bool inputLock = false;          // after closing the menu, keep the game's pads neutral until released
  int testFrames = getenv("SMKPLAY_TEST") ? atoi(getenv("SMKPLAY_TEST")) : 0;
  // test hook: SMKPLAY_TESTINPUT=<seed> drives the local pad with a pseudo-random pattern
  uint32_t testSeed = getenv("SMKPLAY_TESTINPUT") ? (uint32_t)atoi(getenv("SMKPLAY_TESTINPUT")) : 0;
  while(running) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          bool down = e.type == SDL_KEYDOWN;
          SDL_Keycode k = e.key.keysym.sym;
          if(down && !e.key.repeat) {
            bool changed = false, resize = false;
            if(k == SDLK_ESCAPE) { if(menu_isOpen(menu)) { menu_close(menu); opt_save(&opt, cfgPath); } else menu_open(menu); }
            else if(net && (k == SDLK_p || k == SDLK_F1 || k == SDLK_F5 || k == SDLK_F9 || k == SDLK_TAB)) { /* off in netplay */ }
            else if(k == SDLK_h) { opt.hdMode7 = !opt.hdMode7; changed = true; }
            else if(k == SDLK_F2) { opt.widescreen = !opt.widescreen; if(!opt.widescreen) opt.hd2d = false; changed = resize = true; }
            else if(k == SDLK_F3 && hd2d) { opt.hd2d = !opt.hd2d; if(opt.hd2d) opt.widescreen = true; changed = resize = true; }
            else if(k == SDLK_F4 && hd2d) { static bool dbg; dbg = !dbg; hd2d_setDebug(hd2d, dbg); }
            else if(k == SDLK_F6 && hd2d) { opt.hd2dPost = !opt.hd2dPost; changed = true; }
            else if(k == SDLK_p) paused = !paused;
            else if(k == SDLK_F1) snes_reset(snes, false);
            else if(k == SDLK_F5) {
              int n = snes_saveState(snes, NULL); uint8_t* d = malloc(n);
              snes_saveState(snes, d); writeFile(statePath, d, n); free(d); printf("state saved\n");
            } else if(k == SDLK_F9) {
              int n; uint8_t* d = readFile(statePath, &n);
              if(d && snes_loadState(snes, d, n)) printf("state loaded\n");
              free(d);
            } else if(k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
              opt.fullscreen = !opt.fullscreen; changed = true;
            } else if(k >= SDLK_1 && k <= SDLK_6 && !opt.fullscreen && !menu_isOpen(menu)) {
              opt.scale = k - SDLK_0 < 2 ? 2 : k - SDLK_0; changed = resize = true;
            }
            if(changed) { applyOptions(resize); opt_save(&opt, cfgPath); }
            if(k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) break;
          }
          if(k == SDLK_TAB && !net) ff = down;
          handleKey(k, down);
          break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          handlePadButton(e.cbutton.which, e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
          break;
        case SDL_CONTROLLERDEVICEADDED:
          if(!pads[0] || !pads[1]) { for(int i = 0; i < 2; i++) if(pads[i]) SDL_GameControllerClose(pads[i]); pads[0] = pads[1] = NULL; openPads(); }
          break;
        case SDL_DROPFILE: SDL_free(e.drop.file); break;
      }
    }
    uint64_t now = SDL_GetPerformanceCounter();
    acc += (double)(now - last) / freq; last = now;
    if(acc > 0.25) acc = 0.25;
    while(acc >= frameTime - 0.002) {
      acc -= frameTime;
      uiFrames++;
      // ---- Enhancements menu: SELECT on the title screen, SELECT+START anywhere; controller-driven
      uint16_t local = kbPad | gpPad[0] | gpPad[1];
      if(menuKeys) {                                   // test hook
        const char* q = menuKeys; local = 0;
        while(q && *q) { unsigned f = 0, b = 0; if(sscanf(q, "%u:%i", &f, &b) == 2 && f == uiFrames) local = (uint16_t)b; q = strchr(q, ','); if(q) q++; }
      }
      uint16_t pressedNow = local & ~prevLocal;
      prevLocal = local;
      bool onTitle = snes->ram[0x36] == 0x04;
      if(!menu_isOpen(menu)) {
        if(((pressedNow & 0x0004) && onTitle) || ((local & 0x000c) == 0x000c && (pressedNow & 0x000c))) menu_open(menu);
      } else {
        int r = menu_update(menu, local, &opt, &caps);
        if(r == MENU_CHANGED) { applyOptions(true); opt_save(&opt, cfgPath); }
        else if(r == MENU_CLOSED) { opt_save(&opt, cfgPath); inputLock = true; }
        else if(r == MENU_QUIT) running = false;
      }
      bool menuOpen = menu_isOpen(menu);
      if(inputLock && !local) inputLock = false;
      bool muted = menuOpen || inputLock;
      if(paused || (menuOpen && !net)) continue;      // the game waits while the menu is open (not in netplay)
      if(net) {
        uint16_t p1, p2;
        uint16_t mine = muted ? 0 : (kbPad | gpPad[0]);
        if(testSeed) { uint32_t f = net_frameNo(net) / 20; uint32_t x = (f + 1) * 2654435761u ^ testSeed * 40503u; mine = (x >> 7) & 0x0ff3; }
        int r = net_frame(net, mine, &p1, &p2);
        if(r < 0) {
          SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "smkplay netplay", "The other player disconnected.", window);
          net_close(net); net = NULL; caps.netplay = false; SDL_SetWindowTitle(window, "Super Mario Kart (offline)");
          continue;
        }
        if(r == 0) { acc = 0; break; }       // waiting for the other side's input
        applyPads(p1, p2);
        runFrameTapped();
        if(testFrames && net_frameNo(net) == (uint32_t)testFrames - 60)
          printf("netplay: test checkpoint frame %u hash %016llx\n", net_frameNo(net), (unsigned long long)wramHash());
        if(((net_frameNo(net) - 1) % 60) == 0 && !net_checkHash(net, wramHash()) && !desyncShown) {
          desyncShown = true;
          printf("netplay: DESYNC detected at frame %u\n", net_frameNo(net) - 1);
          SDL_SetWindowTitle(window, "Super Mario Kart - netplay DESYNC (restart both sides)");
        }
      } else {
        if(muted) applyPads(testHold, 0); else applyPads(kbPad | gpPad[0] | testHold, gpPad[1]);
        int n = ff ? 4 : 1;
        for(int i = 0; i < n; i++) runFrameTapped();
      }
      if(audioDev && !ff) {
        snes_setSamples(snes, audioBuf, samplesPerFrame);
        if(SDL_GetQueuedAudioSize(audioDev) <= (uint32_t)samplesPerFrame * 4 * 6)
          SDL_QueueAudio(audioDev, audioBuf, samplesPerFrame * 4);
      }
      void* px; int pitch;
      if(hd2d) hd2d_capture(hd2d, snes);
      else if(snes->ppu->widescreen) {
        if(SDL_LockTexture(textureWide, NULL, &px, &pitch) == 0) { snes_setPixelsWide(snes, px); SDL_UnlockTexture(textureWide); }
      } else if(SDL_LockTexture(texture, NULL, &px, &pitch) == 0) { snes_setPixels(snes, px); SDL_UnlockTexture(texture); }
      if(++frames % 300 == 0 && !hostPort && !joinHost) saveSram(false);
      if(testFrames && (int)frames >= testFrames) running = false;
    }
    if(testFrames && menu_isOpen(menu) && (int)uiFrames >= testFrames) running = false;   // test hook with the menu open
    // ---- overlay: the menu, or the title-screen hint
    bool overlayOn = true;
    if(menu_isOpen(menu)) menu_draw(menu, &opt, &caps, overlay);
    else if(snes->ram[0x36] == 0x04) menu_drawHint(overlay, uiFrames);
    else overlayOn = false;
    if(hd2d) {
      hd2d_setOverlay(hd2d, overlayOn ? overlay : NULL);
      int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
      hd2d_render(hd2d, dw, dh);
      if(!running && getenv("SMKPLAY_SHOT")) {      // test hook: save the final frame
        uint8_t* buf = malloc((size_t)dw * dh * 3);
        void (*rp)(int, int, int, int, unsigned, unsigned, void*) = SDL_GL_GetProcAddress("glReadPixels");
        rp(0, 0, dw, dh, 0x1907 /*GL_RGB*/, 0x1401, buf);
        FILE* f = fopen(getenv("SMKPLAY_SHOT"), "wb");
        if(f) { fprintf(f, "P6\n%d %d\n255\n", dw, dh); for(int y = dh - 1; y >= 0; y--) fwrite(buf + (size_t)y * dw * 3, 1, (size_t)dw * 3, f); fclose(f); }
        free(buf);
      }
      SDL_GL_SwapWindow(window);
      continue;
    }
    bool wsNow = snes->ppu->widescreen;
    SDL_Rect src = {0, 16, wsNow ? PPU_WS_WIDTH * 2 : 512, 448};
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, wsNow ? textureWide : texture, &src, NULL);
    if(overlayOn) {
      SDL_UpdateTexture(overlayTex, NULL, overlay, MENU_W * 4);
      SDL_Rect dst = {wsNow ? PPU_WS_EXT * 2 : 0, 0, 512, 448};
      SDL_RenderCopy(renderer, overlayTex, NULL, &dst);
    }
    if(!running && getenv("SMKPLAY_SHOT")) {        // test hook (classic renderer)
      int ow, oh; SDL_GetRendererOutputSize(renderer, &ow, &oh);
      uint8_t* buf = malloc((size_t)ow * oh * 3);
      if(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24, buf, ow * 3) == 0) {
        FILE* f = fopen(getenv("SMKPLAY_SHOT"), "wb");
        if(f) { fprintf(f, "P6\n%d %d\n255\n", ow, oh); fwrite(buf, 1, (size_t)ow * oh * 3, f); fclose(f); }
      }
      free(buf);
    }
    SDL_RenderPresent(renderer);
  }
  if(net) {
    printf("netplay: ran %u frames, WRAM hash %016llx%s\n", net_frameNo(net), (unsigned long long)wramHash(), desyncShown ? " (DESYNC seen)" : "");
    net_close(net);
  }
  opt_save(&opt, cfgPath);
  if(!hostPort && !joinHost) saveSram(true);   // netplay sessions don't overwrite your save
  menu_free(menu);
  snes_free(snes);
  SDL_Quit();
  return 0;
}
