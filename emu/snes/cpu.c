
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"
#include "statehandler.h"

void (*cpu_traceHook)(Cpu* cpu) = NULL;
bool cpu_fetching = false;
int (*cpu_recompHook)(Cpu* cpu) = NULL;

// addressing modes and opcode functions not declared, only used after defintions

#include "cpu_core.inc"

Cpu* cpu_init(void* mem, CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle) {
  Cpu* cpu = malloc(sizeof(Cpu));
  cpu->mem = mem;
  cpu->read = read;
  cpu->write = write;
  cpu->idle = idle;
  cpu->useRecomp = false;
  return cpu;
}

void cpu_free(Cpu* cpu) {
  free(cpu);
}

void cpu_reset(Cpu* cpu, bool hard) {
  if(hard) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0;
    cpu->pc = 0;
    cpu->dp = 0;
    cpu->k = 0;
    cpu->db = 0;
    cpu->c = false;
    cpu->z = false;
    cpu->v = false;
    cpu->n = false;
    cpu->i = false;
    cpu->d = false;
    cpu->xf = false;
    cpu->mf = false;
    cpu->e = false;
    cpu->irqWanted = false;
  }
  cpu->waiting = false;
  cpu->stopped = false;
  cpu->nmiWanted = false;
  cpu->intWanted = false;
  cpu->resetWanted = true;
}

void cpu_handleState(Cpu* cpu, StateHandler* sh) {
  sh_handleBools(sh,
    &cpu->c, &cpu->z, &cpu->v, &cpu->n, &cpu->i, &cpu->d, &cpu->xf, &cpu->mf, &cpu->e, &cpu->waiting, &cpu->stopped,
    &cpu->irqWanted, &cpu->nmiWanted, &cpu->intWanted, &cpu->resetWanted, NULL
  );
  sh_handleBytes(sh, &cpu->k, &cpu->db, NULL);
  sh_handleWords(sh, &cpu->a, &cpu->x, &cpu->y, &cpu->sp, &cpu->pc, &cpu->dp, NULL);
}

void cpu_runOpcode(Cpu* cpu) {
  if(cpu->resetWanted) {
    cpu->resetWanted = false;
    // reset: brk/interrupt without writes
    cpu_read(cpu, (cpu->k << 16) | cpu->pc);
    cpu_idle(cpu);
    cpu_read(cpu, 0x100 | (cpu->sp-- & 0xff));
    cpu_read(cpu, 0x100 | (cpu->sp-- & 0xff));
    cpu_read(cpu, 0x100 | (cpu->sp-- & 0xff));
    cpu->sp = (cpu->sp & 0xff) | 0x100;
    cpu->e = true;
    cpu->i = true;
    cpu->d = false;
    cpu_setFlags(cpu, cpu_getFlags(cpu)); // updates x and m flags, clears upper half of x and y if needed
    cpu->k = 0;
    cpu->pc = cpu_readWord(cpu, 0xfffc, 0xfffd, false);
    return;
  }
  if(cpu->stopped) {
    cpu_idleWait(cpu);
    return;
  }
  if(cpu->waiting) {
    if(cpu->irqWanted || cpu->nmiWanted) {
      cpu->waiting = false;
      cpu_idle(cpu);
      cpu_checkInt(cpu);
      cpu_idle(cpu);
      return;
    } else {
      cpu_idleWait(cpu);
      return;
    }
  }
  // not stopped or waiting, execute a opcode or go to interrupt
  if(cpu->intWanted) {
    cpu_read(cpu, (cpu->k << 16) | cpu->pc);
    cpu_doInterrupt(cpu);
  } else {
    // recompiled code runs as many instructions as it can; -1 = nothing compiled here
    if(cpu->useRecomp && cpu_recompHook && cpu_recompHook(cpu) >= 0) return;
    if(cpu_traceHook) cpu_traceHook(cpu);
    uint8_t opcode = cpu_readOpcode(cpu);
    cpu_doOpcode(cpu, opcode);
  }
}

void cpu_nmi(Cpu* cpu) {
  cpu->nmiWanted = true;
}

void cpu_setIrq(Cpu* cpu, bool state) {
  cpu->irqWanted = state;
}

