# Dusklight for PS5

A native PS5 port of [Dusklight](https://github.com/TwilitRealm/dusklight), based on v2.0.3.
The tested build reaches playable gameplay with working controller input and saves.

## Download and install

Get the Windows or Linux ZIP from [Releases](https://github.com/mshivam019/dusklight-ps5/releases).
The console title is the same in both; only the upload helper differs.

No game dump, HD texture pack, saves, console firmware or keys are included.
Supply a dump of your own legally obtained game. The initial PS5 build is tested
with the USA GameCube disc, GZ2E01 revision 0, as a raw `game.iso`.

Extract the ZIP, add `game.iso` beside `eboot.bin` in `output/PPSA99640`, and upload
that folder to your native-title location. Register it with your normal launcher
or PS5 Upload. See [installation instructions](ps5/INSTALL.txt).
Close the title before updates and preserve `user/`, which contains saves and settings.

## Features and testing

- Native Vulkan/RADV rendering through Dawn, stereo audio and PS5 controller input.
- 3840x2160 output at about 59.94 FPS in the measured scenes, using frame interpolation.
- Character movement and save creation confirmed in console testing on firmware 9.00.
- Touchpad opens settings, with 200% menu scale for the tested 4K display.
- Cross=A, Circle=B, Square=X, Triangle=Y and Options=Start.
- Quiet launch: FPS counter, controller-connected toast and shader-compilation overlay disabled.
- Optional HD textures and PlayStation prompts, prepared from your own downloads.

The original simulation remains at its original rate. Whole-game progression,
all regions, gyro, code mods and 120 Hz output are not verified. Some compressed
texture-thumbnail warnings remain; the tested full-size HD visuals look correct.
See [tested status](ps5/CURRENT_STATUS.txt) for the current limits.

## Optional texture packs

Download the PC pack and PlayStation UI addon from
[Henriko Magnifico's official page](https://www.henrikomagnifico.com/zelda-twilight-princess-4k).
Then use `ps5/tools/prepare-textures.py` to extract textures and prepare the prompts.
It handles PC BC7 files directly and can convert mobile ASTC files with a host decoder.
The textures stay separate from the release. See [build and texture instructions](ps5/BUILD.md).

## Build and validate

A Linux build host needs Python 3, Git, CMake, Ninja, Clang/LLVM, Make, Meson,
rsync, wget and unzip. Rebuilding RADV also needs the host Mesa shader-tool dependencies.
The repository is currently private, so dependency fetches require GitHub access.
After it becomes public, the same pinned URLs can be fetched without that access.

```sh
python3 ps5/tools/build.py --setup --build-radv
python3 ps5/tools/validate.py
python3 ps5/tools/package-release.py --version ps5-v0.1.0
python3 ps5/tools/package-source.py --version ps5-v0.1.0
```

A prepared Vulkan tree can be supplied with `--vulkan-dir /path/to/PS5_Vulkan`.
Dependency pins are in [ps5/dependencies.json](ps5/dependencies.json).
The full SDL3/Dawn/game build script has passed using the prepared dependencies;
a complete empty-machine RADV bootstrap has not yet been verified.

## Credits and licensing

Thanks to the Twilit Realm/Dusklight team, zeldaret's Twilight Princess decompilation
contributors, Luke Street and the Aurora/Borealis contributors, John Törnblom and
ps5-payload-dev, Mihawk for the PS5 Vulkan/Mesa work, and BlackBearReloaded for the
native runtime foundation. SDL, Dawn, Mesa, LLVM and the other dependency authors
are credited in the packaged notices and corresponding source.
Henriko Magnifico and contributors made the optional texture pack and controller addon.

Upstream Dusklight is CC0. The linked PS5 platform/runtime is GPL-3.0-or-later;
other components retain their own licenses. Corresponding source accompanies releases.
This is an independent port, not an official Twilit Realm release.
Not affiliated with Nintendo or Sony Interactive Entertainment. PlayStation and PS5
are trademarks of Sony Interactive Entertainment. Vulkan is a Khronos Group trademark;
this RADV port is not a conformant Vulkan product.
