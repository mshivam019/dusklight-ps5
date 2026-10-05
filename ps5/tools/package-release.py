#!/usr/bin/env python3
"""Stage and validate a port-only title, then create Windows/Linux ZIPs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TITLE = 'PPSA99640'
PINS = json.loads((ROOT / 'ps5/dependencies.json').read_text())


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def copy_licenses(source, target):
    target.mkdir(parents=True, exist_ok=True)
    found = []
    for pattern in ('LICENSE*', 'COPYING*', 'NOTICE*'):
        found.extend(p for p in source.glob(pattern) if p.is_file())
    for path in sorted(set(found)):
        shutil.copy2(path, target / path.name)
    if (source / 'docs/FTL.TXT').is_file():
        shutil.copy2(source / 'docs/FTL.TXT', target / 'FTL.TXT')
        found.append(source / 'docs/FTL.TXT')
    return [p.name for p in sorted(set(found))]


def stage(title, vulkan):
    if title.exists():
        raise ValueError('Stage already exists. Choose a new --stage to preserve existing files.')
    title.mkdir(parents=True)
    shutil.copy2(ROOT / 'build/native-game/eboot.bin', title / 'eboot.bin')
    shutil.copytree(ROOT / 'ps5/sce_sys', title / 'sce_sys')
    shutil.copytree(ROOT / 'res', title / 'res')
    (title / 'sce_module').mkdir()
    runtime = vulkan / 'runtime/libc.prx'
    if digest(runtime) != PINS['runtime']['sha256']:
        raise ValueError('Runtime is not the recorded clean-room module')
    shutil.copy2(runtime, title / 'sce_module/libc.prx')
    (title / 'user').mkdir()
    shutil.copy2(ROOT / 'ps5/default-config.json', title / 'user/config.json')
    licenses = title / 'licenses'
    components = []
    sources = [('dusklight', ROOT), ('aurora', ROOT / 'extern/aurora'),
        ('borealis', ROOT / 'extern/borealis'), ('sdl3', ROOT / '.deps/src/SDL'),
        ('dawn', ROOT / '.deps/src/dawn'), ('sdk-platform', ROOT / '.deps/src/sdk'),
        ('vulkan-runtime', vulkan), ('mesa', ROOT / '.deps/src/mesa')]
    sources += [(p.name.removesuffix('-src'), p) for p in
                sorted((ROOT / 'build/game-ps5/_deps').glob('*-src')) if p.is_dir()]
    sources += [('dawn-' + p.name, p) for p in sorted((ROOT / '.deps/src/dawn/third_party').iterdir())
                if p.is_dir() and p.name in ('abseil-cpp', 'spirv-tools', 'spirv-headers', 'vulkan-headers', 'webgpu-headers')]
    for name, source in sources:
        if not source.exists():
            raise ValueError(f'Missing license source: {name}')
        texts = copy_licenses(source, licenses / name)
        components.append({'name':name, 'license_files':[f'licenses/{name}/{x}' for x in texts]})
    shutil.copytree(ROOT / 'ps5/licenses', licenses / 'fonts-and-llvm')
    (licenses / 'mesa/Mesa-license.rst').write_bytes(
        subprocess.check_output(['git', '-C', str(ROOT / '.deps/src/mesa'), 'show',
                                 PINS['mesa']['revision'] + ':docs/license.rst']))
    sdk_license = ROOT / '.deps/src/sdk/LICENSE'
    shutil.copy2(sdk_license, title / 'COPYING')
    (title / 'LEGAL.txt').write_text(
        'No game dumps, texture packs, saves, console firmware or keys are included.\n'
        'Supply a dump of your own legally obtained game.\n'
        'The linked PS5 platform/runtime is GPL-3.0-or-later; corresponding source accompanies this release.\n'
        'Dusklight is CC0; Aurora and Borealis are MIT; SDL3 is zlib; Dawn is BSD-3-Clause.\n'
        'Other libraries retain the licenses supplied under licenses/ and in the source archive.\n'
        'Fonts retain their upstream copyright notices and OFL/Apache licenses.\n'
        'The Midna icon, Dusklight logo and UI artwork originate in the upstream Dusklight repository.\n'
        'Not affiliated with Nintendo or Sony Interactive Entertainment.\n'
        'PlayStation and PS5 are trademarks of Sony Interactive Entertainment.\n'
        'Vulkan is a Khronos Group trademark. This RADV port is not a conformant Vulkan product.\n')
    build = {'revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
             'dependencies': PINS, 'components': components}
    (licenses / 'components.json').write_text(json.dumps(build, indent=2) + '\n')


def validate(title):
    required = ['eboot.bin', 'sce_sys/param.json', 'sce_sys/icon0.png', 'sce_module/libc.prx',
                'res/logo.png', 'res/rml/window.rcss', 'user/config.json', 'LEGAL.txt', 'COPYING',
                'licenses/components.json']
    for name in required:
        if not (title / name).is_file():
            raise ValueError(f'Missing required file: {name}')
    if json.loads((title / 'sce_sys/param.json').read_text())['titleId'] != TITLE:
        raise ValueError('Unexpected title ID')
    if digest(title / 'sce_module/libc.prx') != PINS['runtime']['sha256']:
        raise ValueError('Unexpected runtime module')
    files = []
    forbidden = {'.iso', '.gcm', '.rvz', '.wbfs', '.pkg', '.key', '.pem', '.log', '.mp4', '.mkv', '.pyc'}
    for path in sorted(title.rglob('*')):
        relative = path.relative_to(title)
        if path.is_symlink():
            raise ValueError(f'Symbolic link: {relative}')
        if not path.is_file():
            continue
        if path.suffix.lower() in forbidden or any(p in {'.git', '.deps', 'saves', 'texture_replacements', 'logs'} for p in relative.parts):
            raise ValueError(f'Private or game file in release: {relative}')
        if relative.parts[0] == 'user' and relative.as_posix() != 'user/config.json':
            raise ValueError(f'Unexpected user data: {relative}')
        if relative.parts[0] not in {'res', 'sce_sys', 'sce_module', 'licenses', 'user'} and relative.name not in {'eboot.bin', 'LEGAL.txt', 'COPYING'}:
            raise ValueError(f'Unexpected release path: {relative}')
        if relative.parts[0] == 'sce_module' and relative.as_posix() != 'sce_module/libc.prx':
            raise ValueError(f'Unexpected runtime: {relative}')
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='ps5-v0.1.0')
    parser.add_argument('--vulkan-dir', type=Path, default=ROOT.parent / 'PS5_Vulkan')
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'ps5-v\d+\.\d+\.\d+', args.version):
        parser.error('Version must be ps5-vMAJOR.MINOR.PATCH')
    title = args.stage or ROOT / 'dist' / args.version / TITLE
    if not args.check_only:
        if subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip():
            raise ValueError('Commit/squash the source before creating a release')
        stage(title, args.vulkan_dir.resolve())
    files = validate(title)
    if args.check_only:
        print(f'Validated {len(files)} port-only files')
        return
    out = ROOT / 'dist/releases' / args.version
    out.mkdir(parents=True, exist_ok=True)
    manifest = {'version': args.version, 'title':TITLE,
                'files':[{'path':p.relative_to(title).as_posix(), 'bytes':p.stat().st_size, 'sha256':digest(p)} for p in files]}
    for platform in ['linux', 'windows']:
        target = out / f'dusklight-{args.version}-{platform}.zip'
        if target.exists():
            raise ValueError(f'Package already exists: {target}')
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            def add(name, data, mode=0o644):
                info = zipfile.ZipInfo(name, (2026, 10, 6, 0, 0, 0));info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100000 | mode) << 16
                z.writestr(info, data)
            for path in files:
                add('output/' + TITLE + '/' + path.relative_to(title).as_posix(), path.read_bytes())
            helper = 'install.sh' if platform == 'linux' else 'install.ps1'
            for name in ['install.py', helper, 'prepare-textures.py']:
                add('tools/' + name, (ROOT / 'ps5/tools' / name).read_bytes(), 0o755 if name.endswith('.sh') else 0o644)
            add('README.md', (ROOT / 'README.md').read_bytes())
            add('INSTALL.txt', (ROOT / 'ps5/INSTALL.txt').read_bytes())
            add('MANIFEST.json', (json.dumps(manifest, indent=2) + '\n').encode())
        with zipfile.ZipFile(target) as z:
            if z.testzip() is not None:
                raise ValueError('ZIP CRC validation failed')
            for item in manifest['files']:
                if hashlib.sha256(z.read('output/' + TITLE + '/' + item['path'])).hexdigest() != item['sha256']:
                    raise ValueError('ZIP content differs from staged title')
        print(target)


if __name__ == '__main__':
    main()
