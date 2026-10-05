# Building the PS5 port

## Build

```sh
python3 ps5/tools/build.py --setup --build-radv
python3 ps5/tools/validate.py
```

`setup-deps.py` fetches pinned forks, installs payload SDK v0.42 at the recorded
base revision, overlays the tested platform library/headers, builds native link
tools, and checks the clean-room runtime. `--build-radv` builds the pinned Mesa
revision when its archive is absent. The full bootstrap requires the host Mesa
shader-tool dependencies documented by the pinned PS5_Vulkan repository.

A previously prepared Vulkan tree avoids rebuilding RADV:

```sh
python3 ps5/tools/build.py --setup --vulkan-dir /path/to/PS5_Vulkan
```

The scripts preserve modified dependency trees and refuse to change their revision.
`build.py` builds native SDL3, Dawn and Dusklight, then invokes the native linker.
It produces `build/native-game/eboot.bin`; it does not contact the console.
`validate.py --build` also rebuilds before testing. Its default checks include
synthetic disc tests with ASan/UBSan, source hygiene, JSON/shell/Python syntax,
runtime/driver hashes and native SELF inspection.

The full script passed using prepared dependencies. Fetching missing pinned sources
also passed. A complete empty-machine SDK/RADV bootstrap is not yet verified.

## Package

Commit the source and keep the application tree clean before packaging:

```sh
python3 ps5/tools/package-release.py --version ps5-v0.1.0
python3 ps5/tools/validate.py --title dist/ps5-v0.1.0/PPSA99640
python3 ps5/tools/package-source.py --version ps5-v0.1.0
```

The ZIPs and corresponding source are written beneath `dist/releases/`.
The source packager requires the LLVM 18.1.8 source tarball beneath `.deps/downloads/`,
as recorded in `dependencies.json`. It archives the application, pinned forks,
Dawn's actual fetched source revisions and CMake dependency snapshots, with hashes.
Release validation rejects private data and unexpected runtime modules.
Use a new staging directory/version rather than overwriting an existing package.

`default-config.json` is the initial console preset. The installer preserves an
existing `user/config.json`, so updating does not reset player settings or saves.

## Optional textures

Get the public PC pack and controller addon from the author's official page:
https://www.henrikomagnifico.com/zelda-twilight-princess-4k

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow
.venv/bin/python ps5/tools/prepare-textures.py \
  --pack /path/to/ZTP-4K-4.0d-PC.zip \
  --buttons '/path/to/PlayStation-addon.zip' \
  --out /path/to/new-prepared-folder
```

Only texture files are extracted. The helper validates archive paths and sizes,
converts unaligned compressed base images to PNG, and overlays the normal
PlayStation button layout. The PC addon uses BC7 and needs no extra decoder.
For the mobile ASTC addon, supply `--astcenc` pointing to a host `astcenc` or
`astcdec` built from the pinned ARM astc-encoder source. It is a host tool;
it is never loaded on the console.

Upload the prepared folder beneath `PPSA99640/user/texture_replacements/` while
the title is closed. Converted textures and manifests stay outside Git and the
release bundle. Link to the author instead of redistributing the pack.
