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
  Share dialog as after a crash, with a **diagnostics** block followed by the log
  of the *running* session. The diagnostics block is written straight to
  `papership_report.txt` by the game thread and closed before the dialog opens;
  the log itself travels through a pipe and a pump thread, so without that file
  its last lines can still be in flight when the dialog reads it (an earlier
  report lost the first third of a collider dump this way). The dump also names
  every nearby collider on one line and ends with a `dump complete (N of M)`
  line, so a lost line is visible rather than looking like missing collision.
  Open the menu right after the problem happened (during a cutscene is fine)
  and save or share the report. The report also carries the live status block
  and, in the world, a dump of the colliders around Mario (names, flags,
  bounding boxes, triangles), the entities and bound triggers, wall probes in
  eight directions, the result of the game's own movement test
  (`player_test_move_with_slipping`) in eight directions, and a `[models]`
  list of every map model that currently carries a script transform (doors,
  gates, the bushes that slide apart in the revival scene) with its flags,
  matrix state and translation. Saving a report during a cutscene where a
  prop or a character looks wrong is the way to get that data.
* **Live status** shows the player position, action state and flags, the
  collision ids the game currently sees (floor, wall, inspect target, pushing),
  the collider count of the map and the partner's position/floor. When Mario
  walks through a door, open the menu while pushing against it: `wall` should
  name the door's collider; `-1` means the wall test did not hit it at all.

Two collision checks also run on their own and only need the log:

* every map load ends with a `[collision] self-check:` line — each wall and
  floor triangle of the map is ray-tested against itself with the game's own
  `test_ray_colliders`, so `walls N hit / 0 other / N total` means the ray test
  works on this device for this map's data, and a `MISS` line names a
  triangle it does not;
* every frame the port compares the map's collider bounding boxes with a copy
  taken when the map loaded. Nothing in the game writes them outside the loader,
  so `[collision] BOUNDING BOXES OVERWRITTEN` means something else wrote over the
  start of the collision heap; the line says how many words changed, which
  collider they belong to, the frame number and the old and new bytes;
* whenever Mario's movement in one frame passes through a solid wall triangle
  the port logs `[collision] PLAYER CROSSED WALL #id (name)` (or, when the wall
  is one-sided and he came through its back, `FROM BEHIND (not solid from this
  side)`, which is how the original game behaves) with the move,
  the player state and yaw values, and then repeats the wall ray from the
  previous position at the three heights the game uses, the entity ray and
  the movement test. Walk into a door and save a report: those lines say
  whether the ray test misses the door (data or code) or hits it while the
  game ignored the result (flow, e.g. an entity or a script state).

One caveat when using the warp shortcuts for this: the game's walls are mostly
one-sided, and a map's scripts only make the gates and doors solid from the
side the story lets you reach. Warping to an entry the current story progress
would never take you to (Goomba Village entry 0 with a fresh save, for
example) can spawn Mario behind a closed gate, and walking out of it from
behind is the original game's behaviour, not a port bug. The status block now
prints the story progress, partner and load type so a report shows which case
it is; the watchdog line names the wall and prints its bounding box.

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
  ROM offset, and that a known entity's ROM segment still has a real size in that
  table (the bounds are 1-byte placeholders here, so code that sizes a segment by
  subtracting them gets 1; that is what wrote entity data over the map's
  collision, see `docs/PORTING_NOTES.md`) (this is the check that would have caught the invisible damage
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

`tools/port/collision_harness/run.sh` is a second ROM-free check aimed at the
door/wall bug: it builds a synthetic hit file, loads it through the real
`load_hit_data`, and runs the real ray tests, trig helpers, the port's collision
diagnostics and a copy of the player movement code, comparing the output with
`expected.txt`. `run.sh --host` uses the host compiler; `run.sh --android`
builds the same sources with the NDK as a static aarch64 Bionic executable and
runs it under `qemu-aarch64` (`apt-get install qemu-user`), which is how the
collision code was shown to behave identically on ARM64 and x86-64. CI runs
both (the `build` job the Android one, `selftest` the host one).

A crash report also lists the script API functions the interpreter called most
recently, newest first. A script calls those through a pointer, so a crash inside one
shows up in the backtrace only as `evt_execute_next_command`; that list names it.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| "Could not find CMake 3.24.0+" | Install `cmake;3.30.5` through `sdkmanager` or put CMake ≥ 3.24 on `PATH` |
| Gradle cannot resolve the Android plugin | Needs network access to Google Maven on the first build |
| FetchContent errors while configuring | libultraship clones SDL2, ImGui, spdlog, libzip … from GitHub; check proxies/firewalls |
| App closes immediately | Device lacks OpenGL ES 3.0 or is 32-bit only (`armeabi-v7a` is not built yet) |
| "not Paper Mario (USA)" when importing | Only the US ROM (SHA-1 `3837f44c…`) is supported; JP/PAL/iQue are not |
| Black screen, no audio | Check `adb logcat -s PaperShip` for `ROM file not found` or GL errors |
