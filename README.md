# Poorboys Checkpoint Manager
*A practice tool for [Halo: The Master Chief Collection](https://store.steampowered.com/app/976730/Halo_The_Master_Chief_Collection/): Halo Checkpoint Manager with Halo 2's physics built in for Halo 3 and Halo 3: ODST.*

## Download
Get the latest zip from the **Releases** page (on the right). Unzip it, keep the folder together, and run `PoorboysCheckpointManager.exe`. Nothing to install: .NET and the C++ runtime are inside. `HOW TO USE.txt` in the zip has the details.

1. Start MCC with anti-cheat off (Steam: Play, then "Halo: MCC Anti-Cheat Disabled").
2. Run `PoorboysCheckpointManager.exe` and accept the administrator prompt.
3. In a Halo 3 or ODST game, open the overlay with ` (the key left of 1), tick **Halo 2 physics** (F6), then close the overlay so the game runs.

Made for MCC 1.3528 on Steam.

**Antivirus:** some antivirus programs wrongly flag `h3sb\helper\h3sb_helper.exe` (the map window and the lines in the world; it is a Python program packed into an exe, and those get false alarms) and quarantine it. The manager then says the helper is missing. Restore it in Windows Security > Virus & threat protection > Protection history (or your antivirus's quarantine) and allow it, or unzip again.

Everything Poorboy added is in **Poorboy's Mods**, the first section of the overlay (Halo 3 and ODST), apart from Halo Checkpoint Manager's own sections below it.

To watch the others, **Fly** (Poorboy's Mods) flies your player where you look, through walls: the others see you. **Free Camera** (Camera section) flies only your view, unseen.

To try a jump again, **Save spot** and **Back to spot** (Poorboy's Mods) put you back where you saved: the place, your speed, crouch, which way you look and Halo 2's armed state. Only your own player moves, nothing is rolled back.

## Playing online
Custom games with friends who also have anti-cheat off. Everyone who wants Halo 2 physics runs the manager themselves, **the same version** (versions before 1.0.1 drive the host's character on a non-host's screen, so a non-host gets no Halo 2 physics and cannot superbounce): it drives only that player's own character. Superbounces need **30 tick on** (F9): at 60 the Halo 2 physics runs at 60 ticks too, and superbounces from ordinary heights don't happen. Everyone uses the same 30 tick setting as the host. Co-op campaign and Firefight run in lockstep, so a physics mod on one machine can knock them out of sync: stick to multiplayer custom games. So far tested with two players.

For jumping online use **Save spot / Back to spot**. **Force Revert** still works online, but it rolls the whole game back on your machine only: an object that was destroyed or respawned since your checkpoint (a crate, a fusion coil) comes back pointing at network data that is gone, and the game can crash. Use it for object setups, knowing that.

## Credits
* **Poorboy**: Poorboys Checkpoint Manager and its Halo 2 physics for Halo 3 and ODST: Halo 2's superbounces, movement and jump, button combos, solid objects (riding lifts, pushing crates), and the barrier, tick-rate and falling-death switches.
* **Burnt-o (Maurice Dix) and Harc**: [Halo Checkpoint Manager](https://github.com/Burnt-o/HaloCheckpointManager), which all of this is built on: dumping and injecting checkpoints, the checkpoint library, the in-game overlay, the hotkey system and every practice cheat (invulnerability, speedhack, teleport, free camera and the rest). Everything that is not the Halo 2 physics is their work.
* Third-party libraries: see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## What it adds to Halo Checkpoint Manager
A **Poorboy's Mods** section at the top of the in-game overlay (Halo 3 and ODST), kept apart from Halo Checkpoint Manager's own features, every switch with a rebindable hotkey:
* Halo 2 physics on / off (Halo 2's player body, movement and jump; superbounces work as in Halo 2)
* Barriers: the game's own, soft ceilings off, out-of-bounds kill off, kill volumes off
* 30 tick on / off, any time: the game and the Halo 2 physics together at 30 (Xbox Halo 2: superbounces, big landings on slopes stop on ledges) or 60 (PC Halo 2: you slide down slopes, no superbounces from ordinary heights)
* Halo 2's button combos, including the plasma pistol double grenade (the pistol's overheat cuts a throw short) and X during a weapon switch swapping at once
* Halo 2's lunges (with Halo 2 physics on): a melee or sword lunge flies straight at the target with no gravity and keeps going when you cancel the attack, so **butterflying** works (spam melee + reload at an enemy standing on your head while he jumps: you both climb), and **sword flying** (rifle locked on someone, Y, X, RT: the sword lunges at what the rifle had locked)
* Falling deaths on / off
* Live status: armed or not, speed against the 5.45 a step has to pass
* Fly: your player flies where you look (others see it)
* **Save spot** / **Back to spot** (with Halo 2 physics on or off; you face the way you looked when you saved)
* **Poorboy's logo** in the top-left corner of Halo 3 and ODST games, left of the grenade counts, so a clip shows it was played with Poorboy's mods. Always on (Halo Checkpoint Manager's own watermark can be hidden; the logo cannot). Like the rest of the overlay, it stays out of OBS when Halo Checkpoint Manager's OBS bypass is on.
* **Bottomless Clip**: your own magazine never empties, plasma batteries don't drain and your grenades never run out (you need at least one of a kind to throw it); reserve ammo still counts, and plasma weapons still overheat. (In Halo CE and Halo 2 it is Halo Checkpoint Manager's own, under Useful Cheats.)

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
