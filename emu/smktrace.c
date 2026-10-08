// smktrace: headless SNES runner that records code/data coverage for the
// coverage-guided disassembler, driven by a simple input script.
//
//   smktrace <rom.sfc> <script.txt> <outdir>
//
// Script lines (one per line, '#' comments). Buttons: B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R joined by '+'.
//   <frames> [buttons]           hold buttons on pad 1 for N frames
//   tap <buttons>                press 4 frames, release 8
//   p2 <frames> [buttons]        hold buttons on pad 2
//   both <frames> <p1> <p2>      hold buttons on both pads
//   p2hold <buttons|->           pad 2 keeps holding these during every later command
//   random <frames> <seed>       random button mashing on pad 1
//   until <addr> <val> <max> [b] hold [b] until WRAM[addr]==val (hex), at most <max> frames
//   untiltap <addr> <val> <max> [b]  same, tapping [b]
//   shot <name>                  <outdir>/<name>.ppm (256x224)
//   save/load <name>             savestate <outdir>/<name>.state
//   sramsave/sramload <name>     battery RAM <outdir>/<name>.srm
//   reset                        power cycle (WRAM cleared, SRAM kept)
//   ram <addr> [len]             print WRAM;  poke <addr> <val>;  dumpram <name>
//   watch <addr>                 print every CPU write to that WRAM address with the PC
//   prof <frames>                print the hottest PCs over N frames
// Coverage is merged into <outdir>/cov.bin etc. so multiple runs accumulate.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "snes.h"
#include "cpu.h"
#include "cart.h"
#include "dsp1.h"
#include "ppu.h"
#ifdef WITH_RECOMP
#include "recomp.h"
#endif

#define ROMSZ 0x80000

// cov flags per ROM byte
enum {
  C_OP_M1 = 1, C_OP_M0 = 2, C_OP_X1 = 4, C_OP_X0 = 8,
  C_OPERAND = 16, C_DATA = 32, C_EMU = 64, C_ENTRY = 128
};
static uint8_t cov[ROMSZ];
static uint8_t covDb[ROMSZ];     // data bank seen at instruction
static uint16_t covDp[ROMSZ];    // direct page seen at instruction
static uint8_t covMulti[ROMSZ];  // bit0: multiple DB, bit1: multiple D
static uint8_t covSeen[ROMSZ];   // 1 if covDb/covDp valid

typedef struct { uint32_t from, to; uint8_t op; } Edge;
static Edge* edges; static int nEdges, capEdges;
static uint32_t* ramPcs; static int nRam, capRam;

static Snes* snes;
static CpuWriteHandler origWrite; static int watchAdr = -1; static int watchHits;
static uint32_t curPc;
static void watchWrite(void* mem, uint32_t adr, uint8_t val) {
  uint8_t bank = adr >> 16; uint16_t lo = adr & 0xffff; int w = -1;
  if(bank == 0x7e || bank == 0x7f) w = ((bank & 1) << 16) | lo;
  else if((bank & 0x7f) < 0x40 && lo < 0x2000) w = lo;
  if(w == watchAdr && watchHits < 50) { watchHits++; printf("  write $%05x = %02x at pc %06x\n", w, val, curPc); }
  origWrite(mem, adr, val);
}
static uint32_t* profHist; static bool profOn;
static uint32_t prevPc = 0xffffffff; static uint8_t prevOp;

static int romOff(uint8_t bank, uint16_t adr) {
  uint8_t b = bank & 0x7f;
  if(b == 0x7e || b == 0x7f) return -1;
  if(adr >= 0x8000 || b >= 0x40) return (((b & 0x3f) << 16) | adr) & (ROMSZ - 1);
  return -1;
}

#define EH_BITS 20
static uint64_t* edgeHash;
static void addEdge(uint32_t from, uint32_t to, uint8_t op) {
  if(!edgeHash) edgeHash = calloc(1 << EH_BITS, 8);
  uint64_t key = ((uint64_t)from << 32) | ((uint64_t)to << 8) | op | (1ull << 63);
  uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> (64 - EH_BITS));
  while(edgeHash[h]) {
    if(edgeHash[h] == key) return;
    h = (h + 1) & ((1 << EH_BITS) - 1);
  }
  if(nEdges >= (1 << EH_BITS) / 2) return; // table full enough; drop
  edgeHash[h] = key;
  if(nEdges == capEdges) { capEdges = capEdges ? capEdges * 2 : 4096; edges = realloc(edges, capEdges * sizeof(Edge)); }
  edges[nEdges++] = (Edge){from, to, op};
}

