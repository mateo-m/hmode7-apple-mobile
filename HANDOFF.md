# H-Mode7 Port — Session Handoff

Snapshot of what's done, what's next, and the known caveats so this
can be resumed cleanly in a future session (possibly by a different
coder / agent). Updated after full port + mkxp-z integration.

## CRITICAL: Where to edit this port

Edit `.cpp` / `.h` files in **the git submodule at
`mkxp-z-apple-mobile/hmode7/src/`**, NOT in any other local clone.
Commit from the submodule working tree, push to `origin/main`, then
bump the parent-repo submodule pointer.

A preBuildScript in `ios/Empo/project.yml`
(`Verify hmode7 submodule is up to date`) fails the Xcode build if
either (a) the submodule has uncommitted changes or (b) its HEAD
differs from `origin/main`. If you see that error, commit / push /
pull inside the submodule, then retry the build.

Historical context: during the bring-up, a duplicate clone at
`/tmp/hmode7-apple-mobile` was being edited in parallel with the
submodule. The build silently compiled the stale submodule copy,
which hid already-made fixes for multiple iterations. The pre-
build verification exists to make that class of bug fail loudly.

## Status

**Completed** — all 9 functions ported, Ruby binding layer written,
xcodegen integration done, build succeeds, app launches on sim.

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
| `renderHM7`          | 1010       | 954      | `hm7_render.cpp`              |

Plus the portable Ruby↔C++ binding helpers (`hm7_bindings.cpp`,
191 LOC) and the pixel-access primitives (`hm7_pixels.h`, 60 LOC).

**Total port: ~2850 LOC of C++ across 19 files.** The bindings
layer compiles two ways: with `HM7_HAVE_MKXP_BITMAP` set, it resolves
`VALUE` arguments against mkxp-z's Bitmap/Table classes; without,
every binding is a null-returning stub so the pure pixel-math
functions can be unit-tested in isolation.

**Integration on the mkxp-z-apple-mobile side** (see
`mkxp-z-apple-mobile` repo, `dev` branch):

- `binding/hmode7-binding.cpp` — registers `HM7::Native` module
  functions via `rb_define_module_function`. Unwraps Ruby Bitmap/
  Table/Array/Hash objects into C++ types and dispatches into the
  `hm7::` namespace.
- `scripts/postload/hmode7_shim.rb` — overrides the 8 `HM7.self.xxx`
  methods in Insurgence's `210-HM7_NEW_CLASSES.rb` to bypass the
  Win32API stubs and call `HM7::Native` directly.
- `binding/binding-mri.cpp` updated to call `hmode7BindingInit()` +
  load `Postload/hmode7_shim.rb`.
- `ios/Empo/project.yml` updated with `HM7_HAVE_MKXP_BITMAP` define,
  extra header search path for `hmode7/src`, and source entries for
  all 10 `.cpp` files.
- `hmode7` added as a git submodule (branch=main) at top level of
  mkxp-z-apple-mobile.

**Verified**: `xcodebuild -sdk iphonesimulator -target Empo`
succeeds cleanly. The app installs + launches on iPhone 17 Pro
sim (iOS 26.4) without crash. No end-to-end game test done yet
(requires Insurgence game data on the sim).

## Remaining work

| Task                                                 | Priority  |
|------------------------------------------------------|-----------|
| Test on Insurgence: new game → Yes/Yes → verify flying-Pokemon intro renders | high |
| Pixel-diff against a Windows reference recording     | high |
| Fix `sScreenBitmap` width: needs to be `2 × screen.width` on the Ruby side | medium |
| Add `Bitmap::uploadCPURect(x,y,w,h)` to mkxp-z-apple-mobile | low (perf) |
| Split `renderHM7` into 5 helper functions per design doc §4 | low (readability) |

## How to resume / verify

### 1. Launch Insurgence on the sim

Install Insurgence game data into the Empo sandbox Documents dir.
Boot the sim, launch Empo, select Insurgence from the game list.
Start a new game, select Yes/Yes through the story prompt.

**Expected**: the 2-Pokemon-flying-over-world-map flyover renders
with rotation + fog. The world map beneath the Pokemon should be
visible (previously only the fog rendered).

**Failure modes**:
- All black with only fog → port is wired but `HM7::Native.xxx`
  is throwing Ruby-side errors. Check the SDL stderr pipe for
  exceptions; most likely cause is a shape mismatch between
  Insurgence's `@params` array and what the binding expects.
- Garbled pixels → byte-order or row-direction bug somewhere in
  the port. See "Known caveats" below.
