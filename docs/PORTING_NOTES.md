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

## First device report (Galaxy Z Fold 7) and the fixes made for it

The game boots, is playable through the intro up to the first Bowser fight, and
saves. Reported problems and what was done:

* **Wrong aspect ratio on the unfolded (almost square) screen.** Both
  libultraship's forced-4:3 render size (`Interpreter::StartFrame`) and the GUI's
  N64 mode (`Gui::DrawGame`) derived the width from the height and only
  pillarboxed; a window narrower than 4:3 rendered at the wrong aspect and was
  cropped on both sides. Both now use the largest 4:3 rectangle that fits
  (letterboxing when needed).
* **Opening logos with broken textures, invisible battle damage numbers.**
  (The real cause turned out to be the ROM-offset lookup, see the third round
  below; the changes here are still valid GLES fixes.)
  PaperShip is tested with the Metal backend; these looked like OpenGL ES-only symptoms.
  Applied: `highp` float/int precision in the GLES fragment shader (`mediump`
  breaks the alpha-dither noise derived from `sin(frame counter)` and
  nearest-neighbour sampling of larger textures), `GL_EXT_depth_clamp` when
  available (desktop GL always enables depth clamp, GLES 3.0 has none), and a
  mirrored-repeat fallback for `GL_MIRROR_CLAMP_TO_EDGE` (an invalid enum on GLES
  without the extension, which silently kept the previous wrap mode). These are
  the plausible causes; confirmation needs another device run.
* **Crash after losing the intro Bowser fight.** Cause unknown (no log yet).
  Changes that remove Android-only differences from the desktop build: the game
  thread now has a 64 MB stack (Java threads default to ~1 MB, desktop mains
  have 8 MB), heap pointer tagging is disabled (`allowNativeHeapPointerTagging`),
  and the decompiled C code is compiled at `-O1` like PaperShip's desktop build.
  The crash handler now writes a backtrace and the last log lines to
  `papership_crash.log`, hands the signal to the system tombstone, and the app
  offers to share the report on the next launch.

### Second device report and the tooling added for it

The aspect fix worked; damage numbers were still missing, the crash was pinned
to the moment Bowser powers up with the Star Rod, no crash report appeared, and
a black band stayed along one screen edge. Changes:

