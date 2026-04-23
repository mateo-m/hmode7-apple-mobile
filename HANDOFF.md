# H-Mode7 Port — Session Handoff

Snapshot of what's done, what's next, and the known caveats so this
can be resumed cleanly in a future session (possibly by a different
coder / agent). Updated after the initial porting burst.

## Status

**Completed** — all 8 non-`renderHM7` functions have a pure-C++
port under `src/`:

| Function             | Source LOC | Port LOC | File                          |
|----------------------|-----------:|---------:|-------------------------------|
| `applyOpacity`       | 29         | 58       | `hm7_apply_opacity.cpp`       |
| `applyZoom`          | 94         | 127      | `hm7_apply_zoom.cpp`          |
| `applyLighting`      | 100        | 149      | `hm7_apply_lighting.cpp`      |
| `computeM7`          | 120        | 162      | `hm7_compute_m7.cpp`          |
| `drawHeightmap`      | 83         | 179      | `hm7_draw_heightmap.cpp`      |
| `drawTextureset`     | 102        | 279      | `hm7_draw_textureset.cpp`     |
| `drawMapTileset`     | 105        | 229      | `hm7_draw_map_tileset.cpp`    |
| `refreshMapTileset`  | 90         | (same)   | `hm7_draw_map_tileset.cpp`    |

Plus the Ruby↔C++ binding layer (`hm7_bindings.cpp`, 185 LOC) and the
pixel-access primitives (`hm7_pixels.h`, 60 LOC).

**Total port so far: ~1840 LOC of C++ across 17 files.** The bindings
layer compiles two ways: with `HM7_HAVE_MKXP_BITMAP` set, it resolves
`VALUE` arguments against mkxp-z's Bitmap/Table classes; without,
every binding is a null-returning stub so the pure pixel-math
functions can be unit-tested in isolation.

**Remaining** — one function + integration:

| Task                                                 | Effort    |
|------------------------------------------------------|----------:|
| `renderHM7` port (split into 5 per design doc §4)    | ~1000 LOC |
| Ruby-side shim overriding `Win32API.new("MGC_Hmode7", …)` | ~80 LOC |
| xcodegen integration (submodule + compile into mkxp-z-apple-mobile) | ~30 LOC |
| First end-to-end integration test on Insurgence intro |         — |
| Pixel-exact comparison against Windows reference run  |         — |

## How to resume

### 1. Finish `renderHM7`

This is the big monolithic rasterizer. The design doc (§1.7 and §4)
recommends splitting it into 5 helper functions:

1. `render_pre_surfaces` — pre-flushes surfaces whose `screenY1` is
   below the first drawn row. Runs exactly once (on `yt == yMax-1`).
2. `render_surface_column` — rasterizes each intersecting surface
   into the shared `sScreenBitmap` scratch for the current column.
3. `render_wall_column` — draws the per-column vertical wall strip,
   sampling from `colormap` (sides) and `mapTileset` (top), applying
   lighting + shadow + optional blend against the scratch buffer.
4. `render_final_overdraw` — fills the pixels above the topmost-
   drawn-Y with either surface pixel or transparency.
5. A top-level `render_hm7` that orchestrates the yt/xt loops and
   calls the above three inner helpers.

Read lines 763-1767 of the original `reference/MGC_Hmode7_1_4_4.cpp`.
Keep the integer fixed-point math exact; the only structural
changes are row direction + pixel byte order + splitting into
helpers.

**Known gotcha**: `sScreenBitmap` must be allocated as 2× width on
the Ruby side (design doc §10.5). Otherwise renderHM7 overreads.

**Known gotcha**: lightline bitmap has 3 rows. Row 2 = per-row
lighting; row 1 = per-column relief + horizontal zoom scratch;
row 0 = reserved for `ym` tracking. See design doc §10.6.

### 2. Write the Ruby shim

