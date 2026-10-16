"""Builds the zip to give someone: Poorboys Checkpoint Manager, ready to run, nothing to install.

    python tools\package.py [<zip path>]          (default: dist\Poorboys Checkpoint Manager.zip next to the repo)

  Paths (environment variables): H3SB_SOURCE = the Halo 2 physics mod's re folder (default ..\h2-bounce-atlas\re next to
  the repo's parent), VCPKG_ROOT = vcpkg (default ..\vcpkg), MSBUILD = MSBuild.exe (default VS 2022 Community).

  * builds the solution (Release x64) and publishes the app self-contained: the .NET runtime is inside
  * the C++ dlls carry their own runtime (/MT), so no VC++ redistributable either
  * a clean folder: none of this PC's settings, hotkeys, logs, checkpoint saves or the mod's recordings
  * the Halo 2 physics mod in h3sb\ (h3sb.dll from re\port\bin2, helper and spots from re\bundle's last build)
"""
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HCM = os.path.dirname(ROOT)
BUILD = os.path.join(ROOT, 'HCMExternal', 'bin', 'x64', 'Release', 'net7.0-windows')
ATLAS = os.environ.get('H3SB_SOURCE', os.path.join(os.path.dirname(HCM), 'h2-bounce-atlas', 're'))
MOD_DLL = os.path.join(ATLAS, 'port', 'bin2', 'h3sb.dll')
BUNDLE = os.path.join(ATLAS, 'bundle', 'out', 'Halo 2 Physics for MCC Halo 3')
NAME = 'Poorboys Checkpoint Manager'
STAGE = os.path.join(HCM, 'dist', NAME)
ZIP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HCM, 'dist', NAME + '.zip')
MSBUILD = os.environ.get('MSBUILD', r'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe')
VCPKG = os.environ.get('VCPKG_ROOT', os.path.join(HCM, 'vcpkg'))
COMMON = ['/nologo', '/v:m', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64']
HOW_TO = """Poorboys Checkpoint Manager
Halo Checkpoint Manager (by Burnt-o and Harc) with Poorboy's Halo 2 physics for Halo 3 and Halo 3: ODST.
Made for MCC 1.3528 on Steam.

1. Start MCC with anti-cheat OFF (Steam: Play, then "Halo: MCC Anti-Cheat Disabled").
2. Run PoorboysCheckpointManager.exe from this folder and say yes to the administrator prompt.
   Keep the folder together: everything in it is used. Windows may warn about an unknown publisher (it is not signed).
3. In MCC start a Halo 3 or ODST game. The overlay opens and closes with ` (the key left of 1).
4. Halo 2 Physics, at the top of the overlay: tick "Halo 2 physics" or press F6. Then close the overlay with ` :
   the game is paused while the overlay is open, and the physics takes hold once the game runs.
     F7   barriers (the game's own / soft ceilings off / + out-of-bounds kill off / + kill volumes off)
     F9   30 / 60 tick, any time: game and Halo 2 physics together (30 = Xbox Halo 2 with superbounces,
          60 = PC Halo 2: slides down slopes, no superbounces from ordinary heights)
     F10  Halo 2 button combos           F11  no falling deaths
          (the combos include the plasma pistol double grenade: charged shot, about a second later grenade, grenade)
     Arm now  the armed state without finding a spot to crouch-stand in: bind it yourself with its .. button
     Fly      your player flies where you look, jump up, crouch down (others see it); bind it yourself
   Useful Cheats: "Bottomless Clip" works in Halo 3 and ODST too (your magazine never empties; reserve ammo and
   grenades still count). Reload first: a part-empty magazine stays as it is.
   Save Management: "Save spot" and "Back to spot" put you back where you saved (place, speed, view, armed state)
   without rolling the game back, so they are safe online. Bind them yourself with their .. buttons.
   Force Checkpoint / Force Revert still roll the whole game back (objects too).
5. Every hotkey can be rebound with the .. button beside it: a key, or a controller button or combination.
6. Helper windows (in the Halo 2 Physics section): the map window with call-outs, and lines drawn in the world.
   "The helper is missing": your antivirus removed h3sb\\helper\\h3sb_helper.exe (a false alarm). Restore it in
   Windows Security > Virus & threat protection > Protection history (or your antivirus's quarantine), or unzip again.

Style: Control > Style changes the overlay's look.
Problems: send the Logs folder (and any HCM_CRASHDUMP file) to Poorboy - not to Burnt.
"""


def run(cmd):
    print('>', ' '.join(cmd[:3]), '...')
    r = subprocess.run(cmd, capture_output=True, text=True)
    errors = [l for l in r.stdout.splitlines() if ' error ' in l or 'error C' in l or 'error MSB' in l or 'error NETSDK' in l]
    if r.returncode != 0 or errors:
        print('\n'.join(errors or r.stdout.splitlines()[-30:]))
        sys.exit('build failed')


def move_held_pdbs():
    """MCC keeps the build folder's PCMInternal.pdb open after a session (dbghelp from the overlay's stack traces), and
    the linker then fails with LNK1201. A held file can still be renamed: move it aside so the build writes a new one."""
    import time
    for name in os.listdir(BUILD):
        if name.lower().endswith('.pdb') and name.startswith('PCM') and '.old_' not in name:
            try:
                os.replace(os.path.join(BUILD, name), os.path.join(BUILD, '%s.old_%d.pdb' % (name[:-4], int(time.time()))))
            except OSError:
                pass


def main():
    move_held_pdbs()
    run([MSBUILD, os.path.join(ROOT, 'HaloCheckpointManager.sln'), '/m', '/restore'] + COMMON +
        ['/p:VcpkgRoot=' + VCPKG + '\\', '/p:ForceImportBeforeCppTargets=' + os.path.join(VCPKG, 'scripts', 'buildsystems', 'msbuild', 'vcpkg.targets')])
    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)
    run([MSBUILD, os.path.join(ROOT, 'HCMExternal', 'HCMExternal.csproj'), '/t:Publish', '/restore'] + COMMON +
        ['/p:RuntimeIdentifier=win-x64', '/p:SelfContained=true', '/p:BuildProjectReferences=false', '/p:PublishDir=' + STAGE + '\\'])
    shutil.copy2(os.path.join(STAGE, 'PCMExternal.exe'), os.path.join(STAGE, 'PoorboysCheckpointManager.exe'))
    for name in ('PCMInternal.dll', 'PCMSpeedhack.dll', 'HCMInterproc.dll', 'ExternalPointerData.xml'):
        shutil.copy2(os.path.join(BUILD, name), STAGE)
    shutil.copy2(os.path.join(ROOT, 'HCMInternal', 'InternalPointerData.xml'), STAGE)
    for name in ('README.md', 'LICENSE', 'THIRD-PARTY-LICENSES.md'):
        shutil.copy2(os.path.join(ROOT, name), STAGE)
    with open(os.path.join(STAGE, 'HOW TO USE.txt'), 'w', newline='\r\n') as f:
        f.write(HOW_TO)
    mod = os.path.join(STAGE, 'h3sb')
    os.makedirs(mod)
    shutil.copy2(MOD_DLL, mod)
    shutil.copytree(os.path.join(BUNDLE, 'helper'), os.path.join(mod, 'helper'))
    shutil.copytree(os.path.join(BUNDLE, 'spots'), os.path.join(mod, 'spots'))
    for d, _, files in os.walk(STAGE):
        for f in files:
            if f.lower().endswith('.pdb'):
                os.remove(os.path.join(d, f))
    if os.path.exists(ZIP):
        os.remove(ZIP)
    with zipfile.ZipFile(ZIP, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for d, _, files in os.walk(STAGE):
            for f in files:
                full = os.path.join(d, f)
                z.write(full, os.path.join(NAME, os.path.relpath(full, STAGE)))
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(STAGE) for f in fs)
    print('folder %s: %.1f MB' % (STAGE, size / 1e6))
    print('zip %s: %.1f MB' % (ZIP, os.path.getsize(ZIP) / 1e6))


if __name__ == '__main__':
    main()
