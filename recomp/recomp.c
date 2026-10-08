// Dispatcher between the interpreter and the recompiled banks.
#include "recomp_rt.h"
#include "recomp.h"
#include "gen/banks.h"

uint64_t recomp_calls;
FILE* recomp_ilog;

uint32_t recomp_y0;

static int recomp_run(Cpu* c) {
  int any = 0;
  recomp_y0 = snes_yieldCounter;   // one snapshot for the whole run, even across bank hops
  for(;;) {
    uint8_t k = c->k;
    if(((k & 0x7f) < 0x40 && c->pc < 0x8000) || (k & 0x7f) == 0x7e || (k & 0x7f) == 0x7f)
      return any ? 0 : -1;            // RAM / IO: interpreter
    int r = smk_banks[k & 7](c);
    recomp_calls++;
    if(r < 0) return any ? 0 : -1;
    any = 1;
    if(r == 0) return 0;
  }
}

void recomp_install(void) { cpu_recompHook = recomp_run; }
