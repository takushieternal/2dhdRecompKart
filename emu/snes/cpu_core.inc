// cpu_core.inc: the 65816 core (LakeSnes, MIT), split so recompiled code can call the
// per-opcode functions directly. Included by cpu.c and by the generated recomp sources.
#ifndef CPU_CORE_INC
#define CPU_CORE_INC
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

static uint8_t cpu_read(Cpu* cpu, uint32_t adr);
static void cpu_write(Cpu* cpu, uint32_t adr, uint8_t val);
static void cpu_idle(Cpu* cpu);
static void cpu_idleWait(Cpu* cpu);
static void cpu_checkInt(Cpu* cpu);
static uint8_t cpu_readOpcode(Cpu* cpu);
static uint16_t cpu_readOpcodeWord(Cpu* cpu, bool intCheck);
static uint8_t cpu_getFlags(Cpu* cpu);
static void cpu_setFlags(Cpu* cpu, uint8_t value);
static void cpu_setZN(Cpu* cpu, uint16_t value, bool byte);
static void cpu_doBranch(Cpu* cpu, bool check);
static uint8_t cpu_pullByte(Cpu* cpu);
static void cpu_pushByte(Cpu* cpu, uint8_t value);
static uint16_t cpu_pullWord(Cpu* cpu, bool intCheck);
static void cpu_pushWord(Cpu* cpu, uint16_t value, bool intCheck);
static uint16_t cpu_readWord(Cpu* cpu, uint32_t adrl, uint32_t adrh, bool intCheck);
static void cpu_writeWord(Cpu* cpu, uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck);
static void cpu_doInterrupt(Cpu* cpu);
static void cpu_doOpcode(Cpu* cpu, uint8_t opcode);

static uint8_t cpu_read(Cpu* cpu, uint32_t adr) {
  return cpu->read(cpu->mem, adr);
}

static void cpu_write(Cpu* cpu, uint32_t adr, uint8_t val) {
  cpu->write(cpu->mem, adr, val);
}

static void cpu_idle(Cpu* cpu) {
  cpu->idle(cpu->mem, false);
}

static void cpu_idleWait(Cpu* cpu) {
  cpu->idle(cpu->mem, true);
}

static void cpu_checkInt(Cpu* cpu) {
  cpu->intWanted = cpu->nmiWanted || (cpu->irqWanted && !cpu->i);
}

static uint8_t cpu_readOpcode(Cpu* cpu) {
  cpu_fetching = true;
  uint8_t v = cpu_read(cpu, (cpu->k << 16) | cpu->pc++);
  cpu_fetching = false;
  return v;
}

static uint16_t cpu_readOpcodeWord(Cpu* cpu, bool intCheck) {
  uint8_t low = cpu_readOpcode(cpu);
  if(intCheck) cpu_checkInt(cpu);
  return low | (cpu_readOpcode(cpu) << 8);
}

static uint8_t cpu_getFlags(Cpu* cpu) {
  uint8_t val = cpu->n << 7;
  val |= cpu->v << 6;
  val |= cpu->mf << 5;
  val |= cpu->xf << 4;
  val |= cpu->d << 3;
  val |= cpu->i << 2;
  val |= cpu->z << 1;
  val |= cpu->c;
  return val;
}

static void cpu_setFlags(Cpu* cpu, uint8_t val) {
  cpu->n = val & 0x80;
  cpu->v = val & 0x40;
  cpu->mf = val & 0x20;
  cpu->xf = val & 0x10;
  cpu->d = val & 8;
  cpu->i = val & 4;
  cpu->z = val & 2;
  cpu->c = val & 1;
  if(cpu->e) {
    cpu->mf = true;
    cpu->xf = true;
    cpu->sp = (cpu->sp & 0xff) | 0x100;
  }
  if(cpu->xf) {
    cpu->x &= 0xff;
    cpu->y &= 0xff;
  }
}

static void cpu_setZN(Cpu* cpu, uint16_t value, bool byte) {
  if(byte) {
    cpu->z = (value & 0xff) == 0;
    cpu->n = value & 0x80;
  } else {
    cpu->z = value == 0;
    cpu->n = value & 0x8000;
  }
}

static void cpu_doBranch(Cpu* cpu, bool check) {
  if(!check) cpu_checkInt(cpu);
  uint8_t value = cpu_readOpcode(cpu);
  if(check) {
    cpu_checkInt(cpu);
    cpu_idle(cpu); // taken branch: 1 extra cycle
    cpu->pc += (int8_t) value;
  }
}

static uint8_t cpu_pullByte(Cpu* cpu) {
  cpu->sp++;
  if(cpu->e) cpu->sp = (cpu->sp & 0xff) | 0x100;
  return cpu_read(cpu, cpu->sp);
}

static void cpu_pushByte(Cpu* cpu, uint8_t value) {
  cpu_write(cpu, cpu->sp, value);
  cpu->sp--;
  if(cpu->e) cpu->sp = (cpu->sp & 0xff) | 0x100;
}

static uint16_t cpu_pullWord(Cpu* cpu, bool intCheck) {
  uint8_t value = cpu_pullByte(cpu);
  if(intCheck) cpu_checkInt(cpu);
  return value | (cpu_pullByte(cpu) << 8);
}

static void cpu_pushWord(Cpu* cpu, uint16_t value, bool intCheck) {
  cpu_pushByte(cpu, value >> 8);
  if(intCheck) cpu_checkInt(cpu);
  cpu_pushByte(cpu, value & 0xff);
}

static uint16_t cpu_readWord(Cpu* cpu, uint32_t adrl, uint32_t adrh, bool intCheck) {
  uint8_t value = cpu_read(cpu, adrl);
  if(intCheck) cpu_checkInt(cpu);
  return value | (cpu_read(cpu, adrh) << 8);
}

static void cpu_writeWord(Cpu* cpu, uint32_t adrl, uint32_t adrh, uint16_t value, bool reversed, bool intCheck) {
  if(reversed) {
    cpu_write(cpu, adrh, value >> 8);
    if(intCheck) cpu_checkInt(cpu);
    cpu_write(cpu, adrl, value & 0xff);
  } else {
    cpu_write(cpu, adrl, value & 0xff);
    if(intCheck) cpu_checkInt(cpu);
    cpu_write(cpu, adrh, value >> 8);
  }
}

static void cpu_doInterrupt(Cpu* cpu) {
  cpu_idle(cpu);
  cpu_pushByte(cpu, cpu->k);
  cpu_pushWord(cpu, cpu->pc, false);
  cpu_pushByte(cpu, cpu_getFlags(cpu));
  cpu->i = true;
  cpu->d = false;
  cpu->k = 0;
  cpu->intWanted = false;
  if(cpu->nmiWanted) {
    cpu->nmiWanted = false;
    cpu->pc = cpu_readWord(cpu, 0xffea, 0xffeb, false);
  } else { // irq
    cpu->pc = cpu_readWord(cpu, 0xffee, 0xffef, false);
  }
}

// addressing modes

static void cpu_adrImp(Cpu* cpu) {
  // only for 2-cycle implied opcodes
  cpu_checkInt(cpu);
  if(cpu->intWanted) {
    // if interrupt detected in 2-cycle implied/accumulator opcode,
    // idle cycle turns into read from pc
    cpu_read(cpu, (cpu->k << 16) | cpu->pc);
  } else {
    cpu_idle(cpu);
  }
}

static uint32_t cpu_adrImm(Cpu* cpu, uint32_t* low, bool xFlag) {
  if((xFlag && cpu->xf) || (!xFlag && cpu->mf)) {
    *low = (cpu->k << 16) | cpu->pc++;
    return 0;
  } else {
    *low = (cpu->k << 16) | cpu->pc++;
    return (cpu->k << 16) | cpu->pc++;
  }
}

static uint32_t cpu_adrDp(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  *low = (cpu->dp + adr) & 0xffff;
  return (cpu->dp + adr + 1) & 0xffff;
}

static uint32_t cpu_adrDpx(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  cpu_idle(cpu);
  *low = (cpu->dp + adr + cpu->x) & 0xffff;
  return (cpu->dp + adr + cpu->x + 1) & 0xffff;
}

static uint32_t cpu_adrDpy(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  cpu_idle(cpu);
  *low = (cpu->dp + adr + cpu->y) & 0xffff;
  return (cpu->dp + adr + cpu->y + 1) & 0xffff;
}

