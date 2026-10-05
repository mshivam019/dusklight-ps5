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

## Controls and settings menu

Press **Touchpad** to open the Dusklight settings menu during gameplay.
This is where you configure the built-in graphics, gameplay and cheat options.

| Control | Action |
| --- | --- |
| Touchpad | Open/toggle Dusklight settings |
| D-pad or left stick | Navigate menu options |
| Cross | Select or confirm |
| Circle | Back or cancel |
| L2 / R2 | Previous / next settings tab |
| Options | Original game's Start menu |
| Left / right stick during gameplay | Movement / camera |

Face buttons use Cross=A, Circle=B, Square=X and Triangle=Y.
Button bindings can be adjusted in the **Input** tab.

## Optional texture packs

Download the PC pack and PlayStation UI addon from
[Henriko Magnifico's official page](https://www.henrikomagnifico.com/zelda-twilight-princess-4k).
Then use `ps5/tools/prepare-textures.py` to extract textures and prepare the prompts.
It handles PC BC7 files directly and can convert mobile ASTC files with a host decoder.
The textures stay separate from the release. See [build and texture instructions](ps5/BUILD.md).

### Installing or changing a texture pack

Close the game before copying files. Install Pillow on your computer, then prepare
one pack in a new output folder:

```sh
python3 -m pip install Pillow
python3 ps5/tools/prepare-textures.py --pack /path/to/pack.zip --buttons /path/to/playstation-addon.zip --out /path/to/prepared-pack
```

The button addon is optional; omit `--buttons` if you do not need it.
Copy the prepared folder into the installed title's
`PPSA99640/user/texture_replacements/`, preserving the texture filenames and folders.
For example: `PPSA99640/user/texture_replacements/MyPack/tex1_....dds`.
Use the original `tex1_...` filenames; renaming them prevents matching.
The PC pack/addon is the tested preparation route. Mobile ASTC files need
`--astcenc /path/to/host-decoder`.

Launch the game, press **Touchpad**, open **Video**, and change
**Enable Texture Replacements** to turn the installed HD textures on or off.
To replace a pack, close the game and move the old pack folder out of
`texture_replacements` before copying the new one. Avoid overlapping packs that
replace the same textures. Preserve the rest of `user/`, especially your saves.

Downloadable native **code mods are disabled in this PS5 release**. Dropping PC
mod libraries or mod ZIPs into `user/mods` does not enable them. The built-in
settings and cheats remain available through the Touchpad menu.

## Languages

The tested USA GameCube disc (GZ2E01 revision 0) provides **English only**.
Changing the console language does not supply additional game text.
Upstream Dusklight selects languages from the supplied disc's assets:

| Disc region | Game text available upstream |
| --- | --- |
| USA GameCube | English |
| European GameCube | English, German, French, Spanish, Italian |
| Japanese GameCube | Japanese |

European and Japanese discs have not been verified in this PS5 release.
Upstream exposes the language selector in its pre-launch settings when the disc
contains multiple languages. This release skips that screen by default.
The Dusklight settings interface is currently in English; changing game text
language does not translate it. Translation patches are not included.

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

- [Twilit Realm](https://github.com/TwilitRealm) and the [Dusklight contributors](https://github.com/TwilitRealm/dusklight): the original PC port and engine.
- [zeldaret](https://github.com/zeldaret): the [Twilight Princess decompilation](https://github.com/zeldaret/tp).
- [Luke Street / encounter](https://github.com/encounter): [Aurora](https://github.com/encounter/aurora) and [Borealis](https://github.com/encounter/borealis).
- [John Törnblom](https://github.com/john-tornblom) and [ps5-payload-dev](https://github.com/ps5-payload-dev): the [PS5 homebrew SDK](https://github.com/ps5-payload-dev/sdk).
- [Mihawk](https://github.com/mihawk-99): [PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa) and [PS5 PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK).
- [BlackBearReloaded](https://github.com/blackbearreloaded): the [native app boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) and [PS5 OpenGL work](https://github.com/blackbearreloaded/ps5-opengl) underlying parts of the native runtime and graphics tooling.
- The [SDL](https://github.com/libsdl-org/SDL), [Dawn](https://dawn.googlesource.com/dawn), [Mesa](https://gitlab.freedesktop.org/mesa/mesa) and [LLVM](https://github.com/llvm/llvm-project) teams, plus the other authors listed in the packaged notices and corresponding source.
- [Henriko Magnifico](https://www.henrikomagnifico.com/zelda-twilight-princess-4k) and contributors: the optional HD texture pack and PlayStation controller addon, downloaded separately.

Upstream Dusklight is CC0. The linked PS5 platform/runtime is GPL-3.0-or-later;
other components retain their own licenses. Corresponding source accompanies releases.
This is an independent port, not an official Twilit Realm release.
Not affiliated with Nintendo or Sony Interactive Entertainment. PlayStation and PS5
are trademarks of Sony Interactive Entertainment. Vulkan is a Khronos Group trademark;
this RADV port is not a conformant Vulkan product.
