# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

Amiberry is an Amiga emulator built on the WinUAE emulation core. `src/` is
largely vendored/merged WinUAE code; `src/osdep/` is Amiberry's platform layer.

This checkout is the **`sasq64/amiberry` fork on the `demarc` branch**, whose
reason for existing is the **libretro core** in `libretro/`. `origin` is the
fork; `upstream` is `BlitterStudio/amiberry`. Upstream changes arrive via merge
commits from `upstream/master`, so:

- Prefer fixing things in the `libretro/` glue over touching `src/`, since
  changes to shared code have to survive future upstream merges.
- Findings that genuinely belong upstream are recorded in `BUGS.md` rather than
  patched locally. Read it before chasing a crash — the open entry there (Denise
  line renderer writing one pixel past the draw buffer) explains a whole class
  of heap-corruption symptoms, especially at `superhires`.

## Builds

There are **two independent build systems** producing two different targets.

### Desktop app (CMake)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Or use a preset from `CMakePresets.json` (`linux-debug`, `linux-release`,
`linux-relwithdebinfo`, and macOS/Windows/Android equivalents), which build into
`out/build/<preset>`. Feature toggles are the `USE_*` / `WITH_*` options at the
top of `CMakeLists.txt`. Dependencies come from vcpkg (`vcpkg.json`) on
Windows/macOS; system packages elsewhere. Sources are listed in
`cmake/SourceFiles.cmake`, not globbed.

### libretro core (hand-written Makefile)

```bash
make -C libretro platform=unix -j$(nproc)      # -> libretro/amiberry_libretro.so
make -C libretro DEBUG=1 ...                   # -O0 -g
make -C libretro SANITIZE=1 ...                # ASan (see BUGS.md for usage)
make -C libretro JIT=0 ...                     # interpreter only
make -C libretro clean
```

`platform` is `unix` | `osx` | `win`; `ARCH` overrides `uname -m`.

**This Makefile compiles object files in-tree, next to the sources** (`src/*.o`,
`libretro/*.o`) and those objects do *not* encode the flag set. Run `make -C
libretro clean` whenever you flip `JIT`, `DEBUG`, `SANITIZE`, `ARCH`, or the
compiler — stale objects otherwise link silently into a wrong core.

The version is parsed out of `CMakeLists.txt` by the Makefile; `CMakeLists.txt`
is the single source of truth for it.

### Running the core against RetroArch

`tools/run_libretro_local.sh` builds the core, provisions a throwaway RetroArch
test root (system dir, Kickstarts, whdboot assets, save/state dirs, config) and
launches it:

```bash
tools/run_libretro_local.sh --roms /path/to/kickstarts --content game.lha --run-secs 30
```

It handles the `make clean` needed on an arch switch itself. Core-side logging
goes to `<test-root>/save/Amiberry/Amiberry.log`; setting
`AMIBERRY_LIBRETRO_DEBUG=1` (the script does) turns on the extra debug log.

## Tests

Tests are **standalone shell scripts**, one per unit, each compiling a single
`tests/*_test.cpp` with `-Wall -Wextra -Werror` against a handful of headers and
running it. They are not wired into CMake/CTest and **not run by CI** — run them
yourself, from the repository root (the `-I.` / `-Isrc` paths assume that cwd):

```bash
tests/test_libretro_crop_policy.sh
tests/test_play_content_detection.sh
```

There is no aggregate runner; `for t in tests/test_*.sh; do "$t" || echo "FAIL $t"; done`
is the usual way to sweep them.

This shape constrains what is testable: logic that wants a test is factored into
a header or a small standalone `.cpp` (e.g. `libretro/libretro_crop_helpers.h`,
`src/osdep/amiberry_gfx_geometry.h`, `src/osdep/imgui/play_content_detection.cpp`)
so a test can include it without dragging in the emulator.

`tools/` holds Python guard scripts run manually — `check_libretro_contract.py`
asserts libretro API invariants that are easy to regress by refactoring,
`check_gencomp_outputs.py` guards the JIT generator's checked-in output layout.

## Architecture

### Platform indirection (`*_platform_internal.h`)

The osdep layer is shared between the desktop app and the libretro core; the
split is done at compile time by the preprocessor rather than by `#ifdef`
thickets. Headers like `src/osdep/gfx_platform_internal.h` do nothing but

