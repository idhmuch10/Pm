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

## Debugging

* All `fprintf(stderr, ...)`/`SPDLOG` output from the port goes to logcat under the
  tag `PaperShip`.
* A crash writes `papership_crash.log` into the data directory and logs
  `[CRASH] SIGSEGV received` to logcat. Use `ndk-stack` with the unstripped
  library from `app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/libPaperShip.so`
  to symbolise `adb logcat` tombstones.
* The libultraship ImGui console and stats window are reachable from the
  *Debug* tab of the settings menu.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| "Could not find CMake 3.24.0+" | Install `cmake;3.30.5` through `sdkmanager` or put CMake ≥ 3.24 on `PATH` |
| Gradle cannot resolve the Android plugin | Needs network access to Google Maven on the first build |
| FetchContent errors while configuring | libultraship clones SDL2, ImGui, spdlog, libzip … from GitHub; check proxies/firewalls |
| App closes immediately | Device lacks OpenGL ES 3.0 or is 32-bit only (`armeabi-v7a` is not built yet) |
| "not Paper Mario (USA)" when importing | Only the US ROM (SHA-1 `3837f44c…`) is supported; JP/PAL/iQue are not |
| Black screen, no audio | Check `adb logcat -s PaperShip` for `ROM file not found` or GL errors |
