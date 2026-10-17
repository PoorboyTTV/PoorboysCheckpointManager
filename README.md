# Poorboys Checkpoint Manager

**Halo 2's physics in Halo 3, Halo 3: ODST and Halo: Reach** - superbounces, barrier breaks, Halo 2 movement, button combos and lunges - built into Halo Checkpoint Manager, the practice tool for Halo: The Master Chief Collection.

## ⬇️ [Download the latest version](https://github.com/PoorboyTTV/PoorboysCheckpointManager/releases/latest/download/PoorboysCheckpointManager.zip)

One zip, nothing to install. Windows, MCC on Steam (made for MCC 1.3528). Older versions and release notes: [Releases](https://github.com/PoorboyTTV/PoorboysCheckpointManager/releases).

💬 **Need help, found a bug, or want something added?** Message Poorboy on Discord: **@poorboy**

---

## How to run it (4 steps)

1. **Unzip** the download anywhere and keep the folder together.
2. **Start MCC with anti-cheat off**: in Steam press Play, then pick **"Halo: MCC Anti-Cheat Disabled"**.
3. **Run `PoorboysCheckpointManager.exe`** from the folder and say yes to the administrator prompt.
4. **In a Halo 3, ODST or Reach game**, press **`** (the key left of 1) to open the overlay, tick **Halo 2 physics** (or press **F6**), then press **`** again to close the overlay. The game is paused while the overlay is open.

That's it: you now move, jump and bounce like in Halo 2. `HOW TO USE.txt` in the zip has the same steps.

---

## What it can do

Everything below is in **Poorboy's Mods**, the first section of the overlay.

### Halo 2 physics
- **Superbounces** work as in Halo 2: Halo 2's own player body, collision step, movement and jump, ported to run inside Halo 3, ODST and Reach.
- **Barrier breaks**, as in MCC Halo 2: Halo 2's time-of-impact skip (TIM) and its back-stepping over-credit are reproduced. Bump a wall again and again next to a sharp corner, then walk into the other wall and you go through it (needs 30 tick; you come out armed).
- **30 tick** (F9): the game and the Halo 2 physics run at 30 ticks a second, like Xbox Halo 2, which is what superbounces need. 60 = PC Halo 2 (no superbounces from ordinary heights). Switch any time.
- **Arm now**: puts you in Halo 2's armed state without finding a crouch-stand spot. The status line shows whether you're armed and your speed.
- **Explosions push the Halo 2 way**, with Halo 2's own numbers: grenade and rocket jumps feel like Halo 2.
- **Solid objects**: ride lifts and man cannons, stand on and push crates, all as Halo 2 does it.

### Halo 2 combat
- **Button combos** (F10): BXR, BXB, double shot, the melee lock, YX (X during a weapon switch swaps at once) and the plasma pistol double grenade.
- **Halo 2 lunges** (with Halo 2 physics on): melee and sword lunges fly straight at the target with no gravity and keep going when you cancel, so **butterflying** works, and **sword flying** (rifle locked on someone, Y, X, RT: the sword lunges at what the rifle had locked).

### Practice tools
- **Save spot / Back to spot**: back exactly where you saved: place, speed, view, crouch and armed state. Safe online (only your player moves).
- **Fly**: your player flies where you look, through walls (others see you). Speed slider next to it.
- **Barriers** (F7): soft ceilings off, out-of-bounds kill off, kill volumes off (in Reach also its out-of-the-map timers).
- **No falling deaths** (F11): falls and hard landings never hurt (in Reach also its landing and impact damage).
- **Bottomless Clip**: your magazine never empties, plasma batteries don't drain, grenades never run out.
- **Disable AI**: enemies and allies stand still and don't shoot, for campaign practice and videos.
- **Helper windows**: a map window with call-outs, and lines drawn in the world (floor seams, where bounces come from).

### Halo: Reach
Everything above, plus Reach's armor abilities working with Halo 2 physics:
- **Jetpack** lifts you (its thrust goes into the Halo 2 body every tick).
- **Sprint** speeds up Halo 2's running by Reach's own amount (speed traits too).
- **Evade** rolls at Reach's speed and keeps its momentum when you jump out of it or roll off a ledge. You stay armed, so you can superbounce out of an evade.
- **Armor lock** holds you still, and nothing pushes you while it lasts.

### Forge and Theatre
- **Forge bounces** (Halo 3, Reach): Forge objects count as part of the map, so you can bounce off them like the map. Turn it off for Halo 2's own rule for objects. In Reach, still crates and Forge pieces are always solid.
- **Theatre**: films replay what the Halo 2 physics did in that game, so you can film clips from Theatre.

### Poorboy's logo
A see-through logo sits in the top-left corner of the game, next to the grenade counts, so clips show they were played with Poorboy's mods.

---

## Hotkeys

| Key | What it does |
|---|---|
| ` | Open / close the overlay |
| F6 | Halo 2 physics on / off |
| F7 | Barriers: the game's own → soft ceilings off → + out-of-bounds kill off → + kill volumes off |
| F9 | 30 / 60 tick |
| F10 | Halo 2 button combos |
| F11 | No falling deaths |
| (set your own) | Arm now, Fly, Save spot, Back to spot, Bottomless Clip, Disable AI, Forge bounces |

Every key can be rebound with the **..** button beside it in the overlay: a key, a controller button, or a combination.

---

## Playing online
Multiplayer custom games with friends who also have anti-cheat off.
- Everyone who wants Halo 2 physics runs the manager themselves, **the same version**. It moves only that player's own character, host or not.
- Everyone uses **the same 30 tick setting as the host** (on for superbounces).
- Use **Save spot / Back to spot** to retry jumps. Force Revert rolls the whole game back on your machine only and can crash an online game.
- Stick to multiplayer custom games: co-op campaign and Firefight run in lockstep, and a physics mod on one machine can knock them out of sync.

---

## Troubleshooting
- **"The helper is missing"**: your antivirus removed `h3sb\helper\h3sb_helper.exe` (a false alarm: it's a Python program packed into an exe). Restore it in Windows Security > Virus & threat protection > Protection history, or unzip again.
- **Windows warns about an unknown publisher**: the exe isn't signed. Click "More info" > "Run anyway".
- **Nothing happens in game**: MCC must be started with anti-cheat off, and the manager run as administrator.
- **"Not armed: the Halo 2 body is not driving the player"**: you're flying, in a vehicle, dead, or the game is still loading.
- **Something went wrong**: message Poorboy on Discord (**@poorboy**) and send the `Logs` folder and the `h3sb\h3sb.log` and `h3sb\h3sb_prev.log` files (not to Burnt-o).

Known limits: in Reach, vehicles and objects that move are not solid for the Halo 2 body yet. Online play has been tested with two players.

---

## Contact
Questions, problems, or ideas for something to add: Poorboy on Discord, **@poorboy**.

---

## Credits
- **Poorboy**: Poorboys Checkpoint Manager and its Halo 2 physics for Halo 3, ODST and Reach: superbounces, movement and jump, barrier breaks, button combos and lunges, solid objects, Reach's armor abilities, Theatre replays, and all the switches above.
- **Burnt-o (Maurice Dix) and Harc**: [Halo Checkpoint Manager](https://github.com/Burnt-o/HaloCheckpointManager), which all of this is built on: dumping and injecting checkpoints, the checkpoint library, the in-game overlay, the hotkey system and every practice cheat (invulnerability, speedhack, teleport, free camera and the rest). Everything that is not the Halo 2 physics is their work.
- **Harc**: the research on Halo 2's Havok back-stepping and TIM over-credit that made barrier breaks possible.
- Third-party libraries: see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

---

## Halo Checkpoint Manager's own features (all still here)
- Dump the games' checkpoints to files and inject them again any time, with a library to organise them (ordering, sorting, renaming).
- Practice cheats: force checkpoints, block the games' own checkpoints, invincibility, speedhack (slow-down and fast-forward), teleport and launch, free camera, and more (each game has its own set).
- Support for downpatched MCC versions: Season 7 (2448), Season 8 (2645) and the current patch.

It runs side by side with the original Halo Checkpoint Manager's files without clashing (its own dll names, shared memory and process names), but use one of the two at a time.

---

## Building from source
Visual Studio 2022 with .NET desktop development, Desktop development with C++ and a Windows 10 SDK. The C++ dependencies come from vcpkg manifests, pinned to a 2025-07 vcpkg (`builtin-baseline` in each `vcpkg.json`; newer curl ports dropped the `non-http` and `schannel` features these projects ask for). With a vcpkg clone that is not integrated into Visual Studio:

    msbuild HaloCheckpointManager.sln /restore /m /p:Configuration=Release /p:Platform=x64 /p:VcpkgRoot=<vcpkg>\ /p:ForceImportBeforeCppTargets=<vcpkg>\scripts\buildsystems\msbuild\vcpkg.targets

Everything lands in `HCMExternal\bin\x64\Release\net7.0-windows\`; run `PoorboysCheckpointManager.exe`. The Halo 2 physics mod (`h3sb.dll`) is not built from this repo: take the `h3sb` folder from the release zip and put it next to the exe. The original notes: SafetyHook by cursey is included as a submodule; after a first full build, rebuild the whole solution so all build events fire.
