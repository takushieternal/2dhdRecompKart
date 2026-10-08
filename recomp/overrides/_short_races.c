// C version of mods/short_races.asm: karts start their lap counter at $82 (2-lap races).
// Original at $81F108:  ORA.w #$7F00   (A = (A & $00FF) | $7F00, sets N/Z)
// Rename this file to short_races.c (drop the underscore) to enable it.
#include "smk.h"

SMK_OVERRIDE(0x81F108, short_races_lap_init)
void short_races_lap_init(Cpu* c) {
  c->a = (c->a & 0x00FF) | 0x8200;   // high byte -> lap counter at kart+$C1
  setzn16(c, c->a);
  c->pc = 0xF10B;                    // next instruction (STA $C0,X)
}
