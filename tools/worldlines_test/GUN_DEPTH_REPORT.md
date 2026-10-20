# Map grid over the first-person gun: the game's own depth (2026-10-19)

User: "Find grid lines rendering in front of guns for the games please". The frame path (`WorldLinesGpu`, drawn after the
game's finished frame) is used for ODST, Reach, and Halo 3 with "Grid without bloom". Its only depth was its own pass of
the collision polygons, so lines showed over the first-person gun and arms. Halo 3's engine path (Jagged's way, in
`c_player_view::render`) was already hidden by the gun, but it gets bloom.

**Result:** the frame path now copies the game's own depth buffer each frame. A piece of line is not drawn where the game
has something within 2 world units in front of it. That covers the gun, arms, and anything right against your face.
Lines are still not bloomed. It was verified offline only (static RE + WARP test); none of it has been seen in game.

## 1. Where the depth is (static, game dlls 1.3528)

All three MCC ports drive D3D11 through an Xbox 360-style rasterizer. Render targets and depth buffers live in a table of
"surfaces". `set_depth_stencil_surface` picks the buffer:

| | Halo 3 `0x27632C` | ODST `0x2A35E0` | Reach `0x2743A4` |
|---|---|---|---|
| table, stride | `0xA65A80`, 0x58 | `0xAC80A0`, 0x60 | `0xC8E4B0`, 0x70 |
| flags | +0 | +0 | +0x10 |
| own buffer index | - | flag 0x400, int +0x58 | flag 0x100, int +0x68 |
| paired buffers (x2) | - | - | flag 8 |
| per split-screen view (view + 4 x index) | flag 0x20, view `[0x46BB868]` | flag 0x20, view `[0x46E5B90]` | flag 0x20, view `[0x4E38A08]` |
| buffers (pointer), stride | +0x50, 0xA8 | +0x50, 0xA8 | +0x60, 0x88 |
| buffer +0 | ID3D11DepthStencilView | same | same |
| current DSV / "float depth" flag | `[0x46BB8E8]` / `[0x46BB86C]` (format 0x14) | `[0x46E5BD8]` / `[0x46E5B94]` | `[0x4E38A28]` / `[0xB43ADC]` (0x14 or 0x28) |

**The main depth surface** is the one the first-person view is drawn into:
- **Halo 3: 3.**
  - `render_first_person` 0x2835D4 (called from the view render 0x285378 at 0x2859AB / 0x285A93) starts with `set_depth_stencil_surface(3)` and `set_z_buffer_mode(0)`.
  - The view render itself ends on `jmp 0x27632C` with ecx = 3.
  - While its transparents draw, 3 and 6 become 7 (`[0x46BB8DC]`, set at 0x2A94EE and reset at 0x2A9220). That is also handled.
- **ODST: 3.** The first-person render 0x2AC2E4 starts the same way: `set_depth(3)`, then z mode 0.
- **Reach: 9.**
  - The first-person pass 0x26EA78 sets 9, then builds the first-person camera twice (0x286C6C reads `render_first_person_fov_scale`).
  - The depth-constants function 0x261920 binds surface 9 as a texture when it needs depth.

**Depth encoding.** A float depth is kept reversed: 1 at the near plane, 0 far away.
- Halo 3's `c_rasterizer::clear` 0x274254 turns the clear value into 1 - z when `[0x46BB86C]` is set.
- `render_camera_build_projection` 0x2A6980 swaps near and far for that case. ODST 0x2CB1C4 is identical.
- Reach's flag `[0xB43ADC]` defaults to 1.
- The stored depth is therefore n(f - d) / (d (f - n)). The shader turns it back into a distance: d = n f / (n + z (f - n)).

**Clip planes** are at render_camera +0x64 (near) and +0x68 (far):
- Halo 3 passes the global camera record 0x2D2F688 itself to the projection builder (0x24CC28 .. 0x24CC43).
- ODST does the same with 0x2D73598 (0x1B653E / 0x1B6542).
- Reach 0x261920 reads +0x64 / +0x68 of its render camera. Its defaults are the hs globals `render_near_clip_distance` 1/128 and `render_far_clip_distance` 10240 (0x1800D34CD).

**The first-person projection uses another near plane.** Halo 3 0x279BEC multiplies near by 2.4 (screens wider than 1.8:1) or 3.2. Read with the world's near plane, the gun looks closer than it is, which is still "in front".

## 2. What changed

