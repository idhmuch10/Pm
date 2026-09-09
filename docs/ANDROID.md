# Building and running PaperShip Mobile on Android

## How the Android build works

```
android/app (Gradle)  ──externalNativeBuild──▶  ../../CMakeLists.txt (NDK, arm64-v8a)
        │                                              │
        │  packs assets/port -> papership.o2r          ├─ libultraship (OpenGL ES 3, SDL2 fetched from source)
        │  downloads gamecontrollerdb.txt              ├─ Paper Mario decomp + port/ (PaperShip PORT layer)
        │                                              └─ port/android (JNI bridge, logcat, touch input)
        ▼                                                          │
   APK: org.libsdl.app.* + com.papership.mobile.*  +  libSDL2.so + libPaperShip.so
```

* `MainActivity` extends SDL's `SDLActivity`. SDL creates the GL surface and
  starts the game thread, which runs `SDL_main` in `port/Game.cpp`.
* The native side blocks in `port_android_wait_for_setup()` until the activity
  has copied `papership.o2r` and `gamecontrollerdb.txt` into the app data
  directory and a valid ROM exists there (`nativeSetupDone`).
* `TouchControlsView` draws the on-screen pad and sends button/stick state
  through JNI; `port/android/AndroidPort.cpp` merges it into the N64 pad in
  `nuContDataGet()` (`port/NuSystemShims.cpp`).
* All file paths go through the app's private external files directory
  (`SDL_AndroidGetExternalStoragePath()`), so no storage permission is needed.

## Prerequisites

