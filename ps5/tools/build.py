#!/usr/bin/env python3
"""Build native SDL3, Dawn and Dusklight, then link the PS5 executable."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PINS = json.loads((ROOT / 'ps5/dependencies.json').read_text())


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def configure(source, build, sdk, options):
    run('cmake', '-S', source, '-B', build, '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={sdk}/toolchain/prospero.cmake',
        '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', '-DCMAKE_BUILD_TYPE=Release',
        *[f'-D{name}={value}' for name, value in options.items()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--setup', action='store_true', help='Fetch and prepare pinned dependencies first')
    parser.add_argument('--build-radv', action='store_true')
    parser.add_argument('--vulkan-dir', type=Path)
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('BUILD_JOBS', '4')))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.setup:
        cmd = ['python3', ROOT / 'ps5/tools/setup-deps.py']
        if args.vulkan_dir:
            cmd += ['--vulkan-dir', args.vulkan_dir]
        if args.build_radv:
            cmd += ['--build-radv']
        run(*cmd)
    sdk = Path(os.environ.get('PS5_PAYLOAD_SDK', ROOT / '.deps/native/ps5-payload-sdk')).resolve()
    saved = ROOT / '.deps/native/vulkan-path.txt'
    vulkan = args.vulkan_dir or Path(os.environ.get('PS5_VULKAN_DIR',
        saved.read_text().strip() if saved.is_file() else ROOT.parent / 'PS5_Vulkan'))
    vulkan = vulkan.resolve()
    for name, folder in [('dawn', 'dawn'), ('sdl3', 'SDL'), ('abseil2024', 'abseil-20240722')]:
        revision = subprocess.check_output(['git', '-C', str(ROOT / '.deps/src' / folder),
                                            'rev-parse', 'HEAD'], text=True).strip()
        dirty = subprocess.check_output(['git', '-C', str(ROOT / '.deps/src' / folder),
                                         'status', '--porcelain'], text=True).strip()
        if dirty:
            raise RuntimeError(f'{name} has uncommitted changes; commit them before building.')
        if revision != PINS[name]['revision']:
            raise RuntimeError(f'{name} differs from its pinned revision. Run setup-deps.py.')
    sdl = ROOT / 'build/sdl3-ps5'
    sdl_options = dict(SDL_SHARED='OFF', SDL_STATIC='ON', SDL_TEST_LIBRARY='OFF', SDL_TESTS='OFF',
        SDL_UNIX_CONSOLE_BUILD='ON', SDL_X11='OFF', SDL_WAYLAND='OFF', SDL_KMSDRM='OFF',
        SDL_AUDIO='ON', SDL_JOYSTICK='ON', SDL_HAPTIC='ON', SDL_SENSOR='ON', SDL_HIDAPI='OFF',
        SDL_DUMMYAUDIO='OFF', SDL_DISKAUDIO='OFF', SDL_CAMERA='OFF', SDL_DIALOG='OFF', SDL_TRAY='OFF',
        SDL_VULKAN='OFF', SDL_OPENGL='OFF', SDL_OPENGLES='OFF',
        LIBC_HAS_WCSNLEN='OFF', LIBC_HAS_WCSLCPY='OFF', LIBC_HAS_WCSLCAT='OFF')
    configure(ROOT / '.deps/src/SDL', sdl, sdk, sdl_options)
    run('cmake', '--build', sdl, '--target', 'SDL3-static', '--parallel', args.jobs)
    dawn = ROOT / 'build/dawn-ps5'
    dawn_options = dict(DAWN_FETCH_DEPENDENCIES='ON', DAWN_BUILD_MONOLITHIC_LIBRARY='STATIC',
        DAWN_BUILD_PROTOBUF='OFF', TINT_BUILD_IR_BINARY='OFF', DAWN_SUPPORTS_CXX_MODULES='OFF',
        DAWN_BUILD_SAMPLES='OFF', DAWN_BUILD_TESTS='OFF', DAWN_BUILD_BENCHMARKS='OFF',
        DAWN_ENABLE_D3D11='OFF', DAWN_ENABLE_D3D12='OFF', DAWN_ENABLE_METAL='OFF',
        DAWN_ENABLE_DESKTOP_GL='OFF', DAWN_ENABLE_OPENGLES='OFF', DAWN_ENABLE_NULL='OFF',
        DAWN_ENABLE_VULKAN='ON', DAWN_ENABLE_WEBGPU_ON_WEBGPU='OFF',
        DAWN_SUPPORTS_GLFW_FOR_WINDOWING='OFF', DAWN_USE_X11='OFF', DAWN_USE_WAYLAND='OFF',
        TINT_BUILD_TESTS='OFF', TINT_BUILD_CMD_TOOLS='OFF', TINT_BUILD_SPV_WRITER='ON',
        TINT_BUILD_GLSL_WRITER='OFF', TINT_BUILD_HLSL_WRITER='OFF', TINT_BUILD_MSL_WRITER='OFF',
        CMAKE_CXX_FLAGS='-fno-omit-frame-pointer')
    configure(ROOT / '.deps/src/dawn', dawn, sdk, dawn_options)
    run('cmake', '--build', dawn, '--target', 'webgpu_dawn', '--parallel', args.jobs)
    configure(ROOT, ROOT / 'build/game-ps5', sdk, dict(DUSK_ENABLE_CODE_MODS='OFF',
        AURORA_DAWN_PROVIDER='vendor', FETCHCONTENT_SOURCE_DIR_DAWN=str(ROOT / '.deps/src/dawn'),
        **{'FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP':str(ROOT / '.deps/src/abseil-20240722')},
        AURORA_SDL3_PROVIDER='system', SDL3_DIR=str(sdl), AURORA_ENABLE_TESTS='OFF',
        DAWN_SUPPORTS_CXX_MODULES='OFF', DAWN_BUILD_PROTOBUF='OFF', TINT_BUILD_IR_BINARY='OFF',
        DAWN_ENABLE_VULKAN='ON', DAWN_ENABLE_DESKTOP_GL='OFF', DAWN_ENABLE_OPENGLES='OFF',
        DAWN_USE_X11='OFF', DAWN_USE_WAYLAND='OFF'))
    env = os.environ.copy()
    env.update(PS5_PAYLOAD_SDK=str(sdk), PS5_VULKAN_DIR=str(vulkan), BUILD_JOBS=str(args.jobs))
    run('python3', ROOT / 'ps5/tools/build-native-game.py', env=env)


if __name__ == '__main__':
    main()
