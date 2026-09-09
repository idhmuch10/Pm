# PaperShip Mobile — Paper Mario 64 on your phone

An Android port of Paper Mario (Nintendo 64), built from the
[Paper Mario decompilation](https://github.com/pmret/papermario) and the
[PaperShip](https://github.com/versacepapermario/papermario-pc-upload) PC port,
rendered with [libultraship](https://github.com/Kenix3/libultraship).

The game code, PaperShip's PORT layer and libultraship are compiled with the
Android NDK into `libPaperShip.so`, which runs inside an
[SDL2](https://libsdl.org) activity with an on-screen N64 controller. Everything
is read at runtime from a ROM that you import once through the system file
picker.

> **Status:** early but playable. On a Galaxy Z Fold 7 the game boots, plays
> through the intro and the first Bowser fight with touch controls, and saves.
> Two Android-only bugs have been fixed since the first device reports: the
> port loaded effect graphics and the logos from the wrong place in the ROM,
> and a macOS-specific "corrupt pointer" heuristic dropped every effect
> instance (no damage numbers, no battle effects) and then made the game abort
> when Bowser powers up with the Star Rod. See `docs/PORTING_NOTES.md`. The
> build logs every session and, after a crash, offers the report on the next
> launch (Copy / Save to Downloads / Share) — please send it if anything still
> crashes. The settings menu has a Testing tab with quick save/load and a warp
> straight to that fight, plus "Save report now" and a live collision readout
> for bugs that do not crash (currently under investigation: doors that can be
> walked through, the partner sinking into the ground, Mario invisible in the
> Star Spirits' revival scene).

## Legal notice

This repository contains **no copyrighted Nintendo assets**: no ROM data, no
textures, no audio, no models. All game data is read at runtime from a ROM
file that you provide. You must own a legally obtained copy of Paper Mario
(USA). We do not condone piracy.

Only the **US release** is supported:

| Version | SHA-1 |
|---------|-------|
| Paper Mario (USA) | `3837f44cda784b466c9a2d99df70d77c322b97a0` |

## Playing on Android

1. Install the APK (build it yourself, see below, or download the `PaperShipMobile-debug`
   artifact from the *Android APK* workflow on the Actions tab).
2. Launch **PaperShip Mobile**. On first start it asks for your ROM: pick your
   `.z64`, `.v64` or `.n64` file (40 MB). It is converted to `.z64` byte order,
   verified to be Paper Mario (USA) and copied into the app's private folder.
3. Play. The on-screen pad appears automatically unless a gamepad is connected.

Requirements: Android 7.0+ (API 24), a 64-bit ARM device (`arm64-v8a`) and
OpenGL ES 3.0. Bluetooth/USB gamepads are supported through SDL.

### Controls

| On screen | N64 |
|-----------|-----|
| Left translucent stick | Control Stick |
| Blue **A**, green **B** | A, B |
| Grey **Z** | Z |
| Yellow **C▲ C▼ C◀ C▶** | C buttons |
| **L**, **R** (top corners) | L, R |
| Red **START** (bottom centre) | Start |
| Small **▲▼◀▶** (top left) | D-pad |
| **MENU** pill / Android back button | Settings menu (graphics, audio, controller mapping) |
| **PAD** pill | Hide / show the touch controls |

### Where your files live

`Android/data/com.papership.mobile/files/` (the app's private external storage):

| File | Purpose |
|------|---------|
| `Paper Mario (USA).z64` | Your imported ROM |
| `papership_save.bin` | Save data (N64 flash image) |
| `papership.cfg.json` | Settings (libultraship configuration) |
| `papership.o2r`, `gamecontrollerdb.txt` | Copied from the APK on every launch |
| `mods/` | Drop `.o2r` mod archives here |
| `papership_crash.log` | Written if the game crashes |

## Building

### Android

Requirements: JDK 17+, Android SDK (platform 35, build-tools 35), NDK
`27.2.12479018`, CMake ≥ 3.24, git, and an internet connection for the first
build (libultraship fetches SDL2 and its other dependencies).

```bash
git clone https://github.com/idhmuch10/Pm.git
cd Pm/android
./gradlew assembleDebug            # -> app/build/outputs/apk/debug/app-debug.apk
adb install app/build/outputs/apk/debug/app-debug.apk
```

Or open the `android/` folder in Android Studio. See
[docs/ANDROID.md](docs/ANDROID.md) for details, debugging tips and the emulator
(`-PabiFilters=arm64-v8a,x86_64`).

### Desktop (Linux / macOS / Windows)

The desktop build is PaperShip's; it still works from this tree:

```bash
sudo apt install cmake build-essential ninja-build libsdl2-dev libpng-dev libglew-dev \
     libzip-dev nlohmann-json3-dev libtinyxml2-dev libspdlog-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PaperShip
python3 tools/port/pack_o2r.py assets/port build/papership.o2r   # or: cmake --build build --target GeneratePortO2R
cp "Paper Mario (USA).z64" build/ && cd build && ./PaperShip
```

`tools/port/prepare_us_rom.py` verifies a ROM (or a zip containing one), converts
`.v64`/`.n64` dumps and checks the SHA-1.

## Repository layout

```
android/            Gradle project: SDL2 Java glue, MainActivity, touch overlay, ROM import
port/               PaperShip PORT layer (N64 OS/NuSystem shims, ROM loading, audio mixer, menu)
port/android/       Android-only glue: JNI bridge, logcat redirect, touch-input merge
src/, include/      Paper Mario decompilation with #ifdef PORT adaptations
libultraship/       Rendering/audio/input engine (Fast3D interpreter, OpenGL ES on Android)
Torch/              Asset pipeline tool (desktop only)
assets/port/        Shaders packed into papership.o2r
tools/port/         ROM verification and archive packing scripts
docs/               Android build guide and porting notes
CMakeLists.txt      Builds the game: executable on desktop, libPaperShip.so on Android
```

## Roadmap

See [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md) for what was done, what is
known to be missing and the suggested next steps (device testing, touch
control polish, rumble, 32-bit ARM, iOS).

## Licensing

The decompiled game code and PaperShip's port layer declare no license (the
game code is Nintendo's). Third-party components keep their own licenses:
libultraship and Torch are MIT, SDL is zlib. The Android integration added in
this repository (`android/app/src/main/java/com/papership/mobile`,
`port/android`, `tools/port`) is provided as-is for educational and
interoperability purposes.

## Credits

- **versacepapermario** and the PaperShip contributors — the PC port this is based on
- [Paper Mario decompilation team](https://github.com/pmret/papermario) — the complete decomp
- [Kenix3 / HarbourMasters](https://github.com/Kenix3/libultraship) — libultraship
- [Waterdish](https://github.com/Waterdish) — Android ports of Ship of Harkinian and 2Ship2Harkinian, the reference for how libultraship games run on Android
- [SDL](https://libsdl.org) — window, input and audio on every platform