static uint32_t cpu_adrIdp(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  uint16_t pointer = cpu_readWord(cpu, (cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
  *low = (cpu->db << 16) + pointer;
  return ((cpu->db << 16) + pointer + 1) & 0xffffff;
}

static uint32_t cpu_adrIdx(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  cpu_idle(cpu);
  uint16_t pointer = cpu_readWord(cpu, (cpu->dp + adr + cpu->x) & 0xffff, (cpu->dp + adr + cpu->x + 1) & 0xffff, false);
  *low = (cpu->db << 16) + pointer;
  return ((cpu->db << 16) + pointer + 1) & 0xffffff;
}

static uint32_t cpu_adrIdy(Cpu* cpu, uint32_t* low, bool write) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  uint16_t pointer = cpu_readWord(cpu, (cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
  // writing opcode or x = 0 or page crossed: 1 extra cycle
  if(write || !cpu->xf || ((pointer >> 8) != ((pointer + cpu->y) >> 8))) cpu_idle(cpu);
  *low = ((cpu->db << 16) + pointer + cpu->y) & 0xffffff;
  return ((cpu->db << 16) + pointer + cpu->y + 1) & 0xffffff;
}

static uint32_t cpu_adrIdl(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  uint32_t pointer = cpu_readWord(cpu, (cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
  pointer |= cpu_read(cpu, (cpu->dp + adr + 2) & 0xffff) << 16;
  *low = pointer;
  return (pointer + 1) & 0xffffff;
}

static uint32_t cpu_adrIly(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  if(cpu->dp & 0xff) cpu_idle(cpu); // dpr not 0: 1 extra cycle
  uint32_t pointer = cpu_readWord(cpu, (cpu->dp + adr) & 0xffff, (cpu->dp + adr + 1) & 0xffff, false);
  pointer |= cpu_read(cpu, (cpu->dp + adr + 2) & 0xffff) << 16;
  *low = (pointer + cpu->y) & 0xffffff;
  return (pointer + cpu->y + 1) & 0xffffff;
}

static uint32_t cpu_adrSr(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  cpu_idle(cpu);
  *low = (cpu->sp + adr) & 0xffff;
  return (cpu->sp + adr + 1) & 0xffff;
}

static uint32_t cpu_adrIsy(Cpu* cpu, uint32_t* low) {
  uint8_t adr = cpu_readOpcode(cpu);
  cpu_idle(cpu);
  uint16_t pointer = cpu_readWord(cpu, (cpu->sp + adr) & 0xffff, (cpu->sp + adr + 1) & 0xffff, false);
  cpu_idle(cpu);
  *low = ((cpu->db << 16) + pointer + cpu->y) & 0xffffff;
  return ((cpu->db << 16) + pointer + cpu->y + 1) & 0xffffff;
}

static uint32_t cpu_adrAbs(Cpu* cpu, uint32_t* low) {
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  *low = (cpu->db << 16) + adr;
  return ((cpu->db << 16) + adr + 1) & 0xffffff;
}

static uint32_t cpu_adrAbx(Cpu* cpu, uint32_t* low, bool write) {
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  // writing opcode or x = 0 or page crossed: 1 extra cycle
  if(write || !cpu->xf || ((adr >> 8) != ((adr + cpu->x) >> 8))) cpu_idle(cpu);
  *low = ((cpu->db << 16) + adr + cpu->x) & 0xffffff;
  return ((cpu->db << 16) + adr + cpu->x + 1) & 0xffffff;
}

static uint32_t cpu_adrAby(Cpu* cpu, uint32_t* low, bool write) {
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  // writing opcode or x = 0 or page crossed: 1 extra cycle
  if(write || !cpu->xf || ((adr >> 8) != ((adr + cpu->y) >> 8))) cpu_idle(cpu);
  *low = ((cpu->db << 16) + adr + cpu->y) & 0xffffff;
  return ((cpu->db << 16) + adr + cpu->y + 1) & 0xffffff;
}

static uint32_t cpu_adrAbl(Cpu* cpu, uint32_t* low) {
  uint32_t adr = cpu_readOpcodeWord(cpu, false);
  adr |= cpu_readOpcode(cpu) << 16;
  *low = adr;
  return (adr + 1) & 0xffffff;
}

static uint32_t cpu_adrAlx(Cpu* cpu, uint32_t* low) {
  uint32_t adr = cpu_readOpcodeWord(cpu, false);
  adr |= cpu_readOpcode(cpu) << 16;
  *low = (adr + cpu->x) & 0xffffff;
  return (adr + cpu->x + 1) & 0xffffff;
}

// opcode functions

static void cpu_and(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low);
    cpu->a = (cpu->a & 0xff00) | ((cpu->a & value) & 0xff);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true);
    cpu->a &= value;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_ora(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low);
    cpu->a = (cpu->a & 0xff00) | ((cpu->a | value) & 0xff);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true);
    cpu->a |= value;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_eor(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low);
    cpu->a = (cpu->a & 0xff00) | ((cpu->a ^ value) & 0xff);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true);
    cpu->a ^= value;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_adc(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low);
    int result = 0;
    if(cpu->d) {
      result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
      if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
      result = (cpu->a & 0xf0) + (value & 0xf0) + result;
    } else {
      result = (cpu->a & 0xff) + value + cpu->c;
    }
    cpu->v = (cpu->a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
    if(cpu->d && result > 0x9f) result += 0x60;
    cpu->c = result > 0xff;
    cpu->a = (cpu->a & 0xff00) | (result & 0xff);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true);
    int result = 0;
    if(cpu->d) {
      result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
      if(result > 0x9) result = ((result + 0x6) & 0xf) + 0x10;
      result = (cpu->a & 0xf0) + (value & 0xf0) + result;
      if(result > 0x9f) result = ((result + 0x60) & 0xff) + 0x100;
      result = (cpu->a & 0xf00) + (value & 0xf00) + result;
      if(result > 0x9ff) result = ((result + 0x600) & 0xfff) + 0x1000;
      result = (cpu->a & 0xf000) + (value & 0xf000) + result;
    } else {
      result = cpu->a + value + cpu->c;
    }
    cpu->v = (cpu->a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
    if(cpu->d && result > 0x9fff) result += 0x6000;
    cpu->c = result > 0xffff;
    cpu->a = result;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_sbc(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low) ^ 0xff;
    int result = 0;
    if(cpu->d) {
      result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
      if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
      result = (cpu->a & 0xf0) + (value & 0xf0) + result;
    } else {
      result = (cpu->a & 0xff) + value + cpu->c;
    }
    cpu->v = (cpu->a & 0x80) == (value & 0x80) && (value & 0x80) != (result & 0x80);
    if(cpu->d && result < 0x100) result -= 0x60;
    cpu->c = result > 0xff;
    cpu->a = (cpu->a & 0xff00) | (result & 0xff);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true) ^ 0xffff;
    int result = 0;
    if(cpu->d) {
      result = (cpu->a & 0xf) + (value & 0xf) + cpu->c;
      if(result < 0x10) result = (result - 0x6) & ((result - 0x6 < 0) ? 0xf : 0x1f);
      result = (cpu->a & 0xf0) + (value & 0xf0) + result;
      if(result < 0x100) result = (result - 0x60) & ((result - 0x60 < 0) ? 0xff : 0x1ff);
      result = (cpu->a & 0xf00) + (value & 0xf00) + result;
      if(result < 0x1000) result = (result - 0x600) & ((result - 0x600 < 0) ? 0xfff : 0x1fff);
      result = (cpu->a & 0xf000) + (value & 0xf000) + result;
    } else {
      result = cpu->a + value + cpu->c;
    }
    cpu->v = (cpu->a & 0x8000) == (value & 0x8000) && (value & 0x8000) != (result & 0x8000);
    if(cpu->d && result < 0x10000) result -= 0x6000;
    cpu->c = result > 0xffff;
    cpu->a = result;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_cmp(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low) ^ 0xff;
    result = (cpu->a & 0xff) + value + 1;
    cpu->c = result > 0xff;
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true) ^ 0xffff;
    result = cpu->a + value + 1;
    cpu->c = result > 0xffff;
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_cpx(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->xf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low) ^ 0xff;
    result = (cpu->x & 0xff) + value + 1;
    cpu->c = result > 0xff;
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true) ^ 0xffff;
    result = cpu->x + value + 1;
    cpu->c = result > 0xffff;
  }
  cpu_setZN(cpu, result, cpu->xf);
}

static void cpu_cpy(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->xf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low) ^ 0xff;
    result = (cpu->y & 0xff) + value + 1;
    cpu->c = result > 0xff;
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true) ^ 0xffff;
    result = cpu->y + value + 1;
    cpu->c = result > 0xffff;
  }
  cpu_setZN(cpu, result, cpu->xf);
}

