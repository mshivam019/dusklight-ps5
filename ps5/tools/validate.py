#!/usr/bin/env python3
"""Validate source hygiene, synthetic disc handling and the native title build."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    subprocess.run(list(map(str, args)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true', help='Run the full dependency/game build first')
    parser.add_argument('--vulkan-dir', type=Path, default=ROOT.parent / 'PS5_Vulkan')
    parser.add_argument('--title', type=Path, help='Also validate a staged release title')
    args = parser.parse_args()
    run('git', '-C', ROOT, 'diff', '--check')
    for path in (ROOT / 'ps5/tools').glob('*.py'):
        ast.parse(path.read_text(), filename=str(path))
    for path in (ROOT / 'ps5/tools').glob('*.sh'):
        run('bash', '-n', path)
    for path in (ROOT / 'ps5').rglob('*.json'):
        json.loads(path.read_text())
    tracked = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z'], text=True).split('\0')
    forbidden = {'.iso', '.gcm', '.rvz', '.wbfs', '.pkg', '.key', '.pem', '.mp4', '.mkv', '.log', '.pyc'}
    for name in tracked:
        if name and (Path(name).suffix.lower() in forbidden or name.startswith(('.deps/', 'private-assets/', 'dist/'))):
            raise ValueError(f'Private/build file tracked in the repository: {name}')
    pins = json.loads((ROOT / 'ps5/dependencies.json').read_text())
    runtime = args.vulkan_dir / 'runtime/libc.prx'
    with runtime.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != pins['runtime']['sha256']:
            raise ValueError('Runtime hash differs from the clean-room module')
    radv = args.vulkan_dir / '.deps/native/radv-release/lib/libvulkan_radeon.ps5.a'
    with radv.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != pins['radv']['sha256']:
            raise ValueError('RADV archive differs from the console-tested build')
    with tempfile.TemporaryDirectory() as temporary:
        target = Path(temporary) / 'disc-test'
        header = ROOT / 'build/game-ps5/_deps/aurora_nod-src/nod-ffi/include'
        run('clang++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-I', header, ROOT / 'ps5/disc/raw_gamecube.cpp',
            ROOT / 'ps5/disc/raw_gamecube_test.cpp', '-o', target)
        run(target)
    if args.build:
        run('python3', ROOT / 'ps5/tools/build.py', '--vulkan-dir', args.vulkan_dir)
    executable = ROOT / 'build/native-game/eboot.bin'
    if not executable.is_file():
        raise ValueError('Native build is missing; run build.py or pass --build')
    run(args.vulkan_dir / 'build/host/ps5-native-tool', 'self', '--inspect', '--file', executable)
    if args.title:
        run('python3', ROOT / 'ps5/tools/package-release.py', '--stage', args.title, '--check-only')
    print('Source, disc tests, runtime/driver checks and native SELF validation passed')


if __name__ == '__main__':
    main()
