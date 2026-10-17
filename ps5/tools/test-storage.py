#!/usr/bin/env python3
"""Run synthetic PS5 disc selection and update-protection tests on a Linux host."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    executable = work / 'storage-test'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', '-I', str(root / 'ps5'), str(root / 'ps5/storage.cpp'),
        str(root / 'ps5/disc/storage_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(work / 'fixtures')], check=True)
