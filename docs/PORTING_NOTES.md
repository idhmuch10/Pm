# Porting notes

This file records how the phone port is put together, what was verified, and
what remains. It continues the work started in an earlier ChatGPT session,
which had rebuilt the decomp ROM byte-for-byte and estimated the port itself
at months of work; that session's sandbox (its ROM tooling and compiler
fixes) was not preserved, so the ROM helper was recreated under
`tools/port/` and the port was restarted on a much stronger foundation.

## Why this architecture

Three routes exist for running Paper Mario natively:

| Route | State | Verdict for phones |
|-------|-------|--------------------|
| Static recompilation (N64Recomp, used by *Paper Mario ReCut*) | Windows-only, RT64 renderer needs modern Vulkan/D3D12 | Heavy renderer, no mobile support upstream |
| Bare decomp + hand-written N64 abstraction (sm64-port style) | Would have to be written from scratch for Paper Mario | Months before a first frame |
| **Decomp + libultraship (PaperShip)** | Playable through Chapter 2 on desktop; libultraship already has an Android/iOS layer (SDL2 + OpenGL ES) | **Chosen**: the phone work is packaging, input and file handling |

PaperShip vendors libultraship and Torch as plain directories (its
`.gitmodules` pointed at commits that do not exist upstream), so this
repository does the same; there are no submodules to initialise.

## What was added or changed

### Build system (`CMakeLists.txt`)
* On Android the game is a `SHARED` library (`libPaperShip.so`) instead of an
  executable; Torch, the desktop OpenGL lookup and the post-build asset copy
  are skipped; `USE_OPENGLES` is forced on for libultraship; `SDL2::SDL2`,
  `GLESv3`, `EGL`, `log` and `android` are linked; shared objects are aligned
  to 16 KB pages (Android 15 requirement).
* Release flags on Android are `-O2` (PaperShip uses `-O1` on desktop) with
  `-fno-strict-aliasing -fwrapv` for the game code, because the decomp relies
  on wraparound arithmetic and type punning.
* Linux fixes that PaperShip's untested Linux branch needed with clang 18:
  `-Wno-return-type` and `-Wno-implicit-function-declaration` (decomp code
  uses `return;` in non-void functions and calls `sprintf` without headers),
  and the settings menu's *Restart Game* button no longer calls the macOS-only
  `_NSGetExecutablePath` on other platforms.

### PORT layer (`port/`)
* `NuSystemShims.cpp`: the ROM is loaded into memory once and normalised from
  `.v64`/`.n64` to `.z64` byte order, so `nuPiReadRom()` is a `memcpy`. The
  search now also covers the app data directory, `$PAPERSHIP_ROM`, and a path
  injected at runtime (`port_set_rom_path()`, used by the Android file picker).
  The internal game code is checked (`NMQE` = US).
* `port_paths.h`: `port_get_data_path()` resolves files relative to the app data
  directory (next to the executable on desktop, the app's private external
  files directory on Android). Used for the save file (`os_stubs.c`, previously
  the current working directory, which is not writable on phones) and the
  crash log (previously `/tmp`).
* `Game.cpp`: `SDL_main` entry on Android, logcat redirection, waiting for the
  Java side, per-frame menu toggle requests.
* `port/android/AndroidPort.cpp`: JNI bridge (`nativeSetupDone`,
  `nativeSetTouchButton`, `nativeSetTouchStick`, `nativeToggleMenu`,
  `nativeIsMenuVisible`), stdout/stderr → logcat pump thread, and the merge of
  the touch pad into `OSContPad[0]` (buttons OR-ed, stick overrides when moved,
  suppressed while the settings menu is open).

### Android app (`android/`)
* SDL 2.32.10's Java glue (`org.libsdl.app`, zlib license) — the same version
  libultraship fetches for the native build.
* `MainActivity`: loads `SDL2` + `PaperShip`, copies bundled files, imports the
  ROM through the Storage Access Framework (no storage permission), hosts the
  overlay, maps the back button to the menu, hides the overlay by default when a
  gamepad is present.
* `RomImporter`: byte-order normalisation, size and game-code validation,
  atomic write.
* `TouchControlsView`: multi-touch N64 layout drawn with `Canvas`, scaled from
  the screen height; owns all touches while visible; hides itself while the
  ImGui menu is open.
* Gradle packs `assets/port` into `papership.o2r` (with Torch's `portVersion`
  entry) and fetches `gamecontrollerdb.txt` at build time.
* `.github/workflows/android.yml` builds the debug APK on every push.

## Verification done in this repository

* Desktop Linux build of the tree with clang 18 (needed the flag fixes above).
* Android `assembleDebug` for `arm64-v8a` with NDK r27c: see the build status in
  the commit message / CI. Nothing has been run on a physical device yet.

## Known gaps and next steps

1. **Device testing.** Boot, frame rate, audio latency and heat on real phones.
   Start with a mid-range arm64 device and `adb logcat -s PaperShip`.
2. **Performance.** PaperShip runs the game logic at 30 fps and renders through
   the Fast3D interpreter; the internal resolution multiplier (2x default) and
   MSAA are the first knobs if a phone struggles. Frame interpolation is still a
   stub in `Engine.cpp`.
3. **Touch controls polish.** Opacity/scale settings in the menu, haptic feedback,
   an editable layout, and hiding the overlay while a gamepad is in use.
4. **Rumble.** `nuContRmbStart/Stop` are no-ops; SDL haptics on Android could
   drive the phone vibrator.
5. **32-bit ARM.** Only `arm64-v8a` is built. PaperShip's 64-bit fixes use
   `intptr_t`, so `armeabi-v7a` may work, but the decomp's pointer widening
   has only been exercised on 64-bit hosts.
6. **iOS.** libultraship has an iOS layer (Metal); the same PORT changes apply,
   but an Xcode project, code signing and a file-import UI are needed.
7. **Upstream sync.** PaperShip is actively developed; the Linux fixes here are
   worth sending upstream, and its later commits (chapters beyond 2, effect
   fixes) should be merged periodically.
8. **Pause-screen and framebuffer effects** are the same limitations as
   PaperShip's desktop build (see `PORT_SKIPS.md`).