| Tool | Version |
|------|---------|
| JDK | 17 or newer |
| Android SDK | platform 35, build-tools 35.0.0 |
| Android NDK | 27.2.12479018 (r27c) |
| CMake | ≥ 3.24 (install `cmake;3.30.5` in the SDK, or have a recent `cmake` on `PATH`) |
| git | any (libultraship's CMake fetches dependencies with git) |

With the command-line tools:

```bash
sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0" \
           "ndk;27.2.12479018" "cmake;3.30.5"
export ANDROID_HOME=/path/to/sdk
```

## Building

```bash
cd android
./gradlew assembleDebug                      # debug APK (optimised native code, RelWithDebInfo)
./gradlew assembleRelease                    # release APK signed with the debug key
./gradlew assembleDebug -PabiFilters=arm64-v8a,x86_64   # also build for the x86_64 emulator
```

Outputs: `android/app/build/outputs/apk/<variant>/app-<variant>.apk`.
The first build compiles ~1,800 C files plus libultraship and SDL2 and takes a
while; later builds are incremental.

To ship a signed release, add a `signingConfigs` block to `app/build.gradle`
(or sign the APK with `apksigner`); the release build type currently reuses the
debug key so that it can be installed for testing.

### Android Studio

Open the `android/` directory. Set the NDK/CMake versions in *SDK Manager* if
the sync complains. The native sources are visible under `app/cpp` after the
first sync.

## Installing and first launch

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb logcat -s PaperShip SDL SDL/APP
```

On first launch the app asks for the ROM. Pick your Paper Mario (USA) dump
(`.z64`, `.v64` or `.n64`, 40 MB). The importer converts it to big-endian,
rejects anything that is not the US cartridge (game code `NMQE`) and stores it
as `Android/data/com.papership.mobile/files/Paper Mario (USA).z64`.

You can also push a ROM directly:

```bash
adb push "Paper Mario (USA).z64" "/sdcard/Android/data/com.papership.mobile/files/"
```

## Controls and settings

* The touch overlay can be hidden with the **PAD** pill; the preference is remembered.
  It is hidden by default when a gamepad is connected at launch.
* **MENU** (or the back button/gesture) opens PaperShip's settings: internal
  resolution, MSAA, texture filtering, volume and the libultraship controller
  mapping editor. While the menu is open the overlay disappears and touches go
  to the menu.
* Gamepads use SDL's game controller mappings (`gamecontrollerdb.txt`).

## Testing shortcuts

The settings menu (MENU pill or back button) has a **Testing** tab:

* **Quick save / Quick load**: writes the active save slot with the current
  position anywhere in a map (not only at save blocks) and re-enters the world
  from it, the same way the file menu does. Use it right before a scene you want
  to retry.
* **Presets**: *Intro: Bowser confrontation* sets the story progress back to the
  intro and warps to the castle room where the fight starts; *Goomba Village*
  warps to `kmr_02`.
* **Warp**: any area/map/entry from the game's map table, plus a story-progress
  editor. Maps depend on flags set by earlier scenes, so an arbitrary warp can
  show an inconsistent state; it is a test tool, not a cheat menu.

A real "save state" (full memory snapshot) is not feasible in a decomp port: the
game state is spread over static memory, malloc'd buffers and the renderer.

The **Diagnostics** part of the same tab has two tools for reporting bugs that
do not crash the game:

* **Save report (current log) now** opens the same Copy / Save to Downloads /
  Share dialog as after a crash, but with the log of the *running* session.
  Open the menu right after the problem happened (during a cutscene is fine)
  and save or share the report. The report also carries the live status block
  and, in the world, a dump of the colliders around Mario (names, flags,
  bounding boxes, first triangles) plus wall probes in eight directions.
* **Live status** shows the player position, action state and flags, the
  collision ids the game currently sees (floor, wall, inspect target, pushing),
  the collider count of the map and the partner's position/floor. When Mario
  walks through a door, open the menu while pushing against it: `wall` should
  name the door's collider; `-1` means the wall test did not hit it at all.

## Debugging and reporting crashes

* All `fprintf(stderr, ...)`/`SPDLOG` output from the port goes to logcat under the
  tag `PaperShip`.
* Every session's log is written continuously to `papership_log.txt` in the data
  directory (previous run: `papership_log_prev.txt`); it contains one line per
  compiled shader variant and the full source of any shader the GPU rejects.
* A crash writes `papership_crash.log` (signal, native backtrace with function
  names, the last 250 log lines) from an alternate signal stack without calling
  into Java, then hands the signal back to Android so a full tombstone appears in
  `adb logcat -s DEBUG`.
* On the next launch the app detects an unclean exit and offers the report with
  three buttons: **Copy** (clipboard, paste it into a bug report), **Save to
  Downloads** (`Download/papership_report_<date>.txt`, visible in the Files app)
  and **Share**.
* Without a phone connected: the crash report is enough to locate the crash.
  With `adb`: `adb logcat -s PaperShip DEBUG` shows the game log and the
  tombstone; symbolise with `ndk-stack -sym <dir containing the unstripped
  libPaperShip.so>` (CI uploads it as the `PaperShipMobile-debug-symbols`
  artifact; locally it is under `app/build/intermediates/merged_native_libs/`).
* The libultraship ImGui console and stats window are reachable from the
  *Debug* tab of the settings menu.

## Self-test without a phone

`PAPERSHIP_SELFTEST=1` makes the desktop build verify itself instead of booting
the game, and needs no ROM:

* `port/rom_offsets.c` checks that every N64 linker symbol resolves to its own
  ROM offset (this is the check that would have caught the invisible damage
  numbers: zero-length stub symbols shared addresses on Android/Linux, see
  `docs/PORTING_NOTES.md`);
* `port/ShaderSelfTest.cpp` renders every combiner mode the game uses (136
  pairs, generated into `port/shader_selftest_cases.inc` by
  `tools/port/gen_shader_selftest.py`; re-run it after adding combine modes)
  under a matrix of render/alpha/fog/cycle modes and counts shader compile or
  link failures. On a `-DUSE_OPENGLES=ON` Linux build this exercises the same
  GLSL ES 3.00 shader path as Android.

```sh
cmake -S . -B build-gles -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OPENGLES=ON
ninja -C build-gles PaperShip
cp papership.o2r gamecontrollerdb.txt build-gles/     # port assets next to the binary
cd build-gles && PAPERSHIP_SELFTEST=1 SDL_AUDIODRIVER=dummy \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ./PaperShip   # exit code 0 = passed
```

The same run is part of CI (`selftest-linux` job). Every normal boot also
prints one `[rom_offsets]` summary line to the session log.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| "Could not find CMake 3.24.0+" | Install `cmake;3.30.5` through `sdkmanager` or put CMake ≥ 3.24 on `PATH` |
| Gradle cannot resolve the Android plugin | Needs network access to Google Maven on the first build |
| FetchContent errors while configuring | libultraship clones SDL2, ImGui, spdlog, libzip … from GitHub; check proxies/firewalls |
| App closes immediately | Device lacks OpenGL ES 3.0 or is 32-bit only (`armeabi-v7a` is not built yet) |
| "not Paper Mario (USA)" when importing | Only the US ROM (SHA-1 `3837f44c…`) is supported; JP/PAL/iQue are not |
| Black screen, no audio | Check `adb logcat -s PaperShip` for `ROM file not found` or GL errors |
