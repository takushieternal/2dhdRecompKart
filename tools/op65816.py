"""65816 opcode table shared by the disassembler and recompiler tools.

Modes:
  imp  implied            acc  accumulator (A)
  imm  #imm (size by M)   imx  #imm (size by X)   im8  #imm 8-bit (REP/SEP/BRK/COP/WDM)
  dp   dp      dpx dp,X    dpy dp,Y   idp (dp)  idx (dp,X)  idy (dp),Y
  idl  [dp]    ily [dp],Y
  abs  abs     abx abs,X   aby abs,Y
  abl  long    alx long,X
  ind  (abs)   iax (abs,X) ial [abs]
  sr   sr,S    isy (sr,S),Y
  rel  8-bit branch        rll 16-bit branch (BRL/PER)
  bm   block move (MVN/MVP)
"""

_T = """
00 BRK im8|01 ORA idx|02 COP im8|03 ORA sr|04 TSB dp|05 ORA dp|06 ASL dp|07 ORA idl|08 PHP imp|09 ORA imm|0a ASL acc|0b PHD imp|0c TSB abs|0d ORA abs|0e ASL abs|0f ORA abl
10 BPL rel|11 ORA idy|12 ORA idp|13 ORA isy|14 TRB dp|15 ORA dpx|16 ASL dpx|17 ORA ily|18 CLC imp|19 ORA aby|1a INC acc|1b TCS imp|1c TRB abs|1d ORA abx|1e ASL abx|1f ORA alx
20 JSR abs|21 AND idx|22 JSL abl|23 AND sr|24 BIT dp|25 AND dp|26 ROL dp|27 AND idl|28 PLP imp|29 AND imm|2a ROL acc|2b PLD imp|2c BIT abs|2d AND abs|2e ROL abs|2f AND abl
30 BMI rel|31 AND idy|32 AND idp|33 AND isy|34 BIT dpx|35 AND dpx|36 ROL dpx|37 AND ily|38 SEC imp|39 AND aby|3a DEC acc|3b TSC imp|3c BIT abx|3d AND abx|3e ROL abx|3f AND alx
40 RTI imp|41 EOR idx|42 WDM im8|43 EOR sr|44 MVP bm|45 EOR dp|46 LSR dp|47 EOR idl|48 PHA imp|49 EOR imm|4a LSR acc|4b PHK imp|4c JMP abs|4d EOR abs|4e LSR abs|4f EOR abl
50 BVC rel|51 EOR idy|52 EOR idp|53 EOR isy|54 MVN bm|55 EOR dpx|56 LSR dpx|57 EOR ily|58 CLI imp|59 EOR aby|5a PHY imp|5b TCD imp|5c JML abl|5d EOR abx|5e LSR abx|5f EOR alx
60 RTS imp|61 ADC idx|62 PER rll|63 ADC sr|64 STZ dp|65 ADC dp|66 ROR dp|67 ADC idl|68 PLA imp|69 ADC imm|6a ROR acc|6b RTL imp|6c JMP ind|6d ADC abs|6e ROR abs|6f ADC abl
70 BVS rel|71 ADC idy|72 ADC idp|73 ADC isy|74 STZ dpx|75 ADC dpx|76 ROR dpx|77 ADC ily|78 SEI imp|79 ADC aby|7a PLY imp|7b TDC imp|7c JMP iax|7d ADC abx|7e ROR abx|7f ADC alx
80 BRA rel|81 STA idx|82 BRL rll|83 STA sr|84 STY dp|85 STA dp|86 STX dp|87 STA idl|88 DEY imp|89 BIT imm|8a TXA imp|8b PHB imp|8c STY abs|8d STA abs|8e STX abs|8f STA abl
90 BCC rel|91 STA idy|92 STA idp|93 STA isy|94 STY dpx|95 STA dpx|96 STX dpy|97 STA ily|98 TYA imp|99 STA aby|9a TXS imp|9b TXY imp|9c STZ abs|9d STA abx|9e STZ abx|9f STA alx
a0 LDY imx|a1 LDA idx|a2 LDX imx|a3 LDA sr|a4 LDY dp|a5 LDA dp|a6 LDX dp|a7 LDA idl|a8 TAY imp|a9 LDA imm|aa TAX imp|ab PLB imp|ac LDY abs|ad LDA abs|ae LDX abs|af LDA abl
b0 BCS rel|b1 LDA idy|b2 LDA idp|b3 LDA isy|b4 LDY dpx|b5 LDA dpx|b6 LDX dpy|b7 LDA ily|b8 CLV imp|b9 LDA aby|ba TSX imp|bb TYX imp|bc LDY abx|bd LDA abx|be LDX aby|bf LDA alx
c0 CPY imx|c1 CMP idx|c2 REP im8|c3 CMP sr|c4 CPY dp|c5 CMP dp|c6 DEC dp|c7 CMP idl|c8 INY imp|c9 CMP imm|ca DEX imp|cb WAI imp|cc CPY abs|cd CMP abs|ce DEC abs|cf CMP abl
d0 BNE rel|d1 CMP idy|d2 CMP idp|d3 CMP isy|d4 PEI dp|d5 CMP dpx|d6 DEC dpx|d7 CMP ily|d8 CLD imp|d9 CMP aby|da PHX imp|db STP imp|dc JML ial|dd CMP abx|de DEC abx|df CMP alx
e0 CPX imx|e1 SBC idx|e2 SEP im8|e3 SBC sr|e4 CPX dp|e5 SBC dp|e6 INC dp|e7 SBC idl|e8 INX imp|e9 SBC imm|ea NOP imp|eb XBA imp|ec CPX abs|ed SBC abs|ee INC abs|ef SBC abl
f0 BEQ rel|f1 SBC idy|f2 SBC idp|f3 SBC isy|f4 PEA abs|f5 SBC dpx|f6 INC dpx|f7 SBC ily|f8 SED imp|f9 SBC aby|fa PLX imp|fb XCE imp|fc JSR iax|fd SBC abx|fe INC abx|ff SBC alx
"""

