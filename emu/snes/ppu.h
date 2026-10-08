
#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Ppu Ppu;

#define PPU_WS_EXT 72                                  // 256 + 2*72 = 400 px wide: 16:9 at 224 lines
#define PPU_WS_WIDTH (256 + 2 * PPU_WS_EXT)
#define PPU_WS_ROW (PPU_WS_WIDTH * 8)                  // bytes per buffer row (2 output pixels x 4 bytes)

#include "snes.h"
#include "statehandler.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  uint16_t tileAdr;
  bool bigTiles;
  bool mosaicEnabled;
} BgLayer;

typedef struct Layer {
  bool mainScreenEnabled;
  bool subScreenEnabled;
  bool mainScreenWindowed;
  bool subScreenWindowed;
} Layer;

typedef struct WindowLayer {
  bool window1enabled;
  bool window2enabled;
  bool window1inversed;
  bool window2inversed;
  uint8_t maskLogic;
} WindowLayer;

struct Ppu {
  Snes* snes;
  // vram access
  uint16_t vram[0x8000];
  uint16_t vramPointer;
  bool vramIncrementOnHigh;
  uint16_t vramIncrement;
  uint8_t vramRemapMode;
  uint16_t vramReadBuffer;
  // cgram access
  uint16_t cgram[0x100];
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  uint8_t oamAdr;
  uint8_t oamAdrWritten;
  bool oamInHigh;
  bool oamInHighWritten;
  bool oamSecondWrite;
  uint8_t oamBuffer;
  // object/sprites
  bool objPriority;
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  uint8_t objPixelBuffer[256]; // line buffers
  uint8_t objPriorityBuffer[256];
  bool timeOver;
  bool rangeOver;
  bool objInterlace;
  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  uint8_t mosaicSize;
  uint8_t mosaicStartLine;
  // layers
  Layer layer[5];
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;
  // windows
  WindowLayer windowLayer[6];
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  bool mathEnabled[6];
  uint8_t fixedColorR;
  uint8_t fixedColorG;
  uint8_t fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3priority;
  bool evenFrame;
  bool pseudoHires;
  bool overscan;
  bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
  bool interlace;
  bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
  bool directColor;
  // latching
  uint16_t hCount;
  uint16_t vCount;
  bool hCountSecond;
  bool vCountSecond;
  bool countersLatched;
  uint8_t ppu1openBus;
  uint8_t ppu2openBus;
  // pixel buffer (xbgr)
  // times 2 for even and odd frame
  uint8_t pixelBuffer[512 * 4 * 239 * 2];
  // HD Mode 7 (render-only, not part of savestates): 2x2 samples per Mode 7 pixel
  bool hdMode7;
  uint8_t hdSubX, hdSubY;
  int m7startX2, m7startY2;                    // line starts half a line further down
  uint8_t pixelBuffer2[512 * 4 * 239 * 2];     // lower half-line of HD rows
  bool rowHd[239 * 2];
  // Widescreen (render-only): PPU_WS_EXT extra pixels on each side
  bool widescreen;
  uint8_t objPixW[256 + 2 * PPU_WS_EXT], objPrioW[256 + 2 * PPU_WS_EXT];
  bool wsFrameHasM7, wsPrevFrameHasM7;
  bool wsSide;
  uint8_t wsSideLayers;                   // bit n: BGn+1 may fill side columns on non-Mode-7 lines
  // HD-2D taps (render-only): Mode 7 registers as they were on each visible line
  bool hd2dTaps;                          // fill objLayer (needs widescreen on)
  uint32_t objLayer[239 * 2][PPU_WS_WIDTH];  // RGBA (0xAABBGGRR) of pixels whose main-screen layer is a sprite
  int lastMainLayer;
  bool lastSubObj;                         // last pixel had colour math applied over a non-sprite layer
  uint8_t layerTap[239 * 2][PPU_WS_WIDTH];  // main-screen layer per pixel: 0-3 BG, 4/6 OBJ, 5 backdrop
  bool lineIsM7[240];
  int16_t lineM7[240][8];
  uint16_t lineHScroll[240][2];            // BG1/BG2 horizontal scroll per line (sky panorama)                            // pixel being rendered is in a side column   // frames without any Mode 7 line are pillarboxed
  uint8_t pixelBufferW[PPU_WS_ROW * 239 * 2];
  uint8_t pixelBufferW2[PPU_WS_ROW * 239 * 2];
  uint8_t pixelOutputFormat;
};

enum { ppu_pixelOutputFormatXBGR = 0, ppu_pixelOutputFormatBGRX = 1 };

Ppu* ppu_init(Snes* snes);
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleState(Ppu* ppu, StateHandler* sh);
bool ppu_checkOverscan(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_handleFrameStart(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_putPixels(Ppu* ppu, uint8_t* pixels);
// widescreen output: (PPU_WS_WIDTH*2) x 480, 4 bytes per pixel
void ppu_putPixelsWide(Ppu* ppu, uint8_t* pixels);
void ppu_setPixelOutputFormat(Ppu* ppu, int pixelOutputFormat);

#endif