static FILE* ilog;
static void traceHook(Cpu* cpu) {
  if(ilog) fprintf(ilog, "%02x%04x %llu\n", cpu->k, cpu->pc, (unsigned long long)snes->cycles);
  uint32_t pc = (cpu->k << 16) | cpu->pc;
  int o = romOff(cpu->k, cpu->pc);
  if(profOn) profHist[pc & 0xffffff]++;
  if(prevPc != 0xffffffff) {
    switch(prevOp) {
      case 0x6c: case 0x7c: case 0xdc: case 0xfc: case 0x60: case 0x6b:
        addEdge(prevPc, pc, prevOp);
    }
  }
  if(o >= 0) {
    uint8_t f = cov[o];
    f |= cpu->mf ? C_OP_M1 : C_OP_M0;
    f |= cpu->xf ? C_OP_X1 : C_OP_X0;
    if(cpu->e) f |= C_EMU;
    cov[o] = f;
    if(!covSeen[o]) { covSeen[o] = 1; covDb[o] = cpu->db; covDp[o] = cpu->dp; }
    else {
      if(covDb[o] != cpu->db) covMulti[o] |= 1;
      if(covDp[o] != cpu->dp) covMulti[o] |= 2;
    }
    prevOp = snes->cart->rom[o];
  } else {
    // executing from RAM
    int found = 0;
    for(int i = 0; i < nRam; i++) if(ramPcs[i] == pc) { found = 1; break; }
    if(!found && nRam < 100000) {
      if(nRam == capRam) { capRam = capRam ? capRam * 2 : 1024; ramPcs = realloc(ramPcs, capRam * sizeof(uint32_t)); }
      ramPcs[nRam++] = pc;
    }
    uint8_t b = cpu->k & 0x7f;
    if(b == 0x7e || b == 0x7f) prevOp = snes->ram[((b & 1) << 16) | cpu->pc];
    else if(cpu->pc < 0x2000) prevOp = snes->ram[cpu->pc];
    else prevOp = 0xea;
  }
  prevPc = pc; curPc = pc;
  { extern uint32_t dsp1_dbgPc; dsp1_dbgPc = pc; }
}

static void readHook(uint8_t bank, uint16_t adr) {
  int o = romOff(bank, adr);
  if(o < 0) return;
  if(cpu_fetching) cov[o] |= 0; // opcode byte, already recorded via hook
  else cov[o] |= C_DATA;
}

static uint8_t* readFile(const char* path, int* len) {
  FILE* f = fopen(path, "rb");
  if(!f) return NULL;
  fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* d = malloc(*len);
  if(fread(d, 1, *len, f) != (size_t)*len) { fclose(f); free(d); return NULL; }
  fclose(f);
  return d;
}

static void writeFile(const char* path, const void* d, int len) {
  FILE* f = fopen(path, "wb");
  if(!f) { perror(path); return; }
  fwrite(d, 1, len, f); fclose(f);
}

static const char* btnNames[12] = {"B","Y","Select","Start","Up","Down","Left","Right","A","X","L","R"};

static uint16_t parseButtons(const char* s) {
  uint16_t m = 0;
  if(!s || !*s || !strcmp(s, "-")) return 0;
  char buf[256]; strncpy(buf, s, 255); buf[255] = 0;
  for(char* t = strtok(buf, "+,"); t; t = strtok(NULL, "+,")) {
    for(int i = 0; i < 12; i++) if(!strcasecmp(t, btnNames[i])) m |= 1 << i;
  }
  return m;
}

static void setPad(int player, uint16_t m) {
  for(int i = 0; i < 12; i++) snes_setButtonState(snes, player, i, (m >> i) & 1);
}

static uint8_t pixels[512 * 480 * 4];
static uint32_t frameCount;