| file | change |
|---|---|
| `HCMInternal/WorldLinesShaders.hlsl` (+ `.h` regenerated) | cbuffer `scene` / `sceneMap`; `t0` the copy; `PSLine` discards a fragment where the game's distance is < `sceneRange` (2) and < 92 % of the line's distance - 3 cm. The same surface is never "in front". |
| `HCMInternal/WorldLinesGpu.h/.cpp` | `DrawOptions.sceneDepth/sceneNear/sceneFar/sceneRange`; `Gpu::sceneFor` (checks the device, copies into a texture of the same format group: R32G8X24 / R32 / R24G8, refuses MSAA, arrays and unknown formats); SRV bound for the line pass; the state backup now saves and restores PS SRV slot 0; `sceneUsed()` / `sceneProblem()` |
| `HCMInternal/PoorboyWorldLines.h/.cpp` | `kDepthGames` (the table above), `depthViewOf` (VirtualQuery-checked reads), `depthTexture` (vtable mapped, then QI DSV + device check + GetResource under SEH), `gameDepth()`. The texture is looked up every 500 ms and held with one reference; the clip planes are read every frame (near must be 1e-5 .. 1). One log line when the state changes: "Map grid on <game>: kept off your first-person gun and arms by the game's own depth (W x H, format F; clip planes N .. F)", or "drawn over your first-person gun: the game's own depth <why>". |
| `HCMInternal/GUIElementConstructor.cpp`, `README.md`, `tools/package.py` | The "Grid without bloom" tooltip and the docs: the gun and arms now hide the lines in ODST, Reach and no-bloom mode. |
| `tools/worldlines_test/main.cpp` | A new section: a stand-in game depth (the map plus a gun quad 0.3 units ahead with the 2.4x near plane) in three layouts. Env `WORLDLINES_DUMP=<dir>` writes every frame as a picture. |

## 3. Offline test (WARP)

| map | layout | gun quarter line pixels | elsewhere kept | state put back |
|---|---|---|---|---|
| cyberdyne | D32_FLOAT_S8X24 at the frame's size (bound as the DSV while drawing) | 406,387 -> **0** | 97.9 % | yes |
| | D32_FLOAT at 1280 x 720 | -> 0 | 97.8 % | yes |
| | D24_UNORM_S8, the usual way round | -> 0 | 97.9 % | yes |
| sc130 | all three | 783,054 -> **0** | 98.3 % | yes |
| 52_ivory_tower (Reach) | all three | 746,335 -> **0** | 100.0 % | yes |

A colour texture given as the depth is refused, and the lines are drawn as before.

The ~2 % "lost" elsewhere is one view on cyberdyne (`gunmask_014`). A collision face passes within 0.06 units of the eye there: the grid's own depth pass clips it at its 0.06 near plane, while the game keeps it (near 1/128). The game draws that face over the area, so hiding those lines is right.

The whole suite passes on cyberdyne, sc130 and 52_ivory_tower. On guardian it stops at the existing "wide lines as ribbons" check before reaching the new section. The committed 1.3.0 test fails the same way, so this is not from this change.

HCMInternal compiles (Release x64, PCMInternal.dll in the repo's build folder). Nothing was packaged; no dist folder or zip was made.

## 4. Needs an in-game check

1. In ODST, Reach, and Halo 3 with "Grid without bloom", turn the Map grid on and look at a wall behind your gun: no lines should show over the gun or arms. h3sb's PCM log (`Logs\`) should show the "kept off your first-person gun ... (W x H, format 19 ...; clip planes 0.0078125 .. N)" line.
   - Format 19 = R32G8X24_TYPELESS, 20 = D32_FLOAT_S8X24_UINT.
   - The clip planes should look like 1/128 and a large far plane.
2. If the log says "was not found in its surface table", a table offset or the per-view index is wrong for that game. If it says "has no readable near plane", the camera +0x64 offset is wrong. Either way the lines are drawn as before (no harm).
3. If lines still show over the gun with the "kept off" line logged:
   - Either the game's depth at Present no longer holds the gun (cleared or reused after the view render),
   - or the first-person pass doesn't write depth in that game.
   - Then the depth would have to be copied right after the view render instead (Halo 3: in `onViewRendered`; ODST and Reach would need their own view-render hook).
4. Lines on the floor right at your feet and on a wall you are hugging should stay. They are the same surface, which needs to be 8 % + 3 cm nearer to hide a line. Check that they don't flicker.
5. Performance: one depth copy per frame (8 bytes a pixel at the frame's size), only while the grid is on and drawn over the frame.

Scripts used: `scratchpad\gun\strs.py`, `hsglobal.py`, `vcalls.py`, `argcalls.py`, `nearfar.py`, `h3toreach.py`, `lost.py`.