- Correct rendering but low FPS → expected; the
  `Bitmap::uploadCPURect` optimization hasn't landed. Each frame
  does a full-bitmap GPU upload.

### 2. Diff against Windows reference

The port is deterministic integer arithmetic. Record a Windows
session of the intro at the same offset, compare frames.

### 3. Fix `sScreenBitmap` width

Add to `hmode7_shim.rb` something like:

```ruby
# Patch HM7 setup to allocate sScreenBitmap at 2× width (the
# native renderer needs 8 bytes/pixel of scratch space, not 4).
HM7::Map.class_eval do
  alias_method :_mkxp_orig_init, :initialize
  def initialize(*args)
    _mkxp_orig_init(*args)
    # @params[10] is sScreenBitmap; reallocate at 2× width.
    if @params && @params[10].is_a?(Bitmap)
      old = @params[10]
      @params[10] = Bitmap.new(old.width * 2, old.height)
      old.dispose
    end
  end
end
```

Untested — the exact class name (`HM7::Map` vs `HM7::Map_H_Mode7`)
needs verification against Insurgence's 210-HM7_NEW_CLASSES.rb:449
class definition.

## Known caveats / TODOs

1. **`@params[10]` needs 2× width** — the original Windows DLL
   overreads; our port would crash or paint garbage without the
   shim above.

2. **`drawTextureset` pointer arithmetic is the highest-risk
   port.** The original uses `(k << 7)` column offsets for animated
   tile atlas layout. First visual test will reveal any column-
   shift bug.

3. **`drawMapTileset` metadata byte layout** must stay in sync with
   `renderHM7`'s reads. Both are untested as a pair; the first run
   is the integration test.

4. **`compute_m7` default clip params** — Insurgence's `@parameters`
   is only 12 elements; the binding defaults `xMin/xMax/yMin/yMax`
   to full-lightline / data bounds. Check this matches Windows
   behaviour (the Windows DLL read uninitialized stack memory for
   those, which happened to be ~zero-ish).

5. **`render_hm7` default clip params** — Similarly, Insurgence's
   `@params` is 13 elements; the binding defaults `x_min=0,
   x_max=screen.w, y_min=0, y_max_draw=screen.h`. Reasonable but
   unverified against Windows.

6. **Performance**: full-bitmap `replaceRaw` every frame re-uploads
   `screen.w * screen.h * 4` bytes. For 640x480 that's 1.2 MB/frame
   = 72 MB/s at 60 FPS. Measurable but not catastrophic on modern
   hardware. `Bitmap::uploadCPURect` would cut this to the modified
   region only; deferred to post-correctness.

## Files (port repo — //hmode7-apple-mobile)

- `reference/MGC_Hmode7_1_4_4.cpp` — MGC's original, unmodified.
- `docs/HMODE7_PORT_DESIGN.md` — full design doc with algorithm
  analysis, data shapes, and risk matrix.
- `src/hm7_*.cpp` + `*.h` — the port.
- `README.md` — public-facing overview + credits to MGC.
- `HANDOFF.md` — this file.

## Files (integration — mkxp-z-apple-mobile, branch `dev`)

- `binding/hmode7-binding.cpp` — Ruby module registration.
- `binding/binding-mri.cpp` — calls `hmode7BindingInit()` + loads
  the postload shim.
- `scripts/postload/hmode7_shim.rb` — redefines `HM7.self.xxx` to
  use the native module.
- `hmode7/` — submodule of //hmode7-apple-mobile, branch=main.

## Commit log

### Port repo (//hmode7-apple-mobile)

```
fix: include table.h; replaceraw takes non-const void*
fix: use &table->at(0,0,0) for raw int16 access; correct rb_hash_foreach callback signature
feat: port renderhm7 - main rasterizer with pre/inline/final surface passes
docs: add handoff.md with session status and remaining work
feat: port drawmaptileset + refreshmaptileset - per-tile atlas build
feat: port drawtextureset - per-tile wall strip atlas build
feat: port drawheightmap - bilinear pattern sampling + per-tile height accumulation
feat: port computem7 - mode7 projection lut + per-row lighting
feat: port applylighting - heightmap row shadow/highlight pass
feat: port applyzoom + ruby bindings layer
docs: readme - add tvos back to platform list
feat: port applyopacity as port binding proof-of-concept
chore: import original MGC h-mode7 source and design doc
```

### Integration (mkxp-z-apple-mobile)

```
feat: integrate mgc h-mode7 native port for insurgence intro scene
```

### Outer project (empo-app)

```
build: wire hmode7 port into xcode project and bump mkxp-z submodule
```
