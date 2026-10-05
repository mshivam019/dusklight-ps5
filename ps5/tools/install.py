#!/usr/bin/env python3
"""Upload a native title over FTP, preserving existing configuration and saves."""
import argparse
import ftplib
from pathlib import Path

TITLE = 'PPSA99640'


def directory(ftp, path):
    current = ''
    for part in path.split('/'):
        if not part:
            continue
        current += '/' + part
        try:
            ftp.mkd(current)
        except ftplib.error_perm:
            ftp.cwd(current)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--src', type=Path, required=True)
    parser.add_argument('--console', required=True)
    parser.add_argument('--ftp-port', type=int, default=2121)
    parser.add_argument('--install-root', default='/mnt/ext1/etaHEN/games')
    args = parser.parse_args()
    if not (args.src / 'eboot.bin').is_file():
        parser.error('--src must be the extracted PPSA99640 folder')
    print('Close the title before uploading. This helper does not register or launch it.')
    base = args.install_root.rstrip('/') + '/' + TITLE
    with ftplib.FTP() as ftp:
        ftp.connect(args.console, args.ftp_port, timeout=60)
        ftp.login()
        ftp.voidcmd('TYPE I')
        for source in sorted(args.src.rglob('*'), key=lambda p: (p.name == 'eboot.bin', str(p))):
            if source.is_symlink():
                raise ValueError(f'Symbolic link in title: {source}')
            if not source.is_file():
                continue
            relative = source.relative_to(args.src).as_posix()
            if relative.startswith('user/') and relative != 'user/config.json':
                raise ValueError('The release installer must not upload saves or texture packs')
            target = base + '/' + relative
            directory(ftp, target.rsplit('/', 1)[0])
            if relative == 'user/config.json':
                try:
                    ftp.size(target)
                    print('Preserved existing user/config.json')
                    continue
                except ftplib.error_perm as error:
                    if not str(error).startswith('550'):
                        raise
            with source.open('rb') as stream:
                ftp.storbinary('STOR ' + target + '.next', stream, 262144)
            if ftp.size(target + '.next') != source.stat().st_size:
                raise RuntimeError(f'Transfer size mismatch: {relative}')
            ftp.rename(target + '.next', target)
            print('Uploaded', relative)
    print('Register/mount', base, 'with your native-title launcher or PS5 Upload.')


if __name__ == '__main__':
    main()