OPS = {}
for chunk in _T.replace("\n", "|").split("|"):
    chunk = chunk.strip()
    if not chunk:
        continue
    code, mn, mode = chunk.split()
    OPS[int(code, 16)] = (mn, mode)
assert len(OPS) == 256

_FIXED = {"imp": 0, "acc": 0, "im8": 1, "dp": 1, "dpx": 1, "dpy": 1, "idp": 1, "idx": 1, "idy": 1,
          "idl": 1, "ily": 1, "sr": 1, "isy": 1, "rel": 1, "abs": 2, "abx": 2, "aby": 2, "ind": 2,
          "iax": 2, "ial": 2, "rll": 2, "bm": 2, "abl": 3, "alx": 3}


def operand_len(op, m8, x8):
    mode = OPS[op][1]
    if mode == "imm":
        return 1 if m8 else 2
    if mode == "imx":
        return 1 if x8 else 2
    return _FIXED[mode]


def fmt_operand(mode, v, nbytes, pc_next=None, sym=None):
    """Format an operand. v = raw operand value. sym(addr,kind) may return a label."""
    def S(a, kind, width):
        if sym:
            r = sym(a, kind)
            if r:
                return r
        return "$%0*X" % (width, a)
    if mode in ("imp",):
        return ""
    if mode == "acc":
        return "A"
    if mode in ("imm", "imx", "im8"):
        return "#$%0*X" % (nbytes * 2, v)
    if mode == "dp": return S(v, "dp", 2)
    if mode == "dpx": return S(v, "dp", 2) + ",X"
    if mode == "dpy": return S(v, "dp", 2) + ",Y"
    if mode == "idp": return "(" + S(v, "dp", 2) + ")"
    if mode == "idx": return "(" + S(v, "dp", 2) + ",X)"
    if mode == "idy": return "(" + S(v, "dp", 2) + "),Y"
    if mode == "idl": return "[" + S(v, "dp", 2) + "]"
    if mode == "ily": return "[" + S(v, "dp", 2) + "],Y"
    if mode == "sr": return "$%02X,S" % v
    if mode == "isy": return "($%02X,S),Y" % v
    if mode == "abs": return S(v, "abs", 4)
    if mode == "abx": return S(v, "abs", 4) + ",X"
    if mode == "aby": return S(v, "abs", 4) + ",Y"
    if mode == "ind": return "(" + S(v, "ptr", 4) + ")"
    if mode == "iax": return "(" + S(v, "jtab", 4) + ",X)"
    if mode == "ial": return "[" + S(v, "ptr", 4) + "]"
    if mode == "abl": return S(v, "long", 6)
    if mode == "alx": return S(v, "long", 6) + ",X"
    if mode == "rel":
        t = (pc_next & 0xFF0000) | ((pc_next + (v - 256 if v & 0x80 else v)) & 0xFFFF)
        return S(t, "code", 6)
    if mode == "rll":
        t = (pc_next & 0xFF0000) | ((pc_next + (v - 65536 if v & 0x8000 else v)) & 0xFFFF)
        return S(t, "code", 6)
    if mode == "bm":
        return "$%02X,$%02X" % (v & 0xFF, v >> 8)  # dest,src in asm syntax
    raise ValueError(mode)
