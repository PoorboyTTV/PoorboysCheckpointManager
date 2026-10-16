# Poorboys Checkpoint Manager
*A practice tool for [Halo: The Master Chief Collection](https://store.steampowered.com/app/976730/Halo_The_Master_Chief_Collection/): Halo Checkpoint Manager with Halo 2's physics built in for Halo 3 and Halo 3: ODST.*

## Download
Get the latest zip from the **Releases** page (on the right). Unzip it, keep the folder together, and run `PoorboysCheckpointManager.exe`. Nothing to install: .NET and the C++ runtime are inside. `HOW TO USE.txt` in the zip has the details.

1. Start MCC with anti-cheat off (Steam: Play, then "Halo: MCC Anti-Cheat Disabled").
2. Run `PoorboysCheckpointManager.exe` and accept the administrator prompt.
3. In a Halo 3 or ODST game, open the overlay with ` (the key left of 1), tick **Halo 2 physics** (F6), then close the overlay so the game runs.

Made for MCC 1.3528 on Steam.

**Antivirus:** some antivirus programs wrongly flag `h3sb\helper\h3sb_helper.exe` (the map window and the lines in the world; it is a Python program packed into an exe, and those get false alarms) and quarantine it. The manager then says the helper is missing. Restore it in Windows Security > Virus & threat protection > Protection history (or your antivirus's quarantine) and allow it, or unzip again.

To watch the others, **Fly** (Halo 2 Physics section) flies your player where you look, through walls: the others see you. **Free Camera** (Camera section) flies only your view, unseen.

To try a jump again, **Save spot** and **Back to spot** (Save Management, above Force Checkpoint) put you back where you saved: the place, your speed, crouch, which way you look and Halo 2's armed state. Only your own player moves, nothing is rolled back.

## Playing online
Custom games with friends who also have anti-cheat off. Everyone who wants Halo 2 physics runs the manager themselves: it drives only that player's own character. Turn **30 tick off** online (the game's 60 for everyone; that also puts the Halo 2 physics at 60 ticks). Co-op campaign and Firefight run in lockstep, so a physics mod on one machine can knock them out of sync: stick to multiplayer custom games. So far tested with two players.

For jumping online use **Save spot / Back to spot**. **Force Revert** still works online, but it rolls the whole game back on your machine only: an object that was destroyed or respawned since your checkpoint (a crate, a fusion coil) comes back pointing at network data that is gone, and the game can crash. Use it for object setups, knowing that.

## Credits
* **Poorboy**: Poorboys Checkpoint Manager and its Halo 2 physics for Halo 3 and ODST: Halo 2's superbounces, movement and jump, button combos, solid objects (riding lifts, pushing crates), and the barrier, tick-rate and falling-death switches.
* **Burnt-o (Maurice Dix) and Harc**: [Halo Checkpoint Manager](https://github.com/Burnt-o/HaloCheckpointManager), which all of this is built on: dumping and injecting checkpoints, the checkpoint library, the in-game overlay, the hotkey system and every practice cheat (invulnerability, speedhack, teleport, free camera and the rest). Everything that is not the Halo 2 physics is their work.
* Third-party libraries: see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## What it adds to Halo Checkpoint Manager
A **Halo 2 Physics** section in the in-game overlay (Halo 3 and ODST), every switch with a rebindable hotkey:
* Halo 2 physics on / off (Halo 2's player body, movement and jump; superbounces work as in Halo 2)
* Barriers: the game's own, soft ceilings off, out-of-bounds kill off, kill volumes off
* 30 tick on / off, any time: the game and the Halo 2 physics together at 30 (Xbox Halo 2: superbounces, big landings on slopes stop on ledges) or 60 (PC Halo 2: you slide down slopes, no superbounces from ordinary heights)
* Halo 2's button combos, including the plasma pistol double grenade (the pistol's overheat cuts a throw short)
* Falling deaths on / off
* Live status: armed or not, speed against the 5.45 a step has to pass
* Fly: your player flies where you look (others see it)

And in **Save Management**, above Force Checkpoint: **Save spot** / **Back to spot** (Halo 3 and ODST, with Halo 2 physics on or off; you face the way you looked when you saved), bindable to a key or a controller button.

**Bottomless Clip** (Useful Cheats) now also works in Halo 3 and ODST: your own magazine never empties, plasma batteries don't drain and your grenades never run out (you need at least one of a kind to throw it); reserve ammo still counts, and plasma weapons still overheat.

It runs side by side with the original Halo Checkpoint Manager's files without clashing (its own dll names, shared memory and process names), but use one of the two at a time.

## Halo Checkpoint Manager's features (all still here)
* Back up (dump) to file the games checkpoints, so you can inject them again at any later time.
* UI for managing your checkpoint collection (ordering, sorting, renaming etc).
* Single-player cheats to help you practice and route the games, such as
   * Force custom checkpoints & back them up
   * Invincibility
   * Speedhack (both slow-down and fast-forward)
   * Block the games natural checkpoints
   * Teleport and Launch the player
   * And more (specific games have different features)
* Support for multiple downpatched versions of MCC - Season 7 (2448), Season 8 (2645), and of course current patch.

Anti-cheat must be off, as for Halo Checkpoint Manager.

## Building from source
Visual Studio 2022 with .NET desktop development, Desktop development with C++ and a Windows 10 SDK. The C++ dependencies come from vcpkg manifests, pinned to a 2025-07 vcpkg (`builtin-baseline` in each `vcpkg.json`; newer curl ports dropped the `non-http` and `schannel` features these projects ask for). With a vcpkg clone that is not integrated into Visual Studio:

    msbuild HaloCheckpointManager.sln /restore /m /p:Configuration=Release /p:Platform=x64 /p:VcpkgRoot=<vcpkg>\ /p:ForceImportBeforeCppTargets=<vcpkg>\scripts\buildsystems\msbuild\vcpkg.targets

Everything lands in `HCMExternal\bin\x64\Release\net7.0-windows\`; run `PoorboysCheckpointManager.exe`. The original notes: SafetyHook by cursey is included as a submodule; after a first full build, rebuild the whole solution so all build events fire.
