# Known bugs

Findings that belong upstream in Amiberry proper, not in the libretro glue.
Both were found with an AddressSanitizer build of the libretro core
(`make -C libretro SANITIZE=1`) driven by RetroArch:

```
retroarch -L libretro/amiberry_libretro.so --config <cfg> --max-frames=300
# with: ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=$(gcc -print-file-name=libasan.so)
```

---

## 1. Denise line renderer writes one pixel past the end of the draw buffer

**Status:** open. Present at every chipset resolution; only *fatal* at
`gfx_resolution=superhires`.

**Symptom.** With the core option `amiberry_video_resolution=superhires`, the
process aborts with glibc heap corruption (`malloc(): invalid size (unsorted)`,
SIGABRT). The abort surfaces later, in an unrelated `SDL_DestroySurface()` call
from `target_graphics_buffer_update()` — that is only where glibc notices the
damage, not where it happens.

**ASAN report** (superhires, PAL, 1504x576 surface):

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 4 at 0x... (0 bytes after 3465216-byte region)   # 1504 * 576 * 4
  #0 lts_ecs_fm0_n0_p2_ilores_dshres   src/linetoscr_ocs_ecs.cpp:23841
  #1 draw_denise_line                  src/drawing.cpp:6360
  #2 draw_denise_line_queue            src/drawing.cpp:8509
  #3 draw_line                         src/custom.cpp:10603
  #4 do_draw_line                      src/custom.cpp:10688
  #5 decide_hsync                      src/custom.cpp:10708
```

The write lands exactly one `uae_u32` past the end of the pixel buffer, i.e.
one pixel past the last pixel of the last row.

**Also fires at the other resolutions** — same call site, same shape, just a
different specialisation and a buffer whose slack happens to absorb it:

```
lores:  lts_ecs_fm0_n0_p2_ilores_dlores    src/linetoscr_ocs_ecs.cpp:42
hires:  lts_ecs_fm0_n0_p2_ilores_dhires    src/linetoscr_ocs_ecs.cpp:13097
```

So this is **not** superhires-specific; superhires just produces an allocation
size where the stray write hits allocator metadata instead of padding.

**Suspected root cause.** The native draw buffer is backed directly by the SDL
surface, which is an exact `w * h * 4` fit with no slack:

- `libretro_allocsoftbuffer()` (`src/osdep/libretro/gfx_platform_internal.h:217`)
  and `allocsoftbuffer()` (`src/osdep/gfx_window.cpp:992`) both give every
  buffer *except* `drawbuffer` a 2x2 padded allocation with a centred `bufmem`.
  `drawbuffer` gets `vram_buffer = true` and no memory of its own.
- `lockscr()` (`src/osdep/amiberry_gfx.cpp:1108`) then points `drawbuffer.bufmem`
  at `surface->pixels` and sets `width_allocated = surface->w`,
  `height_allocated = surface->h`, `rowbytes = surface->pitch`.
- `set_drawbuffer()` (`src/drawing.cpp:5626`) selects `inbuffer = &drawbuffer`
  whenever `drawing_can_lineoptimizations()` is true, so the Denise renderer
  writes straight into that exact-fit surface.
- `setxlinebuffer()` (`src/drawing.cpp:1879`) derives
  `xlinebuffer_end = xlinebuffer + inbuffer->outwidth * 4`, and
  `set_pixtotal_max()` (`src/drawing.cpp:5702`) derives `denise_pixtotal_max`
  from that. With `outwidth == width_allocated` there is zero headroom for the
  renderer's trailing pixel.

`src/osdep/gfx_window.cpp` has the identical `drawbuffer` wiring, so the
standalone SDL build is very likely affected too — not verified.

**Possible directions** (not attempted; both touch shared code):

- Pad the native surface: back it with an owned allocation via
  `SDL_CreateSurfaceFrom()` using `pitch = (w + 8) * 4` and `(h + 1)` rows'
  worth of memory, so a one-pixel row overrun lands in the stride padding and
  the last row has a spare line. Needs lifetime management, since
  `SDL_DestroySurface()` does not free externally supplied pixels.
- Or fix the bound: work out why the `denise_pixtotal < denise_pixtotal_max`
  gate in the generated `lts_*` routines permits one write past
  `xlinebuffer_end` and correct `set_pixtotal_max()` / `setxlinebuffer()`.

**Reproduce.** Set `amiberry_video_resolution = "superhires"` in the core
options and run any content (booting to Workbench with the bundled AROS ROMs is
enough). Aborts within a few hundred frames, every time.

---

## 2. `apply_port_device()` read a dead stack buffer — FIXED

**Status:** fixed in `libretro/libretro.cpp` (libretro glue only; nothing to do
upstream). Recorded here because ASAN flagged it during the investigation above
and it had been live for a while.

`TCHAR joy_name[8]` was declared inside the `else if` branch, assigned to the
outer `name` pointer, and then read by `inputdevice_joyport_config()` after that
branch's scope had ended — so joyport names ("joy0".."joy3") were parsed out of
a dead stack slot.

```
ERROR: AddressSanitizer: stack-use-after-scope
READ of size 1
  #0 inputdevice_joyport_config   src/inputdevice.cpp:10760
  #1 apply_port_device            libretro/libretro.cpp:3690
  #2 apply_libretro_input_options libretro/libretro.cpp:3884
  #3 retro_run                    libretro/libretro.cpp:5084
```

Fix: hoist the array to function scope so it outlives the branch.
