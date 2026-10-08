# Modding workflow

## 1. Quick experiments: edit the disassembly
`asm/` is regenerated from coverage, so treat it as a read-mostly view. For a quick test, edit it directly and rebuild:

```
python tools/build.py asm build/smk_mod.sfc --asar bin/win/asar.exe
bin\win\smkplay.exe build\smk_mod.sfc
```

The build prints the CRC32. `matches original` means your edit had no effect.

## 2. Durable mods: asar patches
Keep mods in `mods/<name>.asm` (see `mods/short_races.asm` for a working example) and apply them on top of the rebuilt ROM. They survive regenerations of `asm/`:

```asm
; mods/example_lap3.asm: example patch skeleton
hirom
org $80XXXX          ; address from asm/ (comment column)
    JSL MyHook       ; replace an instruction (match the original byte length or NOP-pad)
freecode             ; asar finds free space (ROM is 512 KiB; expand to 1 MiB if needed)
MyHook:
    ; ...
    RTL
```

```
python tools\apply_mods.py              # rebuilds asm/ and applies every mods/*.asm -> build\smk_mod.sfc
python tools\apply_mods.py short_races  # just one
bin\win\smkplay.exe build\smk_mod.sfc
```

## 3. Mods in C: overrides
The game runs as recompiled C (`recomp/gen/`). Any instruction can be replaced by your own C function:

```c
// recomp/overrides/my_mod.c
#include "smk.h"
SMK_OVERRIDE(0x81F108, my_lap_init)          // address from asm/ or recomp/gen/
void my_lap_init(Cpu* c) {
  c->a = (c->a & 0x00FF) | 0x8200;           // registers: c->a, c->x, c->y, flags c->n/z/c/v...
  setzn16(c, c->a);
  c->pc = 0xF10B;                            // where execution continues
}
```

Helpers in `recomp/smk.h`: `ram8/ram16` (+`w` to write) for WRAM, `bus8/bus8w` for hardware registers, and flag setters. Then:

```
python tools\recomp.py smk.sfc trace\out recomp\gen --symbols symbols.txt
make -C emu            (or: make -C emu win)
```

`recomp/overrides/_short_races.c` is the C twin of `mods/short_races.asm`. Drop the leading underscore to enable it. With an override active, `verify_recomp.py` reports a mismatch where your change kicks in; that's expected.

## 4. Naming things
When you work out what a routine or table does, add it to `symbols.txt` (`<address> <Name>`) and regenerate. Names carry through to every reference.

## 5. Finding code
- `smktrace` is the investigation tool. Its script language is documented at the top of `emu/smktrace.c`: `watch <addr>` prints the PC of every write to a RAM address, `ram`/`poke`/`dumpram` read and write memory, `prof N` lists the hottest code, `until`/`untiltap` wait on a RAM value, and `shot` writes a screenshot.
- Worked example: `watch 10c1` during a race start found the lap-counter init at `$81F108`, which led to `mods/short_races.asm`.
- Diff `dumpram` snapshots to find variables (that's how the lap counter was found; see `docs/NOTES.md`).
- `python tools/peek.py smk.sfc <addr> <count> [m8 x8]` for a quick look at any address.

## 6. Coverage gaps
Bytes that never executed are emitted as `db`. If you find real code inside a `db` block, add a campaign to `trace/run_coverage.py` that reaches it, or add its entry point to `symbols.txt` and extend `tools/disasm.py` to seed it.