Override the nine `Win32API.new("MGC_Hmode7", "funcName", ...)`
bindings in Insurgence's `210-HM7_NEW_CLASSES.rb:41-49`. Replace
the calls (e.g. `HM7::Draw_Map_Tileset.call(...)`) with
`HM7::Native.draw_map_tileset(...)` where `HM7::Native` is a new
Ruby module our C++ registers via `rb_define_module_function`.

Template location: `mkxp-z-apple-mobile/scripts/postload/hmode7_shim.rb`
(new file). Should live alongside `pokemon_windowskin_fix.rb`.

Needs to be a postload (not preload) because `HM7` doesn't exist
until Insurgence's own scripts load.

### 3. Build integration

Add the `hmode7-apple-mobile` repo as a submodule of
`mkxp-z-apple-mobile`. Extend `ios/Empo/project.yml` to compile
`src/hm7_*.cpp` into the main target. Define
`HM7_HAVE_MKXP_BITMAP` when compiling so the bindings layer
resolves to real mkxp-z code instead of stubs.

Needs a `Bitmap::uploadCPURect(x, y, w, h)` method added to
mkxp-z-apple-mobile's Bitmap class to avoid the full-bitmap
sync roundtrip in `bitmap_commit_rect` (see TODO comment in
`hm7_bindings.cpp`).

### 4. Test on device

Launch Pokemon Insurgence on the sim or an iPhone. Start a new
game, select Yes / Yes through the story prompt. Expected
result: the 2-Pokemon-flying-over-world-map intro renders with
the rotation and fog. If instead you see mostly black with only
fog, the port isn't wired correctly. If you see a garbled
mess, the pixel byte order or row direction translation has a
bug somewhere.

Compare against a Windows screen recording frame-by-frame; any
pixel divergence is a bug in our port. The math is deterministic
integer arithmetic, so bit-exact output is achievable in
principle.

## Known caveats / TODOs

1. **`drawTextureset` pointer-arithmetic translation is the highest-
   risk port.** The original uses `(k << 7)` column offsets that
   interact with animated-tile atlas layout. The port mirrors the
   translation but I was unsure about the column shift semantics in
   a few places; expect to verify against reference output first
   thing.

2. **`drawHeightmap` reads `pattern.r` (not `pattern.b`).** The
   original reads byte `[0]` which is "blue" in BGRA. For grayscale
   heightpatterns R==G==B so either works, but if you ever feed a
   non-grayscale pattern (unusual), verify the channel is what you
   expect.

3. **`sScreenBitmap` width doubling** is enforced in the port (when
   we finish renderHM7) but the Ruby side needs a corresponding
   patch. Add a postload shim that overrides the sScreenBitmap
   allocation.

4. **`drawMapTileset` metadata byte layout** must stay in sync with
   whatever renderHM7 reads. Right now both are untested; the first
   test run will either Just Work™ or reveal a byte-ordering bug
   between them.

5. **Performance**: this is a CPU software rasterizer doing ~300k
   pixel operations per frame at 640x480. Expect 30-60 FPS on a
   modern iPhone. If it's slower, the candidate hotspots are the
   per-pixel surface-compositing loop and the bilinear sampling in
   `applyZoom`/`drawHeightmap`. NEON vectorization can help but
   isn't necessary for the proof-of-concept.

## Files

- `reference/MGC_Hmode7_1_4_4.cpp` — MGC's original, unmodified.
- `docs/HMODE7_PORT_DESIGN.md` — full design doc with algorithm
  analysis, data shapes, and risk matrix.
- `src/hm7_*.cpp` + `*.h` — the port so far.
- `README.md` — public-facing overview + credits to MGC.

## Commit log (high-level)

```
chore: import original MGC h-mode7 source and design doc
feat: port applyopacity as port binding proof-of-concept
docs: readme - add tvos back to platform list
feat: port applyzoom + ruby bindings layer
feat: port applylighting - heightmap row shadow/highlight pass
feat: port computem7 - mode7 projection lut + per-row lighting
feat: port drawheightmap - bilinear pattern sampling + per-tile height accumulation
feat: port drawtextureset - per-tile wall strip atlas build
feat: port drawmaptileset + refreshmaptileset - per-tile atlas build
```
