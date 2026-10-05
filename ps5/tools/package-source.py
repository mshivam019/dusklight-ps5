#!/usr/bin/env python3
"""Archive corresponding sources, excluding local builds and private game data."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
PINS = json.loads((ROOT / 'ps5/dependencies.json').read_text())
EXCLUDED_DIRS = {'.git', '.svn', '__pycache__', 'CMakeFiles', 'node_modules', '.venv', '.deps'}
EXCLUDED_SUFFIXES = {'.o', '.obj', '.a', '.so', '.dll', '.exe', '.elf', '.prx', '.pkg', '.iso', '.gcm',
                      '.rvz', '.wbfs', '.pyc', '.log', '.mp4', '.mkv', '.key', '.pem'}


def source_files(directory):
    for current, dirs, files in os.walk(directory):
        dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS and not (Path(current) / d).is_symlink())
        for name in sorted(files):
            file = Path(current) / name
            if file.is_symlink() or file.suffix.lower() in EXCLUDED_SUFFIXES:
                continue
            if name.startswith('.env') or name in {'.git', 'CMakeCache.txt', 'build.ninja', '.ninja_log', '.ninja_deps'}:
                continue
            with file.open('rb') as stream:
                magic = stream.read(8)
            if magic.startswith((b'\x7fELF', b'!<arch>\n', b'MZ', b'\x4f\x15\x3d\x1d')):
                continue
            yield file


def add_git(tar, repo, revision, prefix):
    command = subprocess.Popen(['git', '-C', str(repo), 'archive', '--format=tar', revision], stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=command.stdout, mode='r|') as source:
            for entry in source:
                if entry.name.startswith('/') or '..' in Path(entry.name).parts:
                    raise ValueError('Unsafe source archive path')
                entry.name = prefix + '/' + entry.name
                tar.addfile(entry, source.extractfile(entry) if entry.isfile() else None)
    finally:
        command.stdout.close()
        if command.wait() != 0:
            raise RuntimeError(f'git archive failed: {repo.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='ps5-v0.1.0')
    parser.add_argument('--vulkan-dir', type=Path, default=ROOT.parent / 'PS5_Vulkan')
    args = parser.parse_args()
    if subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip():
        raise ValueError('Commit the application before packaging source')
    out = ROOT / 'dist/releases' / args.version
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'dusklight-{args.version}-source.tar.gz'
    if archive.exists():
        raise ValueError('Source archive already exists')
    prefix = archive.name.removesuffix('.tar.gz')
    records = []
    with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
        revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        add_git(tar, ROOT, revision, prefix + '/port')
        records.append({'name':'dusklight', 'revision':revision, 'repository':'https://github.com/mshivam019/dusklight-ps5'})
        sources = [('aurora', ROOT / 'extern/aurora'), ('dawn', ROOT / '.deps/src/dawn'),
                   ('sdl3', ROOT / '.deps/src/SDL'), ('abseil2024', ROOT / '.deps/src/abseil-20240722'),
                   ('sdk-platform', ROOT / '.deps/src/sdk'), ('mesa', ROOT / '.deps/src/mesa'),
                   ('vulkan-runtime', args.vulkan_dir.resolve())]
        for name, repo in sources:
            pin = PINS[name]
            add_git(tar, repo, pin['revision'], prefix + '/sources/' + name)
            records.append({'name':name, **pin})
        for name in ['borealis']:
            repo = ROOT / 'extern' / name
            revision = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
            add_git(tar, repo, revision, prefix + '/sources/' + name)
            records.append({'name':name, 'revision':revision, 'repository':'https://github.com/encounter/' + name})
        # Dawn fetches nested repositories into ignored submodule paths. Preserve
        # their actual build revisions, including the private Abseil changes.
        dawn = ROOT / '.deps/src/dawn'
        for current, dirs, _ in os.walk(dawn / 'third_party'):
            dirs[:] = sorted(d for d in dirs if d != '.git')
            repo = Path(current)
            if (repo / '.git').exists():
                relative = repo.relative_to(dawn).as_posix()
                revision = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
                add_git(tar, repo, revision, prefix + '/sources/dawn/' + relative)
                records.append({'name':'dawn/' + relative, 'revision':revision})
                dirs[:] = []
        for source in sorted((ROOT / 'build/game-ps5/_deps').glob('*-src')):
            if not source.is_dir() or source.name == 'abseil-cpp-src':
                continue
            for file in source_files(source):
                tar.add(file, prefix + '/fetched/' + source.name + '/' + file.relative_to(source).as_posix(), recursive=False)
            records.append({'name':source.name, 'source':'Pinned FetchContent source snapshot; versions/hashes are in the included CMake files'})
        # RADV's bundled zlib source differs from the title host-tool zlib.
        for folder, name in [(args.vulkan_dir / '.deps/work/radv-src/subprojects/zlib-1.3.1', 'radv-zlib'),
                             (args.vulkan_dir / '.deps/native/zlib/zlib-1.3.2', 'host-tool-zlib')]:
            if not folder.is_dir():
                raise ValueError(f'Missing corresponding source: {name}')
            for file in source_files(folder):
                tar.add(file, prefix + '/fetched/' + name + '/' + file.relative_to(folder).as_posix(), recursive=False)
        payload = (json.dumps(records, indent=2) + '\n').encode()
        info = tarfile.TarInfo(prefix + '/SOURCES.json');info.size = len(payload)
        tar.addfile(info, io.BytesIO(payload))
    llvm = ROOT / '.deps/downloads/llvm-project-18.1.8.src.tar.xz'
    if not llvm.is_file():
        raise ValueError('Download the LLVM 18.1.8 source release before packaging')
    with llvm.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != PINS['llvm-runtime']['sha256']:
            raise ValueError('LLVM source checksum mismatch')
    shutil.copy2(llvm, out / llvm.name)
    (out / 'SOURCES.json').write_text(json.dumps(records + [PINS['llvm-runtime']], indent=2) + '\n')
    sums = []
    for file in sorted(out.iterdir()):
        if file.is_file() and file.name != 'SHA256SUMS':
            with file.open('rb') as stream:
                sums.append(f'{hashlib.file_digest(stream, "sha256").hexdigest()}  {file.name}\n')
    (out / 'SHA256SUMS').write_text(''.join(sums))
    print(archive)


if __name__ == '__main__':
    main()
