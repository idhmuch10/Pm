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
* libultraship is now built out of source. PaperShip built it inside
  `libultraship/` itself (and had committed 180 build files from the author's
  Mac); a desktop and an Android build running side by side clobbered each
  other's objects in `libultraship/src/libultraship.a`.
* `stb_image.h` is vendored (`libultraship/cmake/dependencies/stb/`): the
  configure-time download through the `github.com/.../raw/` redirect silently
  produced an empty header here, which only showed up as missing `stbi_*`
  identifiers deep into the build.
* `dead_*` aliases: PaperShip aliases the "dead" world areas' renamed engine
  symbols with assembler `.set` directives, which works in Mach-O but yields
  undefined symbols in ELF objects. CMake now generates
  `-Wl,--defsym=dead_X=X` for Linux/Android from the list in
  `port/dead_stubs.c`.
* Two tentative C definitions that older toolchains merged as common symbols
  (`nuGfxCfb_ptr` in `cam_main.c`, the `spirit_card` `Lights1` stub in
  `port/effect_gfx_textures.c`) are now `extern` under `PORT`; the NDK links
  with `-fno-common`.
* `src/is_debug.c` no longer defines a `void printf()` that overrode libc's
  for the whole program; under `PORT` the IS-Viewer debug output goes to
  stderr (logcat on Android).

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

* Android `assembleDebug` for `arm64-v8a` with NDK r27c, CMake 3.30 and AGP
  8.10 succeeds: the APK contains `libPaperShip.so` and `libSDL2.so` (16 KB
  page aligned), exports `SDL_main` and the five JNI entry points, and bundles
  `papership.o2r` and `gamecontrollerdb.txt`. The same build runs in CI
  (`.github/workflows/android.yml`).
* Desktop Linux: the whole tree compiles with clang 18 after the fixes above
  (the link needs the same `dead_*` aliases, generated for Linux as well).
* Nothing has been run on a physical device yet: boot, rendering, audio and
  input on real hardware are the next thing to test.

## Known gaps and next steps

1. **Device testing.** Boot, frame rate, audio latency and heat on real phones.
   Start with a mid-range arm64 device and `adb logcat -s PaperShip`. The frame
   limiter in `port/Game.cpp` spin-waits for the last ~2 ms of every frame,
   which is fine on desktop but wastes battery on a phone; replace it with a
   vsync-driven pace once the game is confirmed to boot.
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
