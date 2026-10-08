"""Quick linear 65816 disassembly: peek.py rom.sfc ADDR COUNT [m8 x8]  (HiROM addresses)"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from op65816 import OPS, operand_len, fmt_operand
rom = open(sys.argv[1], "rb").read()
pc = int(sys.argv[2], 16); n = int(sys.argv[3])
m8 = int(sys.argv[4]) if len(sys.argv) > 4 else 1
x8 = int(sys.argv[5]) if len(sys.argv) > 5 else 1
def rd(a): return rom[((a >> 16 & 0x3f) << 16 | (a & 0xffff)) % len(rom)]
for _ in range(n):
    op = rd(pc); mn, mode = OPS[op]; l = operand_len(op, m8, x8)
    v = 0
    for i in range(l): v |= rd(pc + 1 + i) << (8 * i)
    print("%06X  %-12s %s %s" % (pc, " ".join("%02X" % rd(pc + i) for i in range(l + 1)), mn, fmt_operand(mode, v, l, pc + 1 + l)))
    if mn == "REP": m8 = m8 and not (v & 0x20); x8 = x8 and not (v & 0x10)
    if mn == "SEP": m8 = m8 or bool(v & 0x20); x8 = x8 or bool(v & 0x10)
    pc += 1 + l
