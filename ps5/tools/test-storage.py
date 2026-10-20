#!/usr/bin/env python3
"""Run synthetic PS5 disc selection and update-protection tests on a Linux host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='dusklight-storage-') as directory, \
        tempfile.TemporaryDirectory(prefix='dusklight-storage-cross-',dir='/dev/shm') as cross_directory:
    work = Path(directory)
    assert os.stat(directory).st_dev != os.stat(cross_directory).st_dev, 'Need two fixture filesystems'
    executable = work / 'storage-test'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', '-I', str(root / 'ps5'), str(root / 'ps5/storage.cpp'),
        str(root / 'ps5/disc/storage_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(work / 'fixtures'),cross_directory], check=True)