* **Crash reporting that cannot fail silently.** The previous handler resolved
  the log path through a JNI call from inside the signal handler. Now the data
  directory is passed in at startup (`nativeSetDataDir`), the handler runs on an
  alternate signal stack, writes with `open/write` only, and covers SIGABRT (the
  renderer's `abort()` calls), SIGILL, SIGFPE and SIGTRAP. Every session is also
  logged continuously to `papership_log.txt`, so even a hard kill leaves a log.
  On the next launch the app offers **Copy / Save to Downloads / Share** for the
  report; the Downloads copy is visible in the Files app.
* **Shader failures no longer abort.** libultraship called `abort()` when a
  fragment shader failed to compile and never checked link status. Both are now
  logged together with the full generated GLSL, a compile failure falls back to
  an empty fragment shader (the effect goes missing instead of the game dying),
  and each new shader variant logs its ids. A crash when a new effect first
  appears is the classic signature of a GLSL ES compile failure on Adreno, so
  the next log should show either the failing shader or a real backtrace.
* **Full-panel display.** The activity now draws under the display cutout
  (`LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS`) and hides the system bars through
  `WindowInsetsController`, removing the black band.
* **Testing tab** in the settings menu (`port/testing_bridge.c`): quick save /
  quick load anywhere, warp by area/map/entry, story-progress editor, and a
  preset that jumps straight to the intro Bowser confrontation.

### Fourth round: the crash report

The first report produced by the new crash dialog showed both remaining
symptoms at once. The session log was full of
`[render_effects_UI] CORRUPTION: gEffectInstances[0] = 0x6eb8bc3ea0 (invalid
pointer!)` and the crash was `SIGABRT` in `remove_effect` called from the
Star Rod script. `render_effects_UI` contained a PaperShip "diagnostic" that
declared any effect instance pointer above `0x800000000` (32 GB) corrupt and
nulled the array entry. That range only describes macOS user space; Android
(and Linux x86-64) hand out addresses far above it, so every effect instance
was dropped the frame it was created — no damage numbers, no battle effects —
and when the owning script later called `remove_effect()` on its instance the
lookup failed its `ASSERT`, which is the abort. A sibling heuristic in
`sfx_update_env_sound_params` (valid range 4 GB–32 TB) was removed for the
same reason. The ROM-offset fix from the third round was real and necessary
(effect graphics really were loaded from the wrong place), but it could not
show on a device while this check discarded the instances.

### Fifth round: effects confirmed, three new reports

The device confirmed the previous two fixes: damage numbers render and the
Star Rod power-up no longer crashes. Three new problems were reported, none
of which crashes, so no report was produced: Mario is invisible in the
Star Spirits' revival scene (`kmr_00`, `ANIM_Mario1_Fallen`), the partner
"clips everywhere", and doors can be walked through. The collision loader,
the wall test (`player_test_move_with_slipping`), the trigger code and the
revival script are platform-neutral on reading, and the earlier ARM-only
suspects (FMA contraction, float-to-int saturation) are ruled out because
upstream runs on Apple Silicon. To get device data without a crash, the
Testing tab now has **Save report (current log) now** (a JNI call from the
game thread into `MainActivity.requestReport()`, which shows the usual
Copy / Save / Share dialog with the running session's log) and a **Live
status** readout (player position/state/flags, `gCollisionStatus` ids,
collider counts, partner position/floor). The port also logs the collider
and vertex counts of every map, player animations without animation data,
and a full player raster cache.

### Sixth round: walls are walked through, the code is not the culprit

Two more device reports (one saved while "standing in" the gate at
(-274.8, 0, -28.9) of `kmr_02`, one east of the Goomba Road gate) show the
player inside solid colliders (`mm1`, and past `tt2`) with `wall -1` while
`floor` is always right, and the report dumps show correctly decoded
collider data (flags, bounding boxes, one-sided wall quads with sane normals,
the same script-driven `COLLIDER_FLAGS_UPPER_MASK` changes as on N64).
The wall path (`test_ray_colliders` -> `test_ray_triangle_horizontal`) reads
as platform-neutral, so it was tested instead of read:
`tools/port/collision_harness` builds `src/collision.c`, `src/43F0.c` and
`port/testing_bridge.c` unchanged, once with the host compiler and once with
the NDK's clang for aarch64 as a static Bionic executable run under
`qemu-aarch64` (same `-O1 -fno-strict-aliasing -fwrapv -fsigned-char` as the
APK). Both produce byte-identical output for the loader, the down/horizontal/
general ray tests, `sin_cos_rad`/`atan2`/`_wrap_trig_lookup_value` and the
movement code copied from `src/77480.c`: a player walking into a one-sided
gate is stopped at the collider radius on both. So the compiled code is fine
and the phone differs in what it feeds it at runtime. The port now records
that directly: every map load ray-tests all of its own wall and floor
triangles (`[collision] self-check`), and a per-frame watchdog in
`update_player` detects the player's position passing through a solid wall
triangle, logs the move, the player/collision state and yaws, and repeats the
game's wall ray, entity ray and movement test from the previous position.
The report dump also lists all entities and triggers, every triangle of the
nearby colliders, the movement test in eight directions and every map model
that carries a script transform (the revival scene hides Mario behind bushes
that `TranslateModel` slides apart, and its "clipping" character is Goombaria
walking through those bushes, so the model list is the data for that report).
The harness runs in CI for both architectures. Findings this round that are not bugs: the
gate collider `mm1` is genuinely solid at that story state (only the
`EVS_ReturnToVillage`/gate-opening scripts clear it), `o757` is made passable
by `kmr_02`'s own main script, and the odd floor fan of collider #61 (a
repeated vertex and a downward sliver) is what the map data contains.

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