static void cpu_bit(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t value = cpu_read(cpu, low);
    uint8_t result = (cpu->a & 0xff) & value;
    cpu->z = result == 0;
    cpu->n = value & 0x80;
    cpu->v = value & 0x40;
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, true);
    uint16_t result = cpu->a & value;
    cpu->z = result == 0;
    cpu->n = value & 0x8000;
    cpu->v = value & 0x4000;
  }
}

static void cpu_lda(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    cpu->a = (cpu->a & 0xff00) | cpu_read(cpu, low);
  } else {
    cpu->a = cpu_readWord(cpu, low, high, true);
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static void cpu_ldx(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu->x = cpu_read(cpu, low);
  } else {
    cpu->x = cpu_readWord(cpu, low, high, true);
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static void cpu_ldy(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu->y = cpu_read(cpu, low);
  } else {
    cpu->y = cpu_readWord(cpu, low, high, true);
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static void cpu_sta(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    cpu_write(cpu, low, cpu->a);
  } else {
    cpu_writeWord(cpu, low, high, cpu->a, false, true);
  }
}

static void cpu_stx(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu_write(cpu, low, cpu->x);
  } else {
    cpu_writeWord(cpu, low, high, cpu->x, false, true);
  }
}

static void cpu_sty(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu_write(cpu, low, cpu->y);
  } else {
    cpu_writeWord(cpu, low, high, cpu->y, false, true);
  }
}

static void cpu_stz(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    cpu_checkInt(cpu);
    cpu_write(cpu, low, 0);
  } else {
    cpu_writeWord(cpu, low, high, 0, false, true);
  }
}

static void cpu_ror(Cpu* cpu, uint32_t low, uint32_t high) {
  bool carry = false;
  int result = 0;
  if(cpu->mf) {
    uint8_t value = cpu_read(cpu, low);
    cpu_idle(cpu);
    carry = value & 1;
    result = (value >> 1) | (cpu->c << 7);
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, false);
    cpu_idle(cpu);
    carry = value & 1;
    result = (value >> 1) | (cpu->c << 15);
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
  cpu->c = carry;
}

