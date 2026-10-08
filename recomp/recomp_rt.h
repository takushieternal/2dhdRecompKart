// Runtime support for the generated code in recomp/gen/.
#ifndef RECOMP_RT_H
#define RECOMP_RT_H
#include <stdint.h>
#include <stdbool.h>
#include "snes.h"
#include "cpu.h"
#include "cpu_core.inc"

extern uint32_t recomp_y0;

// Leave compiled code when the interpreter loop would stop or take an interrupt before this
// instruction. Returns -1 if nothing ran yet (interpreter takes over), 0 otherwise.
#define CHK(pcv) do { if(c->intWanted || snes_yieldCounter != y0) { c->pc = (pcv); return ran - 1; } } while(0)
// The instruction's length depends on M/X; if the flags differ from the analysis, let the interpreter do it.
#define GUARD(pcv, cond) do { if(!(cond)) { c->pc = (pcv); return ran - 1; } } while(0)
// Opcode fetch: same bus access as the interpreter (timing), operands are fetched by cpu_op_XX.
#include <stdio.h>
extern FILE* recomp_ilog;   // debug: per-instruction log (smktrace `ilog`)
#define FETCH(pcv) do { ran = 1; c->pc = (pcv); \
  if(recomp_ilog) fprintf(recomp_ilog, "%02x%04x %llu\n", c->k, (pcv), (unsigned long long)((Snes*)c->mem)->cycles); \
  cpu_readOpcode(c); } while(0)

#endif
