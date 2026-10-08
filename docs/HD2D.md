# HD-2D: Super Mario Kart with a 3D environment

## Status (version 4)
Version 4 adds the in-game Enhancements menu (all HD-2D options are controller toggles) and captures colour-math overlays (pixels where colour math applies over a non-sprite layer, alpha `$FE` in the OBJ tap). That's how SMK draws the big race-position number. Rows where colour math covers more than a third of the picture count as full-screen effects and aren't overlaid.

## Status (version 3)
Run `smkplay --hd2d` (OpenGL 3.3).
- F3: 3D on/off
- F4: tint the re-rendered geometry
- F6: HD-2D post effects on/off

| Piece | State |
|---|---|
| Camera tap: every DSP-1 `$02` Parameter call per frame (focus, eye distance, screen distance, azimuth, zenith) | **done** (`dsp1_tapCameras`) |
| Object tap: every DSP-1 `$06` Project call (world X/Y/Z of karts, items, map icons, with the game's screen result) | **done** (`dsp1_tapProjects`) |
| Coordinates: DSP-1 world units = 4 × Mode 7 texels. Verified: the map camera's centre / 4 equals the Mode 7 centre register exactly | **done** |
| View matching: each block of Mode 7 lines is paired with the camera that reproduces it, and the principal row is fitted per frame against the PPU's own per-line registers (rms < 8 texels accepted). Works for 1P (driving view + overview map), 2P split screen and battle | **done** |
| 3D floor: the Mode 7 plane rebuilt from VRAM as a 1024² texture (mipmapped, nearest when close), drawn as a real plane at the window's native resolution, widescreen by construction | **done** |
| 3D walls: the per-tile surface table (WRAM `$0B00`, indexed by the tilemap copy at `$7F0000`) marks solid tiles with bit 7. Those tiles are extruded into blocks (top = original tile, sides = the tile's colours, shaded by direction) | **done**: Mario Circuit borders, Ghost Valley rails, Bowser Castle walls, battle-course blocks |
| HD-2D post: tilt-shift blur towards the horizon of each driving view, bloom, vignette, light grade | **done** |
| Sky band in widescreen: the game streams only the visible 256 columns of its panorama, so side columns get each row's median sky colour, faded from the edge | **done** (stop-gap until a panorama cache) |
| Sprites | screen-space overlay from a per-pixel OBJ tap; they line up because the 3D camera is the game's camera. **Next:** cut them into billboards anchored at the Project positions, with depth against walls |
| Track dressing packs, lighting per track, sky panorama cache | planned |

Surface types seen on Mario Circuit: `$40` road, `$54` dirt, `$26` grass, `$80` wall/border (solid), `$14`/`$1A` item/coin panels.

# Original plan

Goal: keep the original game logic (recompiled, frame-exact), but replace the Mode 7 picture with a real 3D scene. The floor becomes a textured plane, track borders become extruded geometry, karts and items stay sprites drawn as billboards, and HD-2D post-processing goes on top (depth of field, bloom, soft shadows).

Everything below is render-only: it reads game state after each frame and draws its own image. Game state is never written. Netplay, savestates and the lockstep verification stay valid, and the classic renderer is always one key away.

## Why this is tractable for SMK
| Need | Where it comes from |
|---|---|
| **Camera** | The game hands its whole camera to the DSP-1 each frame: command `$02` Parameter gets `Fx, Fy, Fz` (focus point), `Lfe` (eye distance), `Les` (screen distance), `Aas` (azimuth), `Azs` (zenith). The HLE already parses these (`emu/snes/dsp1.c` `cmdParameter`). That's an exact 3D camera, no guessing. Split screen issues it twice per frame, once per player. |
| **Floor texture** | The Mode 7 tilemap and tiles in VRAM: 128×128 tiles of 8×8 = a 1024×1024 texture of the current track. It can be rebuilt whenever VRAM changes, then upscaled/filtered. |
| **Sprite placement** | Every kart/object goes through DSP-1 `$06` Project with its world `X, Y, Z`. Logging those calls per frame yields world positions to place billboards in 3D (the game's own screen projection becomes unnecessary). |
| **Sprite images** | OAM + VRAM: the frame/rotation the game picked for each kart, used as the billboard texture. |
| **Walls, pipes, blocks** | The track's per-tile surface attributes (road / off-road / wall / pit / jump) live in WRAM during a race. Wall tiles get extruded into 3D blocks with the same top texture. **To find:** the attribute table address and format (smktrace `watch` + RAM diffs, same method as the lap counter). |
| **Sky / hills** | The BG1/BG2 parallax layers above the horizon, drawn as a cylinder around the track. |
| **HUD** | Kept 2D: BG3 + HUD sprites composited on top, as in the original. |

## Phases
1. **Data taps (render-only hooks):**
   - Per frame, record the DSP-1 Parameter calls (camera per player), the Project calls (object world positions and the screen result they produce), and the Mode 7 VRAM state.
   - Expose them through a `RenderFrame` struct next to the PPU. Unit check: re-project the recorded positions with our own math and compare against the game's on-screen sprite positions.
2. **3D renderer prototype:** an OpenGL 3.3 path in `smkplay` (SDL already provides the context) drawing the floor plane with the Mode 7 texture from the recorded camera, plus kart billboards and the sky cylinder. Composite the original HUD on top. Toggle with a key. The first target is "looks like the original, but 3D and widescreen-native".
3. **Environment:**
   - Decode the tile-attribute table and extrude walls, pipes and blocks.
   - Hand-authored per-track dressing (trees, grandstands, castle walls) as optional asset packs keyed by track id, loaded from a mods folder, so it's not shipped game data.
4. **HD-2D look:** pixel-art billboards with nearest filtering, tilt-shift depth of field, bloom, ambient occlusion on extruded geometry, per-track light color, soft blob shadows.
5. **Polish:** 2P split screen (two cameras), the battle courses, Rainbow Road effects, the item effects (thunder flash, star palette cycle).

## Risks / unknowns
- The tile-attribute table layout (step 3) still has to be reverse-engineered.
- Some objects may be drawn without a DSP-1 Project call (HUD-like sprites, effects). Those stay 2D overlays until classified.
- The HLE `$06` Project is approximate. For phase 1 only the inputs matter (world positions), so this doesn't block anything.