static void shot(const char* dir, const char* name) {
  snes_setPixels(snes, pixels);
  char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE* f = fopen(path, "wb");
  if(!f) return;
  fprintf(f, "P6\n256 224\n255\n");
  for(int y = 0; y < 224; y++) for(int x = 0; x < 256; x++) {
    // buffer is 512x480 XBGR, doubled; sample even pixels, skip the first 16 lines of overscan
    uint8_t* p = &pixels[((y * 2 + 16) * 512 + x * 2) * 4];
    uint8_t rgb[3] = {p[3], p[2], p[1]}; // BGRX output format
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static FILE* hashLog;
static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = p;
  for(size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
  return h;
}
static void logHash(void) {
  uint64_t h = 0xcbf29ce484222325ull;
  h = fnv(h, snes->ram, sizeof(snes->ram));
  h = fnv(h, snes->ppu->vram, sizeof(snes->ppu->vram));
  h = fnv(h, snes->ppu->cgram, sizeof(snes->ppu->cgram));
  h = fnv(h, snes->ppu->oam, sizeof(snes->ppu->oam));
  Cpu* c = snes->cpu;
  uint16_t regs[8] = {c->a, c->x, c->y, c->sp, c->pc, c->dp, c->k, c->db};
  h = fnv(h, regs, sizeof regs);
  fprintf(hashLog, "%u %llu %016llx\n", frameCount, (unsigned long long)snes->cycles, (unsigned long long)h);
}

static void runFrames(int n, uint16_t p1, uint16_t p2) {
  setPad(1, p1); setPad(2, p2);
  for(int i = 0; i < n; i++) { dsp1_tapReset(); snes_runFrame(snes); frameCount++; if(hashLog) logHash(); }
}

static void mergeLoad(const char* dir) {
  char path[512]; int len;
  uint8_t* d;
  snprintf(path, sizeof path, "%s/cov.bin", dir);
  if((d = readFile(path, &len)) && len == ROMSZ) { memcpy(cov, d, ROMSZ); free(d); }
  snprintf(path, sizeof path, "%s/covctx.bin", dir);
  if((d = readFile(path, &len)) && len == ROMSZ * 5) {
    memcpy(covDb, d, ROMSZ); memcpy(covDp, d + ROMSZ, ROMSZ * 2);
    memcpy(covMulti, d + ROMSZ * 3, ROMSZ); memcpy(covSeen, d + ROMSZ * 4, ROMSZ); free(d);
  }
}

static void mergeSave(const char* dir) {
  char path[512];
  snprintf(path, sizeof path, "%s/cov.bin", dir); writeFile(path, cov, ROMSZ);
  uint8_t* d = malloc(ROMSZ * 5);
  memcpy(d, covDb, ROMSZ); memcpy(d + ROMSZ, covDp, ROMSZ * 2);
  memcpy(d + ROMSZ * 3, covMulti, ROMSZ); memcpy(d + ROMSZ * 4, covSeen, ROMSZ);
  snprintf(path, sizeof path, "%s/covctx.bin", dir); writeFile(path, d, ROMSZ * 5); free(d);
  snprintf(path, sizeof path, "%s/edges.txt", dir);
  FILE* f = fopen(path, "a");
  for(int i = 0; i < nEdges; i++) fprintf(f, "%06x %06x %02x\n", edges[i].from, edges[i].to, edges[i].op);
  fclose(f);
  snprintf(path, sizeof path, "%s/ramcode.txt", dir);
  f = fopen(path, "a");
  for(int i = 0; i < nRam; i++) fprintf(f, "%06x\n", ramPcs[i]);
  fclose(f);
  snprintf(path, sizeof path, "%s/dsp1cmds.txt", dir);
  f = fopen(path, "a");
  for(int i = 0; i < 64; i++) if(dsp1_cmdCount[i]) fprintf(f, "%02x %u\n", i, dsp1_cmdCount[i]);
  fclose(f);
  int code = 0, data = 0;
  for(int i = 0; i < ROMSZ; i++) { if(cov[i] & 15) code++; if(cov[i] & C_DATA) data++; }
  printf("coverage: %d instruction starts, %d data bytes read, %d RAM pcs, %d edges, %u frames\n",
         code, data, nRam, nEdges, frameCount);
}

int main(int argc, char** argv) {
  if(argc < 4) { fprintf(stderr, "usage: smktrace rom script outdir [--recomp]\n"); return 1; }
  bool useRecomp = argc > 4 && !strcmp(argv[4], "--recomp");
  int len;
  uint8_t* rom = readFile(argv[1], &len);
  if(!rom) { perror(argv[1]); return 1; }
  const char* dir = argv[3];
  snes = snes_init();
  if(!snes_loadRom(snes, rom, len)) return 1;
  mergeLoad(dir);
#ifdef WITH_RECOMP
  if(useRecomp) { recomp_install(); snes->cpu->useRecomp = true; printf("running recompiled code\n"); }
#else
  if(useRecomp) { fprintf(stderr, "built without recompiled code\n"); return 1; }
#endif
  cpu_traceHook = traceHook;
  cart_readHook = readHook;
  if(getenv("DSP1LOG")) dsp1_log = fopen(getenv("DSP1LOG"), "w");
  { extern int dsp1_rawLog; dsp1_rawLog = getenv("DSP1RAW") != NULL; }
  FILE* sc = fopen(argv[2], "r");
  if(!sc) { perror(argv[2]); return 1; }
  char line[512];
  uint16_t p2 = 0;
  while(fgets(line, sizeof line, sc)) {
    char* h = strchr(line, '#'); if(h) *h = 0;
    char a[128] = "", b[256] = "", c[128] = "";
    int n = sscanf(line, "%127s %255s %127s", a, b, c);
    if(n <= 0) continue;
    if(!strcmp(a, "shot")) shot(dir, b);
    else if(!strcmp(a, "tap")) { runFrames(4, parseButtons(b), p2); runFrames(8, 0, p2); }
    else if(!strcmp(a, "p2")) { uint16_t keep = p2; runFrames(atoi(b), 0, parseButtons(c)); p2 = keep; }
    else if(!strcmp(a, "random")) {
      int frames = atoi(b); srand(atoi(c));
      uint16_t m = 0;
      for(int i = 0; i < frames; i++) {
        if(i % 6 == 0) m = (rand() & 0x0ff3) | (rand() % 40 == 0 ? 8 : 0);
        runFrames(1, m, p2);
      }
    }
    else if(!strcmp(a, "p2hold")) { p2 = parseButtons(b); } // pad 2 holds these from now on ("p2hold -" releases)
    else if(!strcmp(a, "both")) {   // both <frames> <p1> <p2>
      char d2[128] = ""; sscanf(line, "%*s %*s %*s %127s", d2);
      runFrames(atoi(b), parseButtons(c), parseButtons(d2));
    }
    else if(!strcmp(a, "sramsave") || !strcmp(a, "sramload")) {
      char path[512]; snprintf(path, sizeof path, "%s/%s.srm", dir, b);
      if(a[4] == 's') { uint8_t buf[0x800]; int n = snes_saveBattery(snes, buf); writeFile(path, buf, n); }
      else { int n; uint8_t* d = readFile(path, &n); if(d) { snes_loadBattery(snes, d, n); free(d); } else fprintf(stderr, "no %s\n", path); }
    }
    else if(!strcmp(a, "ilog")) {     // per-instruction log (pc, master cycle) until "ilog -"
      if(ilog) { fclose(ilog); ilog = NULL; }
      if(strcmp(b, "-")) { char path[512]; snprintf(path, sizeof path, "%s/%s", dir, b); ilog = fopen(path, "w"); }
#ifdef WITH_RECOMP
      recomp_ilog = ilog;
#endif
    }
    else if(!strcmp(a, "hashlog")) {   // per-frame state hash, for comparing interpreter vs recompiled runs
      char path[512]; snprintf(path, sizeof path, "%s/%s", dir, b); hashLog = fopen(path, "w");
    }
    else if(!strcmp(a, "dumpppu")) {   // binary dump for HD-2D tooling: vram, cgram, per-line m7, taps
      char path[512]; snprintf(path, sizeof path, "%s/%s.ppudump", dir, b);
      FILE* f = fopen(path, "wb");
      Ppu* q = snes->ppu;
      fwrite(q->vram, 2, 0x8000, f); fwrite(q->cgram, 2, 256, f);
      fwrite(q->lineIsM7, 1, 240, f); fwrite(q->lineM7, 2, 240 * 8, f);
      fwrite(&dsp1_tapNumCameras, 4, 1, f); fwrite(dsp1_tapCameras, sizeof(Dsp1TapCamera), DSP1_TAP_MAX_CAMERAS, f);
      fwrite(&dsp1_tapNumProjects, 4, 1, f); fwrite(dsp1_tapProjects, sizeof(Dsp1TapProject), DSP1_TAP_MAX_PROJECTS, f);
      fclose(f);
    }
    else if(!strcmp(a, "m7lines")) {
      Ppu* q = snes->ppu;
      for(int l = 1; l < 225; l++) if(q->lineIsM7[l])
        printf("L%3d A %6d B %6d C %6d D %6d X %5d Y %5d H %5d V %5d\n", l, q->lineM7[l][0], q->lineM7[l][1], q->lineM7[l][2],
               q->lineM7[l][3], q->lineM7[l][4], q->lineM7[l][5], q->lineM7[l][6], q->lineM7[l][7]);
    }
    else if(!strcmp(a, "ppu")) {
      Ppu* q = snes->ppu;
      printf("mode %d m7 A %d B %d C %d D %d X %d Y %d H %d V %d\n", q->mode, (int16_t)q->m7matrix[0], (int16_t)q->m7matrix[1],
             (int16_t)q->m7matrix[2], (int16_t)q->m7matrix[3], q->m7matrix[4], q->m7matrix[5], q->m7matrix[6], q->m7matrix[7]);
    }
    else if(!strcmp(a, "scroll")) {
      Ppu* q = snes->ppu;
      printf("aas %d  L5 %d %d  L12 %d %d  L20 %d %d\n", dsp1_tapNumCameras > 1 ? dsp1_tapCameras[1].aas : -1,
             q->lineHScroll[5][0], q->lineHScroll[5][1], q->lineHScroll[12][0], q->lineHScroll[12][1], q->lineHScroll[20][0], q->lineHScroll[20][1]);
    }
    else if(!strcmp(a, "wslayers")) { snes->ppu->wsSideLayers = strtol(b, NULL, 0); }
    else if(!strcmp(a, "objprobe")) {   // objprobe <x0> <y0>: layer ids and obj alpha in a 16x8 box (4:3 coords)
      int x0 = atoi(b), y0 = atoi(c); Ppu* q = snes->ppu; int half = q->evenFrame ? 0 : 239;
      for(int y = y0; y < y0 + 8; y++) { for(int x = x0; x < x0 + 16; x++) printf("%d%c", q->layerTap[y + half][x + PPU_WS_EXT], (q->objLayer[y + half][x + PPU_WS_EXT] >> 24) ? '*' : '.'); printf("\n"); }
      printf("mathEnabled %d%d%d%d%d%d add %d half %d sub-obj %d main-obj %d\n", q->mathEnabled[0], q->mathEnabled[1], q->mathEnabled[2], q->mathEnabled[3], q->mathEnabled[4], q->mathEnabled[5], q->addSubscreen, q->halfColor, q->layer[4].subScreenEnabled, q->layer[4].mainScreenEnabled);
    }
    else if(!strcmp(a, "hd2dtaps")) { snes->ppu->hd2dTaps = !strcmp(b, "on"); snes->ppu->widescreen |= snes->ppu->hd2dTaps; }
    else if(!strcmp(a, "ws")) { snes->ppu->widescreen = !strcmp(b, "on"); }   // widescreen rendering (render-only)
    else if(!strcmp(a, "shotws")) {   // widescreen output, 800x448
      static uint8_t wpx[PPU_WS_WIDTH * 2 * 480 * 4];
      snes_setPixelsWide(snes, wpx);
      char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, b);
      FILE* f = fopen(path, "wb");
      if(f) {
        int W = PPU_WS_WIDTH * 2;
        fprintf(f, "P6\n%d 448\n255\n", W);
        for(int y = 0; y < 448; y++) for(int x = 0; x < W; x++) { uint8_t* p = &wpx[((y + 16) * W + x) * 4]; uint8_t rgb[3] = {p[3], p[2], p[1]}; fwrite(rgb, 1, 3, f); }
        fclose(f);
      }
    }
    else if(!strcmp(a, "hd")) { snes->ppu->hdMode7 = !strcmp(b, "on"); }   // HD Mode 7 rendering (render-only)
    else if(!strcmp(a, "shothd")) {   // full 512x448 output
      snes_setPixels(snes, pixels);
      char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, b);
      FILE* f = fopen(path, "wb");
      if(f) {
        fprintf(f, "P6\n512 448\n255\n");
        for(int y = 0; y < 448; y++) for(int x = 0; x < 512; x++) { uint8_t* p = &pixels[((y + 16) * 512 + x) * 4]; uint8_t rgb[3] = {p[3], p[2], p[1]}; fwrite(rgb, 1, 3, f); }
        fclose(f);
      }
    }
    else if(!strcmp(a, "watch")) {
      watchAdr = strtol(b, NULL, 16); watchHits = 0;
      if(!origWrite) { origWrite = snes->cpu->write; snes->cpu->write = watchWrite; }
    }
    else if(!strcmp(a, "reset")) { snes_reset(snes, true); prevPc = 0xffffffff; } // power cycle, SRAM kept
    else if(!strcmp(a, "until") || !strcmp(a, "untiltap")) {
      // until <addr> <val> <maxframes> [buttons]: hold (until) or tap (untiltap) buttons until ram[addr]==val
      int ad = strtol(b, NULL, 16), val = strtol(c, NULL, 16);
      int mx = 0; char btn[128] = "";
      sscanf(line, "%*s %*s %*s %d %127s", &mx, btn);
      uint16_t m = parseButtons(btn);
      bool tap = a[5] == 't';
      int f = 0;
      while(f < mx && snes->ram[ad & 0x1ffff] != val) {
        uint16_t cur = (!tap || (f % 12) < 4) ? m : 0;
        runFrames(1, cur, p2); f++;
      }
      printf("%s %s==%s after %d frames%s\n", a, b, c, f, f >= mx ? " (TIMEOUT)" : "");
    }
    else if(!strcmp(a, "dumpram")) {
      char path[512]; snprintf(path, sizeof path, "%s/%s.ram", dir, b); writeFile(path, snes->ram, 0x20000);
    }
    else if(!strcmp(a, "poke")) {
      snes->ram[strtol(b, NULL, 16) & 0x1ffff] = strtol(c, NULL, 16);
    }
    else if(!strcmp(a, "ram")) {
      int ad = strtol(b, NULL, 16), ln = c[0] ? strtol(c, NULL, 16) : 16;
      printf("%05x:", ad);
      for(int i = 0; i < ln; i++) printf(" %02x", snes->ram[(ad + i) & 0x1ffff]);
      printf("\n");
    }
    else if(!strcmp(a, "prof")) {
      if(!profHist) profHist = calloc(1 << 24, 4);
      memset(profHist, 0, (1 << 24) * 4);
      profOn = true; runFrames(atoi(b), 0, p2); profOn = false;
      for(int k = 0; k < 12; k++) {
        uint32_t best = 0, bi = 0;
        for(uint32_t i = 0; i < (1 << 24); i++) if(profHist[i] > best) { best = profHist[i]; bi = i; }
        if(!best) break;
        printf("  %06x %u\n", bi, best); profHist[bi] = 0;
      }
    }
    else if(!strcmp(a, "save")) {
      int sz = snes_saveState(snes, NULL); uint8_t* d = malloc(sz); snes_saveState(snes, d);
      char path[512]; snprintf(path, sizeof path, "%s/%s.state", dir, b); writeFile(path, d, sz); free(d);
    }
    else if(!strcmp(a, "load")) {
      char path[512]; snprintf(path, sizeof path, "%s/%s.state", dir, b);
      int sz; uint8_t* d = readFile(path, &sz);
      if(!d || !snes_loadState(snes, d, sz)) fprintf(stderr, "load %s failed\n", b);
      free(d); prevPc = 0xffffffff;
    }
    else runFrames(atoi(a), parseButtons(b), p2);
  }
  fclose(sc);
  if(hashLog) fclose(hashLog);
#ifdef WITH_RECOMP
  if(useRecomp) printf("recompiled entries: %llu\n", (unsigned long long)recomp_calls);
#endif
  mergeSave(dir);
  return 0;
}