static void cpu_rol(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    result = (cpu_read(cpu, low) << 1) | cpu->c;
    cpu_idle(cpu);
    cpu->c = result & 0x100;
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    result = (cpu_readWord(cpu, low, high, false) << 1) | cpu->c;
    cpu_idle(cpu);
    cpu->c = result & 0x10000;
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_lsr(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    uint8_t value = cpu_read(cpu, low);
    cpu_idle(cpu);
    cpu->c = value & 1;
    result = value >> 1;
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, false);
    cpu_idle(cpu);
    cpu->c = value & 1;
    result = value >> 1;
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_asl(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    result = cpu_read(cpu, low) << 1;
    cpu_idle(cpu);
    cpu->c = result & 0x100;
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    result = cpu_readWord(cpu, low, high, false) << 1;
    cpu_idle(cpu);
    cpu->c = result & 0x10000;
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_inc(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    result = cpu_read(cpu, low) + 1;
    cpu_idle(cpu);
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    result = cpu_readWord(cpu, low, high, false) + 1;
    cpu_idle(cpu);
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_dec(Cpu* cpu, uint32_t low, uint32_t high) {
  int result = 0;
  if(cpu->mf) {
    result = cpu_read(cpu, low) - 1;
    cpu_idle(cpu);
    cpu_checkInt(cpu);
    cpu_write(cpu, low, result);
  } else {
    result = cpu_readWord(cpu, low, high, false) - 1;
    cpu_idle(cpu);
    cpu_writeWord(cpu, low, high, result, true, true);
  }
  cpu_setZN(cpu, result, cpu->mf);
}

static void cpu_tsb(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    uint8_t value = cpu_read(cpu, low);
    cpu_idle(cpu);
    cpu->z = ((cpu->a & 0xff) & value) == 0;
    cpu_checkInt(cpu);
    cpu_write(cpu, low, value | (cpu->a & 0xff));
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, false);
    cpu_idle(cpu);
    cpu->z = (cpu->a & value) == 0;
    cpu_writeWord(cpu, low, high, value | cpu->a, true, true);
  }
}

static void cpu_trb(Cpu* cpu, uint32_t low, uint32_t high) {
  if(cpu->mf) {
    uint8_t value = cpu_read(cpu, low);
    cpu_idle(cpu);
    cpu->z = ((cpu->a & 0xff) & value) == 0;
    cpu_checkInt(cpu);
    cpu_write(cpu, low, value & ~(cpu->a & 0xff));
  } else {
    uint16_t value = cpu_readWord(cpu, low, high, false);
    cpu_idle(cpu);
    cpu->z = (cpu->a & value) == 0;
    cpu_writeWord(cpu, low, high, value & ~cpu->a, true, true);
  }
}

static inline void cpu_op_00(Cpu* cpu) { // brk imm(s)
  cpu_readOpcode(cpu);
  cpu_pushByte(cpu, cpu->k);
  cpu_pushWord(cpu, cpu->pc, false);
  cpu_pushByte(cpu, cpu_getFlags(cpu));
  cpu->i = true;
  cpu->d = false;
  cpu->k = 0;
  cpu->pc = cpu_readWord(cpu, 0xffe6, 0xffe7, true);
}

static inline void cpu_op_01(Cpu* cpu) { // ora idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_02(Cpu* cpu) { // cop imm(s)
  cpu_readOpcode(cpu);
  cpu_pushByte(cpu, cpu->k);
  cpu_pushWord(cpu, cpu->pc, false);
  cpu_pushByte(cpu, cpu_getFlags(cpu));
  cpu->i = true;
  cpu->d = false;
  cpu->k = 0;
  cpu->pc = cpu_readWord(cpu, 0xffe4, 0xffe5, true);
}

static inline void cpu_op_03(Cpu* cpu) { // ora sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_04(Cpu* cpu) { // tsb dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_tsb(cpu, low, high);
}

static inline void cpu_op_05(Cpu* cpu) { // ora dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_06(Cpu* cpu) { // asl dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_asl(cpu, low, high);
}

static inline void cpu_op_07(Cpu* cpu) { // ora idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_08(Cpu* cpu) { // php imp
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_pushByte(cpu, cpu_getFlags(cpu));
}

static inline void cpu_op_09(Cpu* cpu) { // ora imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_0a(Cpu* cpu) { // asla imp
  cpu_adrImp(cpu);
  if(cpu->mf) {
    cpu->c = cpu->a & 0x80;
    cpu->a = (cpu->a & 0xff00) | ((cpu->a << 1) & 0xff);
  } else {
    cpu->c = cpu->a & 0x8000;
    cpu->a <<= 1;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_0b(Cpu* cpu) { // phd imp
  cpu_idle(cpu);
  cpu_pushWord(cpu, cpu->dp, true);
}

static inline void cpu_op_0c(Cpu* cpu) { // tsb abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_tsb(cpu, low, high);
}

static inline void cpu_op_0d(Cpu* cpu) { // ora abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_0e(Cpu* cpu) { // asl abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_asl(cpu, low, high);
}

static inline void cpu_op_0f(Cpu* cpu) { // ora abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_10(Cpu* cpu) { // bpl rel
  cpu_doBranch(cpu, !cpu->n);
}

static inline void cpu_op_11(Cpu* cpu) { // ora idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_12(Cpu* cpu) { // ora idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_13(Cpu* cpu) { // ora isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_14(Cpu* cpu) { // trb dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_trb(cpu, low, high);
}

static inline void cpu_op_15(Cpu* cpu) { // ora dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_16(Cpu* cpu) { // asl dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_asl(cpu, low, high);
}

static inline void cpu_op_17(Cpu* cpu) { // ora ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_18(Cpu* cpu) { // clc imp
  cpu_adrImp(cpu);
  cpu->c = false;
}

static inline void cpu_op_19(Cpu* cpu) { // ora aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_1a(Cpu* cpu) { // inca imp
  cpu_adrImp(cpu);
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | ((cpu->a + 1) & 0xff);
  } else {
    cpu->a++;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_1b(Cpu* cpu) { // tcs imp
  cpu_adrImp(cpu);
  cpu->sp = cpu->a;
}

static inline void cpu_op_1c(Cpu* cpu) { // trb abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_trb(cpu, low, high);
}

static inline void cpu_op_1d(Cpu* cpu) { // ora abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_1e(Cpu* cpu) { // asl abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_asl(cpu, low, high);
}

static inline void cpu_op_1f(Cpu* cpu) { // ora alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_ora(cpu, low, high);
}

static inline void cpu_op_20(Cpu* cpu) { // jsr abs
  uint16_t value = cpu_readOpcodeWord(cpu, false);
  cpu_idle(cpu);
  cpu_pushWord(cpu, cpu->pc - 1, true);
  cpu->pc = value;
}

static inline void cpu_op_21(Cpu* cpu) { // and idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_22(Cpu* cpu) { // jsl abl
  uint16_t value = cpu_readOpcodeWord(cpu, false);
  cpu_pushByte(cpu, cpu->k);
  cpu_idle(cpu);
  uint8_t newK = cpu_readOpcode(cpu);
  cpu_pushWord(cpu, cpu->pc - 1, true);
  cpu->pc = value;
  cpu->k = newK;
}

static inline void cpu_op_23(Cpu* cpu) { // and sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_24(Cpu* cpu) { // bit dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_bit(cpu, low, high);
}

static inline void cpu_op_25(Cpu* cpu) { // and dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_26(Cpu* cpu) { // rol dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_rol(cpu, low, high);
}

static inline void cpu_op_27(Cpu* cpu) { // and idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_28(Cpu* cpu) { // plp imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_setFlags(cpu, cpu_pullByte(cpu));
}

static inline void cpu_op_29(Cpu* cpu) { // and imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_2a(Cpu* cpu) { // rola imp
  cpu_adrImp(cpu);
  int result = (cpu->a << 1) | cpu->c;
  if(cpu->mf) {
    cpu->c = result & 0x100;
    cpu->a = (cpu->a & 0xff00) | (result & 0xff);
  } else {
    cpu->c = result & 0x10000;
    cpu->a = result;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_2b(Cpu* cpu) { // pld imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu->dp = cpu_pullWord(cpu, true);
  cpu_setZN(cpu, cpu->dp, false);
}

static inline void cpu_op_2c(Cpu* cpu) { // bit abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_bit(cpu, low, high);
}

static inline void cpu_op_2d(Cpu* cpu) { // and abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_2e(Cpu* cpu) { // rol abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_rol(cpu, low, high);
}

static inline void cpu_op_2f(Cpu* cpu) { // and abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_30(Cpu* cpu) { // bmi rel
  cpu_doBranch(cpu, cpu->n);
}

static inline void cpu_op_31(Cpu* cpu) { // and idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_32(Cpu* cpu) { // and idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_33(Cpu* cpu) { // and isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_34(Cpu* cpu) { // bit dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_bit(cpu, low, high);
}

static inline void cpu_op_35(Cpu* cpu) { // and dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_36(Cpu* cpu) { // rol dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_rol(cpu, low, high);
}

static inline void cpu_op_37(Cpu* cpu) { // and ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_38(Cpu* cpu) { // sec imp
  cpu_adrImp(cpu);
  cpu->c = true;
}

static inline void cpu_op_39(Cpu* cpu) { // and aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_3a(Cpu* cpu) { // deca imp
  cpu_adrImp(cpu);
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | ((cpu->a - 1) & 0xff);
  } else {
    cpu->a--;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_3b(Cpu* cpu) { // tsc imp
  cpu_adrImp(cpu);
  cpu->a = cpu->sp;
  cpu_setZN(cpu, cpu->a, false);
}

static inline void cpu_op_3c(Cpu* cpu) { // bit abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_bit(cpu, low, high);
}

static inline void cpu_op_3d(Cpu* cpu) { // and abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_3e(Cpu* cpu) { // rol abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_rol(cpu, low, high);
}

static inline void cpu_op_3f(Cpu* cpu) { // and alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_and(cpu, low, high);
}

static inline void cpu_op_40(Cpu* cpu) { // rti imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu_setFlags(cpu, cpu_pullByte(cpu));
  cpu->pc = cpu_pullWord(cpu, false);
  cpu_checkInt(cpu);
  cpu->k = cpu_pullByte(cpu);
}

static inline void cpu_op_41(Cpu* cpu) { // eor idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_42(Cpu* cpu) { // wdm imm(s)
  cpu_checkInt(cpu);
  cpu_readOpcode(cpu);
}

static inline void cpu_op_43(Cpu* cpu) { // eor sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_44(Cpu* cpu) { // mvp bm
  uint8_t dest = cpu_readOpcode(cpu);
  uint8_t src = cpu_readOpcode(cpu);
  cpu->db = dest;
  cpu_write(cpu, (dest << 16) | cpu->y, cpu_read(cpu, (src << 16) | cpu->x));
  cpu->a--;
  cpu->x--;
  cpu->y--;
  if(cpu->a != 0xffff) {
    cpu->pc -= 3;
  }
  if(cpu->xf) {
    cpu->x &= 0xff;
    cpu->y &= 0xff;
  }
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_45(Cpu* cpu) { // eor dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_46(Cpu* cpu) { // lsr dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_lsr(cpu, low, high);
}

static inline void cpu_op_47(Cpu* cpu) { // eor idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_48(Cpu* cpu) { // pha imp
  cpu_idle(cpu);
  if(cpu->mf) {
    cpu_checkInt(cpu);
    cpu_pushByte(cpu, cpu->a);
  } else {
    cpu_pushWord(cpu, cpu->a, true);
  }
}

static inline void cpu_op_49(Cpu* cpu) { // eor imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_4a(Cpu* cpu) { // lsra imp
  cpu_adrImp(cpu);
  cpu->c = cpu->a & 1;
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | ((cpu->a >> 1) & 0x7f);
  } else {
    cpu->a >>= 1;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_4b(Cpu* cpu) { // phk imp
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_pushByte(cpu, cpu->k);
}

static inline void cpu_op_4c(Cpu* cpu) { // jmp abs
  cpu->pc = cpu_readOpcodeWord(cpu, true);
}

static inline void cpu_op_4d(Cpu* cpu) { // eor abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_4e(Cpu* cpu) { // lsr abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_lsr(cpu, low, high);
}

static inline void cpu_op_4f(Cpu* cpu) { // eor abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_50(Cpu* cpu) { // bvc rel
  cpu_doBranch(cpu, !cpu->v);
}

static inline void cpu_op_51(Cpu* cpu) { // eor idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_52(Cpu* cpu) { // eor idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_53(Cpu* cpu) { // eor isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_54(Cpu* cpu) { // mvn bm
  uint8_t dest = cpu_readOpcode(cpu);
  uint8_t src = cpu_readOpcode(cpu);
  cpu->db = dest;
  cpu_write(cpu, (dest << 16) | cpu->y, cpu_read(cpu, (src << 16) | cpu->x));
  cpu->a--;
  cpu->x++;
  cpu->y++;
  if(cpu->a != 0xffff) {
    cpu->pc -= 3;
  }
  if(cpu->xf) {
    cpu->x &= 0xff;
    cpu->y &= 0xff;
  }
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_55(Cpu* cpu) { // eor dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_56(Cpu* cpu) { // lsr dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_lsr(cpu, low, high);
}

static inline void cpu_op_57(Cpu* cpu) { // eor ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_58(Cpu* cpu) { // cli imp
  cpu_adrImp(cpu);
  cpu->i = false;
}

static inline void cpu_op_59(Cpu* cpu) { // eor aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_5a(Cpu* cpu) { // phy imp
  cpu_idle(cpu);
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu_pushByte(cpu, cpu->y);
  } else {
    cpu_pushWord(cpu, cpu->y, true);
  }
}

static inline void cpu_op_5b(Cpu* cpu) { // tcd imp
  cpu_adrImp(cpu);
  cpu->dp = cpu->a;
  cpu_setZN(cpu, cpu->dp, false);
}

static inline void cpu_op_5c(Cpu* cpu) { // jml abl
  uint16_t value = cpu_readOpcodeWord(cpu, false);
  cpu_checkInt(cpu);
  cpu->k = cpu_readOpcode(cpu);
  cpu->pc = value;
}

static inline void cpu_op_5d(Cpu* cpu) { // eor abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_5e(Cpu* cpu) { // lsr abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_lsr(cpu, low, high);
}

static inline void cpu_op_5f(Cpu* cpu) { // eor alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_eor(cpu, low, high);
}

static inline void cpu_op_60(Cpu* cpu) { // rts imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu->pc = cpu_pullWord(cpu, false) + 1;
  cpu_checkInt(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_61(Cpu* cpu) { // adc idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_62(Cpu* cpu) { // per rll
  uint16_t value = cpu_readOpcodeWord(cpu, false);
  cpu_idle(cpu);
  cpu_pushWord(cpu, cpu->pc + (int16_t) value, true);
}

static inline void cpu_op_63(Cpu* cpu) { // adc sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_64(Cpu* cpu) { // stz dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_stz(cpu, low, high);
}

static inline void cpu_op_65(Cpu* cpu) { // adc dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_66(Cpu* cpu) { // ror dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_ror(cpu, low, high);
}

static inline void cpu_op_67(Cpu* cpu) { // adc idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_68(Cpu* cpu) { // pla imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  if(cpu->mf) {
    cpu_checkInt(cpu);
    cpu->a = (cpu->a & 0xff00) | cpu_pullByte(cpu);
  } else {
    cpu->a = cpu_pullWord(cpu, true);
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_69(Cpu* cpu) { // adc imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_6a(Cpu* cpu) { // rora imp
  cpu_adrImp(cpu);
  bool carry = cpu->a & 1;
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | ((cpu->a >> 1) & 0x7f) | (cpu->c << 7);
  } else {
    cpu->a = (cpu->a >> 1) | (cpu->c << 15);
  }
  cpu->c = carry;
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_6b(Cpu* cpu) { // rtl imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu->pc = cpu_pullWord(cpu, false) + 1;
  cpu_checkInt(cpu);
  cpu->k = cpu_pullByte(cpu);
}

static inline void cpu_op_6c(Cpu* cpu) { // jmp ind
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  cpu->pc = cpu_readWord(cpu, adr, (adr + 1) & 0xffff, true);
}

static inline void cpu_op_6d(Cpu* cpu) { // adc abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_6e(Cpu* cpu) { // ror abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_ror(cpu, low, high);
}

static inline void cpu_op_6f(Cpu* cpu) { // adc abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_70(Cpu* cpu) { // bvs rel
  cpu_doBranch(cpu, cpu->v);
}

static inline void cpu_op_71(Cpu* cpu) { // adc idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_72(Cpu* cpu) { // adc idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_73(Cpu* cpu) { // adc isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_74(Cpu* cpu) { // stz dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_stz(cpu, low, high);
}

static inline void cpu_op_75(Cpu* cpu) { // adc dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_76(Cpu* cpu) { // ror dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_ror(cpu, low, high);
}

static inline void cpu_op_77(Cpu* cpu) { // adc ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_78(Cpu* cpu) { // sei imp
  cpu_adrImp(cpu);
  cpu->i = true;
}

static inline void cpu_op_79(Cpu* cpu) { // adc aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_7a(Cpu* cpu) { // ply imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu->y = cpu_pullByte(cpu);
  } else {
    cpu->y = cpu_pullWord(cpu, true);
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static inline void cpu_op_7b(Cpu* cpu) { // tdc imp
  cpu_adrImp(cpu);
  cpu->a = cpu->dp;
  cpu_setZN(cpu, cpu->a, false);
}

static inline void cpu_op_7c(Cpu* cpu) { // jmp iax
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  cpu_idle(cpu);
  cpu->pc = cpu_readWord(cpu, (cpu->k << 16) | ((adr + cpu->x) & 0xffff), (cpu->k << 16) | ((adr + cpu->x + 1) & 0xffff), true);
}

static inline void cpu_op_7d(Cpu* cpu) { // adc abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_7e(Cpu* cpu) { // ror abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_ror(cpu, low, high);
}

static inline void cpu_op_7f(Cpu* cpu) { // adc alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_adc(cpu, low, high);
}

static inline void cpu_op_80(Cpu* cpu) { // bra rel
  cpu_doBranch(cpu, true);
}

static inline void cpu_op_81(Cpu* cpu) { // sta idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_82(Cpu* cpu) { // brl rll
  cpu->pc += (int16_t) cpu_readOpcodeWord(cpu, false);
  cpu_checkInt(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_83(Cpu* cpu) { // sta sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_84(Cpu* cpu) { // sty dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_sty(cpu, low, high);
}

static inline void cpu_op_85(Cpu* cpu) { // sta dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_86(Cpu* cpu) { // stx dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_stx(cpu, low, high);
}

static inline void cpu_op_87(Cpu* cpu) { // sta idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_88(Cpu* cpu) { // dey imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->y = (cpu->y - 1) & 0xff;
  } else {
    cpu->y--;
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static inline void cpu_op_89(Cpu* cpu) { // biti imm(m)
  if(cpu->mf) {
    cpu_checkInt(cpu);
    uint8_t result = (cpu->a & 0xff) & cpu_readOpcode(cpu);
    cpu->z = result == 0;
  } else {
    uint16_t result = cpu->a & cpu_readOpcodeWord(cpu, true);
    cpu->z = result == 0;
  }
}

static inline void cpu_op_8a(Cpu* cpu) { // txa imp
  cpu_adrImp(cpu);
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | (cpu->x & 0xff);
  } else {
    cpu->a = cpu->x;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_8b(Cpu* cpu) { // phb imp
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_pushByte(cpu, cpu->db);
}

static inline void cpu_op_8c(Cpu* cpu) { // sty abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_sty(cpu, low, high);
}

static inline void cpu_op_8d(Cpu* cpu) { // sta abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_8e(Cpu* cpu) { // stx abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_stx(cpu, low, high);
}

static inline void cpu_op_8f(Cpu* cpu) { // sta abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_90(Cpu* cpu) { // bcc rel
  cpu_doBranch(cpu, !cpu->c);
}

static inline void cpu_op_91(Cpu* cpu) { // sta idy
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, true);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_92(Cpu* cpu) { // sta idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_93(Cpu* cpu) { // sta isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_94(Cpu* cpu) { // sty dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_sty(cpu, low, high);
}

static inline void cpu_op_95(Cpu* cpu) { // sta dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_96(Cpu* cpu) { // stx dpy
  uint32_t low = 0;
  uint32_t high = cpu_adrDpy(cpu, &low);
  cpu_stx(cpu, low, high);
}

static inline void cpu_op_97(Cpu* cpu) { // sta ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_98(Cpu* cpu) { // tya imp
  cpu_adrImp(cpu);
  if(cpu->mf) {
    cpu->a = (cpu->a & 0xff00) | (cpu->y & 0xff);
  } else {
    cpu->a = cpu->y;
  }
  cpu_setZN(cpu, cpu->a, cpu->mf);
}

static inline void cpu_op_99(Cpu* cpu) { // sta aby
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, true);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_9a(Cpu* cpu) { // txs imp
  cpu_adrImp(cpu);
  cpu->sp = cpu->x;
}

static inline void cpu_op_9b(Cpu* cpu) { // txy imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->y = cpu->x & 0xff;
  } else {
    cpu->y = cpu->x;
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static inline void cpu_op_9c(Cpu* cpu) { // stz abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_stz(cpu, low, high);
}

static inline void cpu_op_9d(Cpu* cpu) { // sta abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_9e(Cpu* cpu) { // stz abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_stz(cpu, low, high);
}

static inline void cpu_op_9f(Cpu* cpu) { // sta alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_sta(cpu, low, high);
}

static inline void cpu_op_a0(Cpu* cpu) { // ldy imm(x)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, true);
  cpu_ldy(cpu, low, high);
}

static inline void cpu_op_a1(Cpu* cpu) { // lda idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_a2(Cpu* cpu) { // ldx imm(x)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, true);
  cpu_ldx(cpu, low, high);
}

static inline void cpu_op_a3(Cpu* cpu) { // lda sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_a4(Cpu* cpu) { // ldy dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_ldy(cpu, low, high);
}

static inline void cpu_op_a5(Cpu* cpu) { // lda dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_a6(Cpu* cpu) { // ldx dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_ldx(cpu, low, high);
}

static inline void cpu_op_a7(Cpu* cpu) { // lda idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_a8(Cpu* cpu) { // tay imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->y = cpu->a & 0xff;
  } else {
    cpu->y = cpu->a;
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static inline void cpu_op_a9(Cpu* cpu) { // lda imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_aa(Cpu* cpu) { // tax imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->x = cpu->a & 0xff;
  } else {
    cpu->x = cpu->a;
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_ab(Cpu* cpu) { // plb imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu->db = cpu_pullByte(cpu);
  cpu_setZN(cpu, cpu->db, true);
}

static inline void cpu_op_ac(Cpu* cpu) { // ldy abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_ldy(cpu, low, high);
}

static inline void cpu_op_ad(Cpu* cpu) { // lda abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_ae(Cpu* cpu) { // ldx abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_ldx(cpu, low, high);
}

static inline void cpu_op_af(Cpu* cpu) { // lda abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b0(Cpu* cpu) { // bcs rel
  cpu_doBranch(cpu, cpu->c);
}

static inline void cpu_op_b1(Cpu* cpu) { // lda idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b2(Cpu* cpu) { // lda idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b3(Cpu* cpu) { // lda isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b4(Cpu* cpu) { // ldy dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_ldy(cpu, low, high);
}

static inline void cpu_op_b5(Cpu* cpu) { // lda dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b6(Cpu* cpu) { // ldx dpy
  uint32_t low = 0;
  uint32_t high = cpu_adrDpy(cpu, &low);
  cpu_ldx(cpu, low, high);
}

static inline void cpu_op_b7(Cpu* cpu) { // lda ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_b8(Cpu* cpu) { // clv imp
  cpu_adrImp(cpu);
  cpu->v = false;
}

static inline void cpu_op_b9(Cpu* cpu) { // lda aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_ba(Cpu* cpu) { // tsx imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->x = cpu->sp & 0xff;
  } else {
    cpu->x = cpu->sp;
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_bb(Cpu* cpu) { // tyx imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->x = cpu->y & 0xff;
  } else {
    cpu->x = cpu->y;
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_bc(Cpu* cpu) { // ldy abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_ldy(cpu, low, high);
}

static inline void cpu_op_bd(Cpu* cpu) { // lda abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_be(Cpu* cpu) { // ldx aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_ldx(cpu, low, high);
}

static inline void cpu_op_bf(Cpu* cpu) { // lda alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_lda(cpu, low, high);
}

static inline void cpu_op_c0(Cpu* cpu) { // cpy imm(x)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, true);
  cpu_cpy(cpu, low, high);
}

static inline void cpu_op_c1(Cpu* cpu) { // cmp idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_c2(Cpu* cpu) { // rep imm(s)
  uint8_t val = cpu_readOpcode(cpu);
  cpu_checkInt(cpu);
  cpu_setFlags(cpu, cpu_getFlags(cpu) & ~val);
  cpu_idle(cpu);
}

static inline void cpu_op_c3(Cpu* cpu) { // cmp sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_c4(Cpu* cpu) { // cpy dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_cpy(cpu, low, high);
}

static inline void cpu_op_c5(Cpu* cpu) { // cmp dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_c6(Cpu* cpu) { // dec dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_dec(cpu, low, high);
}

static inline void cpu_op_c7(Cpu* cpu) { // cmp idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_c8(Cpu* cpu) { // iny imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->y = (cpu->y + 1) & 0xff;
  } else {
    cpu->y++;
  }
  cpu_setZN(cpu, cpu->y, cpu->xf);
}

static inline void cpu_op_c9(Cpu* cpu) { // cmp imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_ca(Cpu* cpu) { // dex imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->x = (cpu->x - 1) & 0xff;
  } else {
    cpu->x--;
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_cb(Cpu* cpu) { // wai imp
  cpu->waiting = true;
  cpu_idle(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_cc(Cpu* cpu) { // cpy abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_cpy(cpu, low, high);
}

static inline void cpu_op_cd(Cpu* cpu) { // cmp abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_ce(Cpu* cpu) { // dec abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_dec(cpu, low, high);
}

static inline void cpu_op_cf(Cpu* cpu) { // cmp abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d0(Cpu* cpu) { // bne rel
  cpu_doBranch(cpu, !cpu->z);
}

static inline void cpu_op_d1(Cpu* cpu) { // cmp idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d2(Cpu* cpu) { // cmp idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d3(Cpu* cpu) { // cmp isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d4(Cpu* cpu) { // pei dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_pushWord(cpu, cpu_readWord(cpu, low, high, false), true);
}

static inline void cpu_op_d5(Cpu* cpu) { // cmp dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d6(Cpu* cpu) { // dec dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_dec(cpu, low, high);
}

static inline void cpu_op_d7(Cpu* cpu) { // cmp ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_d8(Cpu* cpu) { // cld imp
  cpu_adrImp(cpu);
  cpu->d = false;
}

static inline void cpu_op_d9(Cpu* cpu) { // cmp aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_da(Cpu* cpu) { // phx imp
  cpu_idle(cpu);
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu_pushByte(cpu, cpu->x);
  } else {
    cpu_pushWord(cpu, cpu->x, true);
  }
}

static inline void cpu_op_db(Cpu* cpu) { // stp imp
  cpu->stopped = true;
  cpu_idle(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_dc(Cpu* cpu) { // jml ial
  uint16_t adr = cpu_readOpcodeWord(cpu, false);
  cpu->pc = cpu_readWord(cpu, adr, (adr + 1) & 0xffff, false);
  cpu_checkInt(cpu);
  cpu->k = cpu_read(cpu, (adr + 2) & 0xffff);
}

static inline void cpu_op_dd(Cpu* cpu) { // cmp abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_de(Cpu* cpu) { // dec abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_dec(cpu, low, high);
}

static inline void cpu_op_df(Cpu* cpu) { // cmp alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_cmp(cpu, low, high);
}

static inline void cpu_op_e0(Cpu* cpu) { // cpx imm(x)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, true);
  cpu_cpx(cpu, low, high);
}

static inline void cpu_op_e1(Cpu* cpu) { // sbc idx
  uint32_t low = 0;
  uint32_t high = cpu_adrIdx(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_e2(Cpu* cpu) { // sep imm(s)
  uint8_t val = cpu_readOpcode(cpu);
  cpu_checkInt(cpu);
  cpu_setFlags(cpu, cpu_getFlags(cpu) | val);
  cpu_idle(cpu);
}

static inline void cpu_op_e3(Cpu* cpu) { // sbc sr
  uint32_t low = 0;
  uint32_t high = cpu_adrSr(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_e4(Cpu* cpu) { // cpx dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_cpx(cpu, low, high);
}

static inline void cpu_op_e5(Cpu* cpu) { // sbc dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_e6(Cpu* cpu) { // inc dp
  uint32_t low = 0;
  uint32_t high = cpu_adrDp(cpu, &low);
  cpu_inc(cpu, low, high);
}

static inline void cpu_op_e7(Cpu* cpu) { // sbc idl
  uint32_t low = 0;
  uint32_t high = cpu_adrIdl(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_e8(Cpu* cpu) { // inx imp
  cpu_adrImp(cpu);
  if(cpu->xf) {
    cpu->x = (cpu->x + 1) & 0xff;
  } else {
    cpu->x++;
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_e9(Cpu* cpu) { // sbc imm(m)
  uint32_t low = 0;
  uint32_t high = cpu_adrImm(cpu, &low, false);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_ea(Cpu* cpu) { // nop imp
  cpu_adrImp(cpu);
  // no operation
}

static inline void cpu_op_eb(Cpu* cpu) { // xba imp
  uint8_t low = cpu->a & 0xff;
  uint8_t high = cpu->a >> 8;
  cpu->a = (low << 8) | high;
  cpu_setZN(cpu, high, true);
  cpu_idle(cpu);
  cpu_checkInt(cpu);
  cpu_idle(cpu);
}

static inline void cpu_op_ec(Cpu* cpu) { // cpx abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_cpx(cpu, low, high);
}

static inline void cpu_op_ed(Cpu* cpu) { // sbc abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_ee(Cpu* cpu) { // inc abs
  uint32_t low = 0;
  uint32_t high = cpu_adrAbs(cpu, &low);
  cpu_inc(cpu, low, high);
}

static inline void cpu_op_ef(Cpu* cpu) { // sbc abl
  uint32_t low = 0;
  uint32_t high = cpu_adrAbl(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f0(Cpu* cpu) { // beq rel
  cpu_doBranch(cpu, cpu->z);
}

static inline void cpu_op_f1(Cpu* cpu) { // sbc idy(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrIdy(cpu, &low, false);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f2(Cpu* cpu) { // sbc idp
  uint32_t low = 0;
  uint32_t high = cpu_adrIdp(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f3(Cpu* cpu) { // sbc isy
  uint32_t low = 0;
  uint32_t high = cpu_adrIsy(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f4(Cpu* cpu) { // pea imm(l)
  cpu_pushWord(cpu, cpu_readOpcodeWord(cpu, false), true);
}

static inline void cpu_op_f5(Cpu* cpu) { // sbc dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f6(Cpu* cpu) { // inc dpx
  uint32_t low = 0;
  uint32_t high = cpu_adrDpx(cpu, &low);
  cpu_inc(cpu, low, high);
}

static inline void cpu_op_f7(Cpu* cpu) { // sbc ily
  uint32_t low = 0;
  uint32_t high = cpu_adrIly(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_f8(Cpu* cpu) { // sed imp
  cpu_adrImp(cpu);
  cpu->d = true;
}

static inline void cpu_op_f9(Cpu* cpu) { // sbc aby(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAby(cpu, &low, false);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_fa(Cpu* cpu) { // plx imp
  cpu_idle(cpu);
  cpu_idle(cpu);
  if(cpu->xf) {
    cpu_checkInt(cpu);
    cpu->x = cpu_pullByte(cpu);
  } else {
    cpu->x = cpu_pullWord(cpu, true);
  }
  cpu_setZN(cpu, cpu->x, cpu->xf);
}

static inline void cpu_op_fb(Cpu* cpu) { // xce imp
  cpu_adrImp(cpu);
  bool temp = cpu->c;
  cpu->c = cpu->e;
  cpu->e = temp;
  cpu_setFlags(cpu, cpu_getFlags(cpu)); // updates x and m flags, clears upper half of x and y if needed
}

static inline void cpu_op_fc(Cpu* cpu) { // jsr iax
  uint8_t adrl = cpu_readOpcode(cpu);
  cpu_pushWord(cpu, cpu->pc, false);
  uint16_t adr = adrl | (cpu_readOpcode(cpu) << 8);
  cpu_idle(cpu);
  uint16_t value = cpu_readWord(cpu, (cpu->k << 16) | ((adr + cpu->x) & 0xffff), (cpu->k << 16) | ((adr + cpu->x + 1) & 0xffff), true);
  cpu->pc = value;
}

static inline void cpu_op_fd(Cpu* cpu) { // sbc abx(r)
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, false);
  cpu_sbc(cpu, low, high);
}

static inline void cpu_op_fe(Cpu* cpu) { // inc abx
  uint32_t low = 0;
  uint32_t high = cpu_adrAbx(cpu, &low, true);
  cpu_inc(cpu, low, high);
}

static inline void cpu_op_ff(Cpu* cpu) { // sbc alx
  uint32_t low = 0;
  uint32_t high = cpu_adrAlx(cpu, &low);
  cpu_sbc(cpu, low, high);
}

static void cpu_doOpcode(Cpu* cpu, uint8_t opcode) {
  switch(opcode) {
    case 0x00: cpu_op_00(cpu); break;
    case 0x01: cpu_op_01(cpu); break;
    case 0x02: cpu_op_02(cpu); break;
    case 0x03: cpu_op_03(cpu); break;
    case 0x04: cpu_op_04(cpu); break;
    case 0x05: cpu_op_05(cpu); break;
    case 0x06: cpu_op_06(cpu); break;
    case 0x07: cpu_op_07(cpu); break;
    case 0x08: cpu_op_08(cpu); break;
    case 0x09: cpu_op_09(cpu); break;
    case 0x0a: cpu_op_0a(cpu); break;
    case 0x0b: cpu_op_0b(cpu); break;
    case 0x0c: cpu_op_0c(cpu); break;
    case 0x0d: cpu_op_0d(cpu); break;
    case 0x0e: cpu_op_0e(cpu); break;
    case 0x0f: cpu_op_0f(cpu); break;
    case 0x10: cpu_op_10(cpu); break;
    case 0x11: cpu_op_11(cpu); break;
    case 0x12: cpu_op_12(cpu); break;
    case 0x13: cpu_op_13(cpu); break;
    case 0x14: cpu_op_14(cpu); break;
    case 0x15: cpu_op_15(cpu); break;
    case 0x16: cpu_op_16(cpu); break;
    case 0x17: cpu_op_17(cpu); break;
    case 0x18: cpu_op_18(cpu); break;
    case 0x19: cpu_op_19(cpu); break;
    case 0x1a: cpu_op_1a(cpu); break;
    case 0x1b: cpu_op_1b(cpu); break;
    case 0x1c: cpu_op_1c(cpu); break;
    case 0x1d: cpu_op_1d(cpu); break;
    case 0x1e: cpu_op_1e(cpu); break;
    case 0x1f: cpu_op_1f(cpu); break;
    case 0x20: cpu_op_20(cpu); break;
    case 0x21: cpu_op_21(cpu); break;
    case 0x22: cpu_op_22(cpu); break;
    case 0x23: cpu_op_23(cpu); break;
    case 0x24: cpu_op_24(cpu); break;
    case 0x25: cpu_op_25(cpu); break;
    case 0x26: cpu_op_26(cpu); break;
    case 0x27: cpu_op_27(cpu); break;
    case 0x28: cpu_op_28(cpu); break;
    case 0x29: cpu_op_29(cpu); break;
    case 0x2a: cpu_op_2a(cpu); break;
    case 0x2b: cpu_op_2b(cpu); break;
    case 0x2c: cpu_op_2c(cpu); break;
    case 0x2d: cpu_op_2d(cpu); break;
    case 0x2e: cpu_op_2e(cpu); break;
    case 0x2f: cpu_op_2f(cpu); break;
    case 0x30: cpu_op_30(cpu); break;
    case 0x31: cpu_op_31(cpu); break;
    case 0x32: cpu_op_32(cpu); break;
    case 0x33: cpu_op_33(cpu); break;
    case 0x34: cpu_op_34(cpu); break;
    case 0x35: cpu_op_35(cpu); break;
    case 0x36: cpu_op_36(cpu); break;
    case 0x37: cpu_op_37(cpu); break;
    case 0x38: cpu_op_38(cpu); break;
    case 0x39: cpu_op_39(cpu); break;
    case 0x3a: cpu_op_3a(cpu); break;
    case 0x3b: cpu_op_3b(cpu); break;
    case 0x3c: cpu_op_3c(cpu); break;
    case 0x3d: cpu_op_3d(cpu); break;
    case 0x3e: cpu_op_3e(cpu); break;
    case 0x3f: cpu_op_3f(cpu); break;
    case 0x40: cpu_op_40(cpu); break;
    case 0x41: cpu_op_41(cpu); break;
    case 0x42: cpu_op_42(cpu); break;
    case 0x43: cpu_op_43(cpu); break;
    case 0x44: cpu_op_44(cpu); break;
    case 0x45: cpu_op_45(cpu); break;
    case 0x46: cpu_op_46(cpu); break;
    case 0x47: cpu_op_47(cpu); break;
    case 0x48: cpu_op_48(cpu); break;
    case 0x49: cpu_op_49(cpu); break;
    case 0x4a: cpu_op_4a(cpu); break;
    case 0x4b: cpu_op_4b(cpu); break;
    case 0x4c: cpu_op_4c(cpu); break;
    case 0x4d: cpu_op_4d(cpu); break;
    case 0x4e: cpu_op_4e(cpu); break;
    case 0x4f: cpu_op_4f(cpu); break;
    case 0x50: cpu_op_50(cpu); break;
    case 0x51: cpu_op_51(cpu); break;
    case 0x52: cpu_op_52(cpu); break;
    case 0x53: cpu_op_53(cpu); break;
    case 0x54: cpu_op_54(cpu); break;
    case 0x55: cpu_op_55(cpu); break;
    case 0x56: cpu_op_56(cpu); break;
    case 0x57: cpu_op_57(cpu); break;
    case 0x58: cpu_op_58(cpu); break;
    case 0x59: cpu_op_59(cpu); break;
    case 0x5a: cpu_op_5a(cpu); break;
    case 0x5b: cpu_op_5b(cpu); break;
    case 0x5c: cpu_op_5c(cpu); break;
    case 0x5d: cpu_op_5d(cpu); break;
    case 0x5e: cpu_op_5e(cpu); break;
    case 0x5f: cpu_op_5f(cpu); break;
    case 0x60: cpu_op_60(cpu); break;
    case 0x61: cpu_op_61(cpu); break;
    case 0x62: cpu_op_62(cpu); break;
    case 0x63: cpu_op_63(cpu); break;
    case 0x64: cpu_op_64(cpu); break;
    case 0x65: cpu_op_65(cpu); break;
    case 0x66: cpu_op_66(cpu); break;
    case 0x67: cpu_op_67(cpu); break;
    case 0x68: cpu_op_68(cpu); break;
    case 0x69: cpu_op_69(cpu); break;
    case 0x6a: cpu_op_6a(cpu); break;
    case 0x6b: cpu_op_6b(cpu); break;
    case 0x6c: cpu_op_6c(cpu); break;
    case 0x6d: cpu_op_6d(cpu); break;
    case 0x6e: cpu_op_6e(cpu); break;
    case 0x6f: cpu_op_6f(cpu); break;
    case 0x70: cpu_op_70(cpu); break;
    case 0x71: cpu_op_71(cpu); break;
    case 0x72: cpu_op_72(cpu); break;
    case 0x73: cpu_op_73(cpu); break;
    case 0x74: cpu_op_74(cpu); break;
    case 0x75: cpu_op_75(cpu); break;
    case 0x76: cpu_op_76(cpu); break;
    case 0x77: cpu_op_77(cpu); break;
    case 0x78: cpu_op_78(cpu); break;
    case 0x79: cpu_op_79(cpu); break;
    case 0x7a: cpu_op_7a(cpu); break;
    case 0x7b: cpu_op_7b(cpu); break;
    case 0x7c: cpu_op_7c(cpu); break;
    case 0x7d: cpu_op_7d(cpu); break;
    case 0x7e: cpu_op_7e(cpu); break;
    case 0x7f: cpu_op_7f(cpu); break;
    case 0x80: cpu_op_80(cpu); break;
    case 0x81: cpu_op_81(cpu); break;
    case 0x82: cpu_op_82(cpu); break;
    case 0x83: cpu_op_83(cpu); break;
    case 0x84: cpu_op_84(cpu); break;
    case 0x85: cpu_op_85(cpu); break;
    case 0x86: cpu_op_86(cpu); break;
    case 0x87: cpu_op_87(cpu); break;
    case 0x88: cpu_op_88(cpu); break;
    case 0x89: cpu_op_89(cpu); break;
    case 0x8a: cpu_op_8a(cpu); break;
    case 0x8b: cpu_op_8b(cpu); break;
    case 0x8c: cpu_op_8c(cpu); break;
    case 0x8d: cpu_op_8d(cpu); break;
    case 0x8e: cpu_op_8e(cpu); break;
    case 0x8f: cpu_op_8f(cpu); break;
    case 0x90: cpu_op_90(cpu); break;
    case 0x91: cpu_op_91(cpu); break;
    case 0x92: cpu_op_92(cpu); break;
    case 0x93: cpu_op_93(cpu); break;
    case 0x94: cpu_op_94(cpu); break;
    case 0x95: cpu_op_95(cpu); break;
    case 0x96: cpu_op_96(cpu); break;
    case 0x97: cpu_op_97(cpu); break;
    case 0x98: cpu_op_98(cpu); break;
    case 0x99: cpu_op_99(cpu); break;
    case 0x9a: cpu_op_9a(cpu); break;
    case 0x9b: cpu_op_9b(cpu); break;
    case 0x9c: cpu_op_9c(cpu); break;
    case 0x9d: cpu_op_9d(cpu); break;
    case 0x9e: cpu_op_9e(cpu); break;
    case 0x9f: cpu_op_9f(cpu); break;
    case 0xa0: cpu_op_a0(cpu); break;
    case 0xa1: cpu_op_a1(cpu); break;
    case 0xa2: cpu_op_a2(cpu); break;
    case 0xa3: cpu_op_a3(cpu); break;
    case 0xa4: cpu_op_a4(cpu); break;
    case 0xa5: cpu_op_a5(cpu); break;
    case 0xa6: cpu_op_a6(cpu); break;
    case 0xa7: cpu_op_a7(cpu); break;
    case 0xa8: cpu_op_a8(cpu); break;
    case 0xa9: cpu_op_a9(cpu); break;
    case 0xaa: cpu_op_aa(cpu); break;
    case 0xab: cpu_op_ab(cpu); break;
    case 0xac: cpu_op_ac(cpu); break;
    case 0xad: cpu_op_ad(cpu); break;
    case 0xae: cpu_op_ae(cpu); break;
    case 0xaf: cpu_op_af(cpu); break;
    case 0xb0: cpu_op_b0(cpu); break;
    case 0xb1: cpu_op_b1(cpu); break;
    case 0xb2: cpu_op_b2(cpu); break;
    case 0xb3: cpu_op_b3(cpu); break;
    case 0xb4: cpu_op_b4(cpu); break;
    case 0xb5: cpu_op_b5(cpu); break;
    case 0xb6: cpu_op_b6(cpu); break;
    case 0xb7: cpu_op_b7(cpu); break;
    case 0xb8: cpu_op_b8(cpu); break;
    case 0xb9: cpu_op_b9(cpu); break;
    case 0xba: cpu_op_ba(cpu); break;
    case 0xbb: cpu_op_bb(cpu); break;
    case 0xbc: cpu_op_bc(cpu); break;
    case 0xbd: cpu_op_bd(cpu); break;
    case 0xbe: cpu_op_be(cpu); break;
    case 0xbf: cpu_op_bf(cpu); break;
    case 0xc0: cpu_op_c0(cpu); break;
    case 0xc1: cpu_op_c1(cpu); break;
    case 0xc2: cpu_op_c2(cpu); break;
    case 0xc3: cpu_op_c3(cpu); break;
    case 0xc4: cpu_op_c4(cpu); break;
    case 0xc5: cpu_op_c5(cpu); break;
    case 0xc6: cpu_op_c6(cpu); break;
    case 0xc7: cpu_op_c7(cpu); break;
    case 0xc8: cpu_op_c8(cpu); break;
    case 0xc9: cpu_op_c9(cpu); break;
    case 0xca: cpu_op_ca(cpu); break;
    case 0xcb: cpu_op_cb(cpu); break;
    case 0xcc: cpu_op_cc(cpu); break;
    case 0xcd: cpu_op_cd(cpu); break;
    case 0xce: cpu_op_ce(cpu); break;
    case 0xcf: cpu_op_cf(cpu); break;
    case 0xd0: cpu_op_d0(cpu); break;
    case 0xd1: cpu_op_d1(cpu); break;
    case 0xd2: cpu_op_d2(cpu); break;
    case 0xd3: cpu_op_d3(cpu); break;
    case 0xd4: cpu_op_d4(cpu); break;
    case 0xd5: cpu_op_d5(cpu); break;
    case 0xd6: cpu_op_d6(cpu); break;
    case 0xd7: cpu_op_d7(cpu); break;
    case 0xd8: cpu_op_d8(cpu); break;
    case 0xd9: cpu_op_d9(cpu); break;
    case 0xda: cpu_op_da(cpu); break;
    case 0xdb: cpu_op_db(cpu); break;
    case 0xdc: cpu_op_dc(cpu); break;
    case 0xdd: cpu_op_dd(cpu); break;
    case 0xde: cpu_op_de(cpu); break;
    case 0xdf: cpu_op_df(cpu); break;
    case 0xe0: cpu_op_e0(cpu); break;
    case 0xe1: cpu_op_e1(cpu); break;
    case 0xe2: cpu_op_e2(cpu); break;
    case 0xe3: cpu_op_e3(cpu); break;
    case 0xe4: cpu_op_e4(cpu); break;
    case 0xe5: cpu_op_e5(cpu); break;
    case 0xe6: cpu_op_e6(cpu); break;
    case 0xe7: cpu_op_e7(cpu); break;
    case 0xe8: cpu_op_e8(cpu); break;
    case 0xe9: cpu_op_e9(cpu); break;
    case 0xea: cpu_op_ea(cpu); break;
    case 0xeb: cpu_op_eb(cpu); break;
    case 0xec: cpu_op_ec(cpu); break;
    case 0xed: cpu_op_ed(cpu); break;
    case 0xee: cpu_op_ee(cpu); break;
    case 0xef: cpu_op_ef(cpu); break;
    case 0xf0: cpu_op_f0(cpu); break;
    case 0xf1: cpu_op_f1(cpu); break;
    case 0xf2: cpu_op_f2(cpu); break;
    case 0xf3: cpu_op_f3(cpu); break;
    case 0xf4: cpu_op_f4(cpu); break;
    case 0xf5: cpu_op_f5(cpu); break;
    case 0xf6: cpu_op_f6(cpu); break;
    case 0xf7: cpu_op_f7(cpu); break;
    case 0xf8: cpu_op_f8(cpu); break;
    case 0xf9: cpu_op_f9(cpu); break;
    case 0xfa: cpu_op_fa(cpu); break;
    case 0xfb: cpu_op_fb(cpu); break;
    case 0xfc: cpu_op_fc(cpu); break;
    case 0xfd: cpu_op_fd(cpu); break;
    case 0xfe: cpu_op_fe(cpu); break;
    case 0xff: cpu_op_ff(cpu); break;
  }
}
#endif
