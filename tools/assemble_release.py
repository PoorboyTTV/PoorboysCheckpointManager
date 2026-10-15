"""Puts together (or updates in place) a ready-to-run Poorboys Checkpoint Manager folder.

    python tools\assemble_release.py [<destination folder>]

  <dest>\              the Release build (HCMExternal\bin\x64\Release\net7.0-windows) without debug files, plus
                       InternalPointerData.xml, README.md, LICENSE, THIRD-PARTY-LICENSES.md
  <dest>\h3sb\         the Halo 2 physics mod: h3sb.dll (h2-bounce-atlas\re\port\bin2, build.bat bin2)
  <dest>\h3sb\helper\  the map window / call-outs / world lines (re\bundle\make_bundle.py builds it)
  <dest>\h3sb\spots\   the jump-spot data the helpers draw
Run PoorboysCheckpointManager.exe from <dest>.
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, 'HCMExternal', 'bin', 'x64', 'Release', 'net7.0-windows')
ATLAS = os.environ.get('H3SB_SOURCE', os.path.join(os.path.dirname(os.path.dirname(ROOT)), 'h2-bounce-atlas', 're'))
MOD_DLL = os.path.join(ATLAS, 'port', 'bin2', 'h3sb.dll')
BUNDLE = os.path.join(ATLAS, 'bundle', 'out', 'Halo 2 Physics for MCC Halo 3')
DEST = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), 'release', 'Poorboys Checkpoint Manager')
SKIP_EXT = ('.pdb', '.lib', '.exp')


def put(src, dest_dir):
    """copy unless the same file is already there (a dll MCC still holds, e.g. PCMSpeedhack.dll, cannot be overwritten)"""
    dest = os.path.join(dest_dir, os.path.basename(src))
    if os.path.isfile(dest) and os.path.getsize(dest) == os.path.getsize(src):
        with open(src, 'rb') as a, open(dest, 'rb') as b:
            if a.read() == b.read():
                return
    try:
        shutil.copy2(src, dest)
    except PermissionError:
        print('NOT UPDATED (in use - close MCC and run this again): %s' % dest)


def main():
    # update in place: the user's things in <dest> stay (Saves\, Logs\, HCMInternalConfig.xml, HCMHotkeyConfig.xml,
    # CheckpointInjections.txt, and the mod's own files in h3sb\: its log, switches, world files and recordings)
    os.makedirs(DEST, exist_ok=True)
    for name in sorted(os.listdir(BUILD)):
        src = os.path.join(BUILD, name)
        if os.path.isfile(src) and not name.lower().endswith(SKIP_EXT):
            put(src, DEST)
    put(os.path.join(ROOT, 'HCMInternal', 'InternalPointerData.xml'), DEST)
    for name in ('README.md', 'LICENSE', 'THIRD-PARTY-LICENSES.md'):
        put(os.path.join(ROOT, name), DEST)
    mod = os.path.join(DEST, 'h3sb')
    os.makedirs(mod, exist_ok=True)
    put(MOD_DLL, mod)
    for sub in ('helper', 'spots'):            # program data only: replaced whole
        target = os.path.join(mod, sub)
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(os.path.join(BUNDLE, sub), target)
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(DEST) for f in fs)
    print('%s: %.1f MB' % (DEST, size / 1e6))


if __name__ == '__main__':
    main()
