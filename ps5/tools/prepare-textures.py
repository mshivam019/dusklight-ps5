#!/usr/bin/env python3
"""Prepare a downloaded Henriko pack and optional PlayStation prompts for PS5."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tempfile
import zipfile

PAGE = 'https://www.henrikomagnifico.com/zelda-twilight-princess-4k'
MAX_BYTES = 16 * 1024**3


def valid_texture(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError(f'Unsafe archive path: {name}')
    return path.name.startswith('tex1_') and path.suffix.lower() in {'.dds', '.png'}


def extract_pack(archive, output):
    count = 0
    with zipfile.ZipFile(archive) as z:
        if sum(i.file_size for i in z.infolist()) > MAX_BYTES:
            raise ValueError('Texture archive exceeds the 16 GiB unpacked limit')
        for entry in z.infolist():
            if not valid_texture(entry.filename):
                continue
            parts = PurePosixPath(entry.filename).parts
            if not parts[0].startswith('HenrikosTP'):
                raise ValueError('Use the PC Dusklight texture pack from the official page')
            target = output.joinpath(*parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(entry) as inp, target.open('wb') as out:
                shutil.copyfileobj(inp, out, 1024 * 1024)
            count += 1
    if not count:
        raise ValueError('No texture files found')
    return count


def convert_astc(source, destination, decoder, temporary):
    from PIL import Image
    data = source.read_bytes()
    if len(data) < 148 or data[:4] != b'DDS ' or data[84:88] != b'DX10':
        raise ValueError(f'Expected a DX10 DDS: {source.name}')
    height, width = struct.unpack_from('<II', data, 12)
    format_id = struct.unpack_from('<I', data, 128)[0]
    if format_id not in (134, 135):
        with Image.open(source) as image:
            image.save(destination)
        return
    if decoder is None or not decoder.is_file():
        raise ValueError('This addon uses ASTC. Pass --astcenc, or use the PC BC7 addon.')
    size = ((width + 3) // 4) * ((height + 3) // 4) * 16
    if width == 0 or height == 0 or width > 16384 or height > 16384 or len(data) < 148 + size:
        raise ValueError(f'Invalid ASTC dimensions/payload: {source.name}')
    astc = temporary / 'prompt.astc'
    tga = temporary / 'prompt.tga'
    astc.write_bytes(bytes.fromhex('13aba15c') + bytes([4, 4, 1]) +
        width.to_bytes(3, 'little') + height.to_bytes(3, 'little') +
        (1).to_bytes(3, 'little') + data[148:148 + size])
    subprocess.run([str(decoder), '-ds' if format_id == 135 else '-dl', str(astc), str(tga)],
                   check=True, stdout=subprocess.DEVNULL)
    with Image.open(tga) as image:
        image.save(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, required=True, help=f'PC texture-pack ZIP from {PAGE}')
    parser.add_argument('--buttons', type=Path, help='Xbox + PlayStation UI Addon ZIP from the same page')
    parser.add_argument('--astcenc', type=Path, help='Host astcenc/astcdec binary, required for the prompt addon')
    parser.add_argument('--out', type=Path, required=True, help='New texture directory, outside the release package')
    args = parser.parse_args()
    # Pillow is needed only for technical DDS/PNG conversion.
    from PIL import Image
    if args.out.exists():
        parser.error('--out already exists; choose a new directory to preserve existing textures')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(args.out.parent).free < 7 * 1024**3:
        parser.error('At least 7 GiB free space is required for the 4K pack')
    with tempfile.TemporaryDirectory(dir=args.out.parent) as temporary:
        temp = Path(temporary)
        output = temp / 'textures'
        output.mkdir()
        count = extract_pack(args.pack, output)
        print(f'Extracted {count} textures', flush=True)
        converted = 0
        for source in list(output.rglob('*.dds')):
            with source.open('rb') as stream:
                header = stream.read(148)
            height, width = struct.unpack_from('<II', header, 12)
            if width % 4 or height % 4:
                with Image.open(source) as image:
                    image.save(source.with_suffix('.png'))
                source.unlink()
                converted += 1
        prompts = 0
        if args.buttons:
            with zipfile.ZipFile(args.buttons) as z:
                if sum(i.file_size for i in z.infolist()) > 256 * 1024**2:
                    raise ValueError('Button addon exceeds the unpacked-size limit')
                for entry in z.infolist():
                    if not valid_texture(entry.filename) or '/Playstation Layout/Buttons/' not in '/' + entry.filename:
                        continue
                    source = temp / PurePosixPath(entry.filename).name
                    source.write_bytes(z.read(entry))
                    folder = output / 'UI/Menus/Buttons'
                    folder.mkdir(parents=True, exist_ok=True)
                    target = folder / source.with_suffix('.png').name
                    if source.suffix.lower() == '.dds':
                        convert_astc(source, target, args.astcenc.resolve() if args.astcenc else None, temp)
                    else:
                        shutil.copy2(source, target)
                    old = target.with_suffix('.dds')
                    if old.exists():
                        old.unlink()
                    prompts += 1
            if prompts == 0:
                raise ValueError('No normal PlayStation-layout textures found in the addon ZIP')
        manifest = {'source': PAGE, 'pack_sha256': hashlib.file_digest(args.pack.open('rb'), 'sha256').hexdigest(),
                    'extracted': count, 'unaligned_png_conversions': converted, 'playstation_prompts': prompts}
        (output / 'PACK-INFO.json').write_text(json.dumps(manifest, indent=2) + '\n')
        output.rename(args.out)
    print(f'Ready: {args.out}. Copy this folder beneath PPSA99640/user/texture_replacements/ while the game is closed.')
    print('Personal installation only. The texture pack is not included in port releases.')


if __name__ == '__main__':
    main()
