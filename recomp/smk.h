// smk.h: helpers for hand-written C that replaces recompiled code (recomp/overrides/*.c).
//
//   SMK_OVERRIDE(0x81F108, my_func)   declares that my_func replaces the instruction at $81F108.
//   void my_func(Cpu* c) { ...; c->pc = <next pc>; }   (or RTS/RTL yourself and set pc/k)
//
// Regenerate with tools/recomp.py after adding or removing overrides.
#ifndef SMK_H
#define SMK_H
#include <stdint.h>
#include <stdbool.h>
#include "snes.h"
#include "cpu.h"

#define SMK_OVERRIDE(addr, fn) /* picked up by tools/recomp.py */

static inline Snes* smk_snes(Cpu* c) { return (Snes*)c->mem; }
// Work RAM ($7E:0000-$7F:FFFF), no bus timing.
#define WRAM(c) (smk_snes(c)->ram)
static inline uint8_t ram8(Cpu* c, uint32_t a) { return WRAM(c)[a & 0x1ffff]; }
static inline uint16_t ram16(Cpu* c, uint32_t a) { return WRAM(c)[a & 0x1ffff] | WRAM(c)[(a + 1) & 0x1ffff] << 8; }
static inline void ram8w(Cpu* c, uint32_t a, uint8_t v) { WRAM(c)[a & 0x1ffff] = v; }
static inline void ram16w(Cpu* c, uint32_t a, uint16_t v) { WRAM(c)[a & 0x1ffff] = v; WRAM(c)[(a + 1) & 0x1ffff] = v >> 8; }
// Bus access with normal timing/side effects (PPU registers, ROM, SRAM...).
static inline uint8_t bus8(Cpu* c, uint32_t a) { return c->read(c->mem, a); }
static inline void bus8w(Cpu* c, uint32_t a, uint8_t v) { c->write(c->mem, a, v); }
// Flag helpers
static inline void setzn16(Cpu* c, uint16_t v) { c->z = v == 0; c->n = v & 0x8000; }
static inline void setzn8(Cpu* c, uint8_t v) { c->z = v == 0; c->n = v & 0x80; }

#endif
