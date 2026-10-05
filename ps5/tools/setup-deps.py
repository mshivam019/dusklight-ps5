#!/usr/bin/env python3
"""Fetch pinned sources and prepare the PS5 SDK and Vulkan link tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / '.deps/src'
NATIVE = ROOT / '.deps/native'
PINS = json.loads((ROOT / 'ps5/dependencies.json').read_text())


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def checkout(name, path):
    pin = PINS[name]
    if not (path / '.git').exists():
        path.mkdir(parents=True, exist_ok=True)
        run('git', 'init', path)
        run('git', '-C', path, 'remote', 'add', 'origin', pin['repository'])
    current = subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'],
                             capture_output=True, text=True)
    dirty = subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain'], text=True)
    if dirty:
        raise RuntimeError(f'Refusing to use a modified dependency: {path}')
    if current.returncode == 0 and current.stdout.strip() == pin['revision']:
        return path
    run('git', '-C', path, 'fetch', '--depth=1', pin['repository'], pin['revision'])
    run('git', '-C', path, 'checkout', '--detach', pin['revision'])
    return path


def export(repo, revision, destination):
    check = subprocess.run(['git', '-C', str(repo), 'cat-file', '-e', revision],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if check.returncode:
        run('git', '-C', repo, 'fetch', '--depth=1', PINS['sdk-platform']['repository'], revision)
    with tempfile.TemporaryFile() as stream:
        run('git', '-C', repo, 'archive', revision, stdout=stream)
        stream.seek(0)
        run('tar', '-x', '-C', destination, stdin=stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources-only', action='store_true')
    parser.add_argument('--vulkan-dir', type=Path)
    parser.add_argument('--build-radv', action='store_true',
                        help='Build the tested Mesa pin if a RADV archive is missing')
    args = parser.parse_args()
    paths = {name: checkout(name, SRC / folder) for name, folder in
             [('dawn', 'dawn'), ('sdl3', 'SDL'), ('abseil2024', 'abseil-20240722'),
              ('sdk-platform', 'sdk'), ('mesa', 'mesa')]}
    vulkan = args.vulkan_dir.resolve() if args.vulkan_dir else checkout('vulkan-runtime', SRC / 'vulkan')
    # Private Aurora is the application's submodule, not a second working copy.
    run('git', '-C', ROOT, 'submodule', 'sync', '--', 'extern/aurora')
    run('git', '-C', ROOT, 'submodule', 'update', '--init', '--', 'extern/aurora', 'extern/borealis')
    if args.sources_only:
        return
    NATIVE.mkdir(parents=True, exist_ok=True)
    sdk = NATIVE / 'ps5-payload-sdk'
    with tempfile.TemporaryDirectory(dir=NATIVE) as temporary:
        tree = Path(temporary)
        export(paths['sdk-platform'], PINS['base-sdk']['revision'], tree)
        run('bash', tree / 'platform/tools/setup-sdk.sh', sdk,
            PINS['base-sdk']['revision'], NATIVE)
    # Overlay exactly the platform library and headers used by the console build.
    run('make', '-C', paths['sdk-platform'] / 'platform',
        f'CC={sdk}/bin/prospero-clang', f'AR={sdk}/bin/llvm-ar')
    shutil.copy2(paths['sdk-platform'] / 'platform/build/ps5/libps5platform.a',
                 sdk / 'target/lib/libps5platform.a')
    shutil.copytree(paths['sdk-platform'] / 'platform/include/ps5platform',
                    sdk / 'target/include/ps5platform', dirs_exist_ok=True)
    (sdk / '.twilight-platform-revision').write_text(PINS['sdk-platform']['revision'] + '\n')
    run('bash', vulkan / 'tools/setup-native-dependencies.sh', '--skip-sdk')
    tool = vulkan / 'build/host/ps5-native-tool'
    tool.parent.mkdir(parents=True, exist_ok=True)
    zlib = vulkan / '.deps/native/zlib/root'
    archive = next(zlib.rglob('libz.a'))
    native = vulkan / 'tooling/native'
    run(os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', zlib / 'usr/include', *(native / x for x in
        ['native_app_builder.cpp', 'self_container.cpp', 'elf_object.cpp', 'sce_module_writer.cpp']),
        archive, '-o', tool)
    runtime = vulkan / 'runtime/libc.prx'
    if not runtime.is_file():
        run('bash', vulkan / 'tools/rebuild-libc.sh')
    if hashlib.file_digest(runtime.open('rb'), 'sha256').hexdigest() != PINS['runtime']['sha256']:
        raise RuntimeError('Clean-room runtime hash mismatch')
    radv = vulkan / '.deps/native/radv-release/lib/libvulkan_radeon.ps5.a'
    if not radv.exists():
        if not args.build_radv:
            raise RuntimeError('RADV is missing. Rerun with --build-radv or supply --vulkan-dir.')
        with tempfile.TemporaryDirectory(dir=NATIVE) as temporary:
            tree = Path(temporary)
            export(paths['sdk-platform'], PINS['radv']['sdk_revision'], tree)
            run('bash', tree / 'platform/tools/setup-sdk.sh',
                vulkan / '.deps/native/ps5-payload-sdk', PINS['radv']['sdk_revision'],
                vulkan / '.deps/native')
        env = os.environ.copy()
        env.update(PS5_MESA_FORK=str(paths['mesa']), PS5_MESA_REVISION=PINS['mesa']['revision'])
        run('bash', vulkan / 'tools/build-radv.sh', 'release', env=env)
    (NATIVE / 'vulkan-path.txt').write_text(str(vulkan) + '\n')
    print('SDK and native Vulkan dependencies ready')


if __name__ == '__main__':
    main()