```c
#include OSDEP_GFX_PLATFORM_HEADER
```

and each build defines that macro to its own implementation:

| macro | CMake (`cmake/SourceFiles.cmake:603`) | libretro (`libretro/Makefile:137`) |
|---|---|---|
| `OSDEP_GFX_PLATFORM_HEADER` | `gfx_platform_internal_host.h` | `libretro/gfx_platform_internal.h` |
| `OSDEP_AMIBERRY_PLATFORM_HEADER` | `amiberry_platform_internal_host.h` | `libretro/amiberry_platform_internal.h` |
| `OSDEP_INPUT_PLATFORM_HEADER` | `input_platform_internal_host.h` | `libretro/input_platform_internal.h` |
| `SOUND_PLATFORM_HEADER` | `sound_platform_internal_host.h` | `sound_platform_internal_libretro.h` |

The `_host.h` variants live in `src/osdep/`, the libretro ones in
`src/osdep/libretro/`. When adding a hook to the osdep layer, add it to *both*
sides or the other build breaks at link time.

### The libretro core

`libretro/libretro.cpp` (~5.9k lines) is the whole frontend-facing surface.

- **Threading is fibers, not threads.** `retro_run` `co_switch`es into
  `core_fiber`, which runs `amiberry_main()` as if it were a normal application.
  The emulator yields back exactly once per presented frame:
  `gfx_platform_present_frame()` (`src/osdep/libretro/gfx_platform_internal.h`)
  calls `video_cb(...)` and then `libretro_yield()`, which switches to
  `main_fiber`. So one `retro_run` ≈ one emulated frame, and everything in
  `src/` runs on the core fiber.
- **Audio is re-paced.** Paula flushes fixed-size chunks that don't divide into
  the per-frame sample count, so `sound_platform_output_audio` enqueues into a
  FIFO (`libretro_audio_enqueue`) and `retro_run` drains one frame's worth
  through `audio_batch_cb`. Don't call the frontend audio callback directly from
  the emulation side.
- **The GUI is stubbed out, not compiled.** The core never builds
  `src/osdep/imgui/` or SDL. `libretro/libretro_gui_stubs.cpp` and
  `libretro/libretro_stubs.cpp` supply the `gui_*` symbols and globals the core
  expects; `libretro/sdl_stub.cpp` + `libretro/sdl_compat.h` reimplement enough
  of the SDL3 surface/types API for `amiberry_gfx.cpp` etc. to compile and run
  headless. Adding a call into GUI or SDL code from shared `src/` will break the
  core's link — add a stub alongside.
- **File I/O goes through libretro VFS.** `src/osdep/libretro/posixemu_vfs.cpp`
  and `stdioemu_vfs.cpp` reroute the emulator's file access to the frontend's
  VFS interface, so content can live inside archives or non-POSIX storage.
- **Core options are declared twice.** `option_defs[]` (v2, categorised) is
  authoritative; `variables[]` above it is the v1 fallback for old frontends.
  The comment at `libretro/libretro.cpp:682` says it, and it is easy to get
  wrong: **edit both**. Option keys are `amiberry_*`, and most are applied by
  translating them into Amiberry `prefs` fields.
- `libretro/amiberry_libretro.info` is generated by the Makefile; the supported
  extension list is scraped out of `info->valid_extensions` in `libretro.cpp`.

### Desktop app specifics

`src/osdep/imgui/` is the Dear ImGui GUI (one file per settings panel);
`src/osdep/` also holds renderers (`opengl_renderer.cpp`, `vulkan_renderer.cpp`,
`sdl_renderer.cpp`, chosen via `renderer_factory.cpp`), shader/bezel support,
and an IPC control socket documented in `docs/ipc.md`.

### CI

`.github/workflows/libretro.yml` builds the core for Linux/macOS/Windows on
every push to `demarc` and publishes to a rolling `latest` release on the fork.
It asserts the JIT is actually linked in (`grep -qa 'JIT: cache=' ...`) because
a JIT-less core still advertises the "JIT Recompiler" option while only
disabling cycle-exact timing. `.github/workflows/c-cpp.yml` is upstream's
desktop build matrix.
