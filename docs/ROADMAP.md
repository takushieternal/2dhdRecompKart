# Roadmap

## Phase 0: runtime (done)
- LakeSnes core with HiROM DSP-1 mapping and a clean-room HLE DSP-1 (`emu/snes/dsp1.c`).
- `smktrace`: deterministic headless runner (scripted input for both pads, savestates, SRAM, RAM pokes and watchpoints, conditional waits, screenshots, coverage, per-frame state hashes, per-instruction logs).
- `smkplay`: SDL2 player for Windows and Linux.

## Phase 1: disassembly (done, grows with coverage)
- Coverage-guided plus recursive-descent disassembly with jump-table recovery, rebuilt byte-for-byte (CRC32 `CD80DB86`) on every run.
- Coverage campaigns play all 20 tracks in time trial, every 1P cup at 50/100/150cc including Special (which unlocks everything), every 2P GP cup, battle mode, match race, the attract demo and the ending credits.
- **Next:** name RAM (`ram.asm` / kart object struct), split banks into topic files, data tools (tracks, graphics, text).

## Phase 2: static recompilation
| Step | State |
|---|---|
| 1. `tools/recomp.py` turns the analysed code into C (one function per bank; branches/calls become `goto`; returns and indirect jumps go through a per-bank dispatcher) | **done**: all ~29.7k known instructions |
| 2. Hybrid execution: recompiled code runs whenever the CPU is at a known instruction; anything else falls back to the interpreter | **done**: on every campaign 100% of executed instructions run recompiled |
| 3. Lockstep verification: `tools/verify_recomp.py` runs every campaign on both cores and compares a per-frame hash of WRAM, VRAM, CGRAM, OAM, CPU registers and the master-cycle count | **done**: identical on all campaigns (383k frames); 0 instructions fall back to the interpreter |
| 4. C overrides: replace any instruction or routine with hand-written C (`recomp/overrides/`, `recomp/smk.h`) | **done**: `_short_races.c` example |
| 5. Lift routines into readable C | **next**: see below |

The recompiled instructions call the interpreter's own per-opcode functions (`emu/snes/cpu_core.inc`), so every bus access and every interrupt check lands on the same cycle. That's why lockstep verification can demand an exact match instead of "close enough".

**Lifting plan (step 5).** Work routine by routine:
1. Pick a routine (start with leaf routines and the kart physics/AI the mods need).
2. Write its C version as an override in `recomp/overrides/`, using `smk.h` helpers and named RAM.
3. Run `verify_recomp.py`. Exact-timing matches are only possible for code that keeps the same bus-access order, so lifted code is checked with a RAM-only comparison mode (to add: hash WRAM only, at routine exit).
4. Once all game logic is lifted, drop CPU timing from the shipping build and run frame-by-frame like the snesrev ports. The PPU/APU stay emulated.

## Phase 3: mods
| Item | State |
|---|---|
| Netplay: delay-based lockstep over UDP, host = P1; the host's SRAM is sent so both sides start identical; WRAM hash every 60 frames for desync detection | **done**: `smkplay --host` / `--join`; verified in sync over 5k frames with different inputs on each side |
| HD Mode 7: 2×2 samples per floor pixel (render-only, so netplay-safe) | **done**: `--hd` or H key |
| Rollback on top of the lockstep protocol (needs faster resimulation: skip rendering/audio while re-running frames) | next |
| Widescreen 16:9 (400×224): Mode 7 floor and BG layers rendered into side columns. Sprites that are partly on screen continue into the sides; hidden/parked sprites stay hidden. BG3 (HUD) is kept out of the sides. Frames with no Mode 7 (menus, title) are pillarboxed. Render-only | **done** (version2): `--wide` / F2 |
| Widescreen part 2: the game culls karts and objects at the original screen edges, so they pop in at the 4:3 border. Fix by widening the culling test with a C override once it's located | next |
| HD Mode 7 with true per-line interpolation (the lower half-line currently reuses the same line's matrix) | planned |
| New modes on top of the GP and battle state machines | planned |

## Phase 4: HD-2D (3D environment)
See `docs/HD2D.md`. The DSP-1 already receives the game's exact camera every frame, so a real 3D renderer can sit on top of the unchanged game logic.
**Version 3:** phase 1 is in. The camera and object taps work, Mode 7 views are re-rendered as 3D planes, walls are extruded from the surface table, and the post effects are on. The renderer is render-only; the bootstrap campaign's frame hashes are unchanged with the taps enabled.

## DSP-1 accuracy
The HLE is exact for multiply, triangle, rotate, radius and distance, and close (float-assisted) for project/target/parameter. The plan for bit-exactness is an LLE uPD7725 core running the real `dsp1b.rom` firmware, supplied by the user.
