#!/usr/bin/env python3
"""Build and link the native title using the prepared, pinned dependencies."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ.get('PS5_PAYLOAD_SDK', root / '.deps/native/ps5-payload-sdk'))
vulkan = Path(os.environ.get('PS5_VULKAN_DIR', root.parent / 'PS5_Vulkan'))
work = root / 'build/native-game'
build = root / 'build/game-ps5'
work.mkdir(parents=True, exist_ok=True)
subprocess.run(['cmake', '--build', str(build), '--target', 'dusklight',
                '--parallel', os.environ.get('BUILD_JOBS', '4')], check=True)
subprocess.run([str(sdk / 'bin/prospero-clang++'), '-std=c++20', '-O2',
                '-I', str(root / '.deps/src/SDL/include'), '-c',
                str(root / 'ps5/main.cpp'), '-o', str(work / 'main.o')], check=True)
# Aurora's standalone main is replaced above. RADV already supplies zlib.
archives = sorted(p for p in build.rglob('*.a')
                  if p.name not in {'libaurora_main.a', 'libz.a'})
archives += [root / 'build/dawn-ps5/src/dawn/native/libwebgpu_dawn.a',
             root / 'build/sdl3-ps5/libSDL3.a']
for archive in archives:
    if not archive.is_file():
        raise FileNotFoundError(archive)
subprocess.run(['bash', str(root / 'ps5/tools/link-game.sh'), str(work),
                str(vulkan), str(sdk), str(work / 'main.o'), '--start-group',
                *map(str, archives), '--end-group'], check=True)
print(work / 'eboot.bin')
