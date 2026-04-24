# H-Mode7 Remaining Sprite Bugs — Investigation & Fix Plan

**STATUS: RESOLVED.** Root cause and fix documented below; keeping this
file as a post-mortem / reference for future HM7 work.

Written originally as a live investigation plan, then updated once the
underlying bug was found.

## Status of the port

- All ground/wall/colormap rendering matches the Windows reference.
- Per-row lighting (blue-fade horizon) works end-to-end.
- Heightmap bush-region overlap fixed (was causing the "white shapes").
- Billboards render (depth-scale fallback on `sFYt` for slant = 0).
- Billboards no longer tilt at intermediate slant angles (depth-scale
  used unconditionally for sprites).

Two classes of defects remain:

1. **Z-ordering / occlusion** — sprites occasionally become partially
   transparent when "under" something in the scene.
2. **Positioning / mirror / clipping** — sprites sometimes appear
   partially cut off at the right edge, or worse, rendered at the
   **wrong horizontal position** (a partial sprite shows up near the
   left edge while the surfaces list claims `screen_x1 >= 211`).

## Confirmed ground truth

### Key references

- Original plugin source: `reference/MGC_Hmode7_1_4_4.cpp`
- MGC's forum announcement:
  <https://www.save-point.org/thread-3151.html>
- Key quote from MGC (2010): *"Characters are rendered directly in the
  HM7 bitmap, and thus there are limitations: transparency and blend
  type are ignored."* Also: *"By default characters are drawn at the
  maximal altitude of their coordinates: ground + 3 layers. You can
  activate the bush flag in the tilesets tab of the database to have
  the altitude of the tile ignored when drawing characters."*
- User-observed behaviour on Windows (confirmed via screenshots):
  - Sprites rotate with theta, **never tilt with alpha**.
  - Top-down (alpha = 0) shows sprites at their full native scale.
- `MapInfos.rxdata` confirms one of Insurgence's HM7 maps starts at
  `[#0]` (alpha = 0); this is almost certainly the intro map.

### Runtime diagnostics captured

- Multi-frame dump of the surface array at the point the user sees a
  partial sprite at the left edge:

  ```
  n=6 [0:(260,294)-(324,294) bmp=64x62]
      [1:(286,268)-(350,268) bmp=64x62]
      [2:(288,243)-(352,243) bmp=64x62]
      [3:(312,243)-(376,243) bmp=64x62]
      [4:(286,217)-(350,217) bmp=64x62]
      [5:(260,192)-(324,192) bmp=64x62]
  ```

  All 6 surfaces have `screen_x1 >= 260` and `screen_x2 <= 376`.
  **None are near the left edge**, yet a partial sprite renders there.

- Raw `@render` PPM dump (= pure port output, no fog/ui overlay)
  clearly shows a partial pelipper-beaked sprite at `x in [0, 60]`.

- `sinAngle` seen as `1` (raw-tagged Fixnum of 0) and `sFYt = 0` at
  alpha = 0 in the `compute_m7` diagnostic dumps.

## Mental model of the sprite pipeline

The plugin renders sprites in **three distinct passes**, all writing to
a 2×-width scratch bitmap (`sScreenBitmap`) with 8 bytes per slot:

| Slot byte | Meaning |
|-----------|---------|
| `[0]`     | claim flag (0 = empty, 1 = occupied) |
| `[1]`     | blend mode (0 = normal, 1 = add, 2 = subtract) |
| `[2..3]`  | packed `rYt - sHbase` (hbase hi/lo), used for Z-depth |
| `[4..6]`  | composited RGB (channel meaning is positional only) |
| `[7]`     | alpha / opacity |

Passes:

1. **Pre-surfaces pass** (`yt == yMax-1` only). Iterates surfaces while
   `yt <= screenY1`. Fires only for surfaces whose anchor line is at
   or below the screen's bottom row. For Insurgence's intro this
   **never fires** (`screen_y1` max is 294, `yMax-1` is 479).

2. **Inline surface pass** (inside the main `yt × xt` loop). Fires
   when `yt <= screenY1 && xt >= screenX1`. Renders the sprite's
   entire column range `[sXmin=xt, sXmax=screenX2)` in a single entry,
   writes into `sScreenBitmap` at `[ss_row = rYt - h][sXt]`. After
   rendering, advances `sIdx` to the next sorted surface.

3. **Final overdraw pass**. After the main `yt` loop completes, for
   each column, walks rows `[y0min-1, yMin]` (i.e., above the topmost
   drawn ground) and composites any remaining `sScreenData[0] == 1`
   pixels, or writes `alpha = 0` if none.

The **Z-ordering mechanism** is encoded in `sHMax` (read from the
lightline's ym-tracking row at column `sXt`): when the `h` loop is
about to write a pixel at `rYt - h > sHMax`, it `break`s. `sHMax` is
essentially the ground's topmost row for this column, so sprite
pixels can never render *below* the established ground — they only
render *above* it. That's how the engine achieves "sprites in front
of the ground but behind elevated walls".

Opaque-occlusion is a second mechanism: when about to write a sprite
pixel and the slot already has `[0]=1 && [1]=0 && [7]==255 &&
hbase_packed + 2 >= rYt - sHend`, the code `continue`s (the
already-drawn opaque surface wins).

## Z-ordering (transparency) bug — hypotheses

Ranked most-likely first.

### Hypothesis Z1: `sHMax` is being evaluated against a **stale** ym-row

The ym row (`lightline[row 2]`) is written by the main wall loop at the
end of each `(yt, xt)` iteration:

```cpp
// render.cpp main loop end
ylp[0] = (ody >> 8) & 0xff;
ylp[1] = (ody - (ylp[0] << 8)) & 0xff;
```

where `ody = rYt - dy` (= the row `dy` pixels above the anchor).

For **sprites rendered via inline pass at their anchor row**, we call
the inline sprite pass *before* the wall loop updates `ylp` for this
`(yt, xt)`. So `sHMax` comes from the *previous* `xt` iteration's
write to `ymRow[sXt]`, not the current one.

If the *previous* `xt` iteration wrote a `ymRow` value that is either
too large (letting sprites draw below where they should) or too small
(clipping sprites above where they should), the sprite gets rendered
at the wrong vertical extent.

Key observation: the inline pass uses `ymRow + (sXt << 2)`, which is
the sprite-column `sXt`, **not the current outer `xt`**. So the `sXt`
column's ym might not even have been touched in the current frame
yet — it could hold data from the previous frame.

The **original plugin has exactly the same structure**, so this isn't
unique to the port. But the port's `ym` initialisation might diverge.
The wall loop initialises `ym` on the first iteration (`yt == yMax-1`)
with `ysize + oScrY`. My port has that exact init, so probably not
the root cause.

**Test**: dump `ymRow[sXt]` at the instant a sprite renders.
Compare against the expected "sprite should draw up to row X" value.
If `sHMax` is larger than it should be, sprite draws too far down
and "goes under the ground".

### Hypothesis Z2: `sScreenData` stale data from a PREVIOUS frame

The `sScreenBitmap` is 2×-wide scratch. After the inline pass writes
sprite pixels, the wall loop reads and composites them, clearing
`[0] = 0`. But this **only happens within the visited `yt × xt`
area**. If the main loop and final overdraw together cover every
`(yt, xt)` cell, all pixels get cleared.

I previously analysed this and concluded no staleness could persist
across frames. However, there's a subtle edge case:

- If the sprite wrote to `(ss_row, sXt)` but the CURRENT frame's
  wall loop does not reach that cell (e.g. `ym` tracks higher than
  `ss_row`, so the wall loop never processes rows below `ym`), the
  sprite data stays in place.
- Next frame, the inline pass at that sprite's new position could
  write different sprite data, but the stale cell may still be read
  by the current frame's ground rendering.

**Test**: zero out `sScreenBitmap` at the start of each `render_hm7`
call (before any sprite writes). If the Z-ordering artefact
disappears, it was a cross-frame staleness bug.

### Hypothesis Z3: `hbase`-packed value comparison wrong

The occlusion check uses `sScreenData[2] + sScreenData[3] >= rYt`.
The packed value represents `rYt - sHbase` bracketed to 0/255/255+255
boundaries. The `+` (not `|`) between bytes is deliberate to get an
approximate numeric range — but it's imprecise.

For sprites with large `sHbase`, the packed value saturates to 510
(both bytes at 255). The comparison `510 >= rYt` is true for any
`rYt < 511`, but for screens at rYt up to 479 this is always true,
so the occlusion path always fires.

This matches the original plugin's behaviour, so unlikely to be a
port bug, but worth noting: this is **not** a real depth-test, it's
a coarse band used to decide whether the existing surface is "nearby
in depth".

---

## Positioning / mirror / cut-off bug — hypotheses

Ranked by likelihood.

### Hypothesis P1: `sScreenBitmap` 2×-width reallocation is silently failing

The post-load shim reallocates `@params[10]` from `Bitmap.new(w, h)`
to `Bitmap.new(w * 2, h)`. If for ANY reason the shim runs AFTER
`HM7::Tilemap#initialize` has already captured a reference to the
original bitmap somewhere **other than `@params[10]`** (e.g. an
instance var), our port writes to the 2×-wide one but reads use the
1×-wide one (or vice versa).

Consequence: writes at `sXt << 3` (8 bytes/slot) don't line up with
reads at `xt << 3`. A sprite written at `sXt = 270` may be read at
`xt = 540` (double), or at `xt = 135` (half). The "half" case
produces exactly the symptom we're seeing — sprite originally at
`x = 260` shows up at `x = 130`.

**Hunt check**:
- Grep Insurgence's scripts for any use of `@sscreen` / `@s_screen`
  instance var references in `HM7::Tilemap`.
- Add a `bitmap_surface(aref_or_nil(params_v, 10))->w` log in the
  binding to verify the width Python-runtime-received is 2× render
  width.
- If width is not 2×, investigate whether the shim even fired
  (`MKXP.puts` in the `if s.width >= @render.width * 2` path).
- **If width is exactly render_w, we've found it**: the shim hasn't
  taken effect for some surfaces, and my port is using wrong slot
  stride.

### Hypothesis P2: `load_surface` lambda state leakage

My `load_surface` lambda copies `surfaces[i]` fields into mutable
locals used by both pre-pass and inline-pass. If a sprite is
rendered and then a subsequent iteration enters a branch that reads
`sScreenX1` BEFORE `load_surface(next)` ran (or while `sNext = 0`
but `sScreenX1` retains the last sprite's value), we could get a
mismatched combination.

Specifically, after advancing `sIdx` past all surfaces and setting
`sNext = 0`, the `sScreenX1` etc. vars retain the last sprite's
values. If any subsequent code accidentally references them
(instead of gating on `sNext`), we'd render a phantom.

**Code audit**: verify every use of `sScreenX1/X2/Y1/Y2/Bitmap/etc.`
is behind an `if (sNext)` gate. The condition at line 621 is
`if (sNext && yt <= sScreenY1 && xt >= sScreenX1)`, gated on sNext
first with short-circuit eval. OK.

But maybe within the render body, some formula uses stale `sScreenX1`
after advance... look at lines 620-795.

### Hypothesis P3: Sorted surfaces + overlap causing double-render

The Ruby side sorts `@render_surfaces` descending by `screen_y`, then
ascending by `screen_x`. In the inline pass, when multiple surfaces
share the same `screen_y`, the xt-loop sweeps left-to-right: first
sprite renders `[X1, X2)`, advances, and the next sprite's
`screen_x1` may be **less than the current `xt`** (if the next
sprite's X1 is anywhere before the first sprite's X2).

When that happens, the condition `xt >= sScreenX1` is already true
when we reach the next sprite's X1, so we enter at `xt = (current)
not sScreenX1`. But `sXmin = xt`, not `sScreenX1`, so we render
**only the right portion** of the sprite.

For a pair of sprites at `[210, 274]` and `[240, 304]` (overlap at
`[240, 274]`):
- At `yt = anchor`, enter sprite[0] at `xt = 210`. Render `[210, 274)`.
  Advance. Now sprite[1] loaded.
- At `xt = 211 .. 239`: `211 >= 240` FALSE (sprite[1].X1=240). Skip.
- At `xt = 240`: `240 >= 240` TRUE. Enter. `sXmin = xt = 240`. Render
  sprite[1] from column 240 to 303. That's the full sprite.

Wait — this is actually correct. `sXmin = xt` happens to equal
`sScreenX1` when we first match. The overlap case works.

Let me trace for overlap where sprites have flipped sort order:
`[240, 304]` and `[210, 274]`. X-asc sort puts `[210, 274]` first.

- At `yt = anchor`, enter sprite[0] = [210, 274] at xt=210. Render
  `[210, 274)`. Advance. sprite[1] = [240, 304] loaded.
- At `xt = 211..239`: `xt >= 240` FALSE. Skip.
- At `xt = 240`: `xt >= 240` TRUE. Enter. `sXmin = xt = 240`. Render
  sprite[1] `[240, 303)`.

Both correctly rendered. **This hypothesis is likely NOT the bug.**

### Hypothesis P4: The pelipper at left edge is a **different** sprite

Looking at the user's screenshots carefully: the left-edge sprite
has a yellow beak (pelipper's distinguishing feature), while the
center sprites are smaller bird-silhouettes.

It's possible that the sprite at the left edge is **$game_player**,
which in Insurgence's intro is a character with the pelipper sprite
graphic (the player starts the game riding / being delivered by the
postal pelipper).

If `$game_player.screen_x` is legitimately near 0 during that frame
(player character is at the left edge of the map), Ruby would push
a surface with small screen_x1 into `@render_surfaces`. My diagnostic
only dumped the first 4 surfaces in its early version; the 6-surface
dump shows all 6 have X >= 260. So this hypothesis is **falsified**
by the diagnostic.

**Unless** the diagnostic fires on a different frame than the PPM
dump (the PPM captured at frame 101, diag at different cadence).
The pelipper at left edge could exist at a frame between dumps.

**Test**: dump surfaces **and** PPM on the same frame. Compare
surface X positions to what visually appears on screen.

### Hypothesis P5: Ruby `update_screen_coordinates` is computing different values on our platform

`$game_temp.cos_theta` etc. come from `(2048 * Math.cos(rad)).to_i`.
`Math.cos` is a Ruby stdlib call. mkxp-z uses Ruby 3.1; RGSS1 Windows
used Ruby 1.8. Edge cases in float-to-int rounding could produce
slightly different cos/sin values, which feed into
`update_screen_coordinates`.

However, the user's Windows screenshot is a DIFFERENT app (RGSS1) and
the Empo app uses mkxp-z's Ruby 3.1. So they're NOT running the same
Ruby. Rounding drift is plausible but should be sub-pixel — not
large enough to shift a sprite by 240 pixels.

**Unlikely but cheap to check**: dump the cos_theta/sin_theta values
during both Windows (via a modified Ruby script) and Empo.

### Hypothesis P6: `sInverse` leaking to 1 for some sprite

My shim hardcodes `sInverse = 0`. But if any code path in my binding
copies a non-zero `inverse` value from the Ruby-side surface, the
inline pass would flip the X-sampling (line 260-268 in
`hm7_render.cpp`).

My binding uses `rs.inverse = aref_int(s, 5);`. For the 11-element
surface from my shim, index [5] is hardcoded `0`. So inverse = 0.

Unless a non-shim fallback runs for some surface (e.g., when
`bitmap == nil`), the **original 6-element** form would be used.
Index [5] in the original is `blend_type`, not `inverse`. If
`blend_type` happens to be 1 for a sprite, my binding would misread
it as `inverse = 1`, causing a horizontal mirror on that sprite.

**Strong test**: add a warning log in the shim whenever
`_mkxp_hm7_orig_get_data` is invoked (the fallback path). If ever
triggered, it explains everything: the fallback 6-element form
leaks a non-zero value into `inverse`, mirroring the sprite.

**Hypothesis P6 is the strongest candidate.** It explains:
- Why sprites appear at the wrong horizontal position (mirrored).
- Why only SOME sprites are affected (only those with
  `bitmap == nil` at the moment of get_data — likely during sprite-
  graphic swap frames, e.g. animation transitions).
- Why the effect looks like "duplicated at left edge" — the mirror
  maps `x` to `screen_w - x`, and for a sprite naturally on the right
  half, the mirror lands on the left half.

## Concrete fix plan (execute in this order)

### Step 1: instrument the shim fallback

Add to `hmode7_shim.rb`, inside the `get_data` override:

```ruby
def get_data
  if bitmap.nil? || bitmap.disposed?
    $mkxp_hm7_fallback_count ||= 0
    $mkxp_hm7_fallback_count += 1
    if ($mkxp_hm7_fallback_count % 60) == 1
      MKXP.puts "[hm7-shim] get_data fallback #{$mkxp_hm7_fallback_count}: " \
                "character=#{character.class} event_id=" \
                "#{character.respond_to?(:id) ? character.id : '?'}"
    end
    return _mkxp_hm7_orig_get_data
  end
  # ... existing synthesis ...
end
```

Run the intro. If `[hm7-shim] get_data fallback` appears → P6 is
confirmed. If it never fires → P6 is out, investigate P1.

### Step 2 (if P6 confirmed): harden the fallback

Instead of returning the 6-element form when bitmap is nil, return a
sentinel that my binding treats as "skip this surface":

```ruby
def get_data
  return nil if bitmap.nil? || bitmap.disposed?
  # ... existing 11-element synthesis ...
end
```

Then in the Ruby caller, filter out `nil` entries before passing to
`HM7::Native.render_hm7`. Alternatively, handle nil in the binding's
surface unpacking loop:

```cpp
if (NIL_P(s) || !RB_TYPE_P(s, T_ARRAY)) continue;
if (RARRAY_LEN(s) < 11) continue;  // defensive: skip 6-elem fallback
```

### Step 3 (if P6 not confirmed): check sScreenBitmap width at runtime

Add a binding-side log:

```cpp
if (rp.s_screen_bitmap) {
    static int warned = 0;
    if (!warned && rp.s_screen_bitmap->w < rp.screen_bitmap->w * 2) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "s_screen width=%d expected=%d",
            rp.s_screen_bitmap->w, rp.screen_bitmap->w * 2);
        mkxp_debugLog("HM7-WARN", "...", buf);
        warned = 1;
    }
}
```

If the log fires → shim isn't effective; P1 confirmed. Investigate
why the shim doesn't take effect (might be an initialization order
bug where `@params[10]` gets rebuilt after `initialize`).

### Step 4: add a frame-start clear of `sScreenBitmap`

Regardless of which hypothesis wins, clearing `sScreenBitmap` at the
start of every `render_hm7` call is a defensible correctness
guarantee (rules out Z2 and any cross-frame staleness):

```cpp
if (rp.s_screen_bitmap) {
    std::memset(rp.s_screen_bitmap->pixels, 0,
                rp.s_screen_bitmap->pitch * rp.s_screen_bitmap->h);
}
```

Cost: ~2.5MB memset per frame for 640×480 2×-wide scratch. At 40 FPS
that's 100 MB/s — negligible. Do this first to rule out staleness.

### Step 5: test the depth-scale fallback's edge case

The current billboard fix uses `(lr[2] << 8) + lr[3]` for `sFYt`.
But at the top of the screen (small `yt`), the zoom value is
larger than at the anchor row. My sprite rendering reads
`reliefRow + ((yt - sH0) << 2)`, where `yt - sH0 = screenY1` after
accounting for slope. For billboards with `slope = 0`, `yt - sH0 = yt`.

But `yt` in the inline pass is always `screenY1` (on the first
iteration where condition matches). So we read relief at column
`screenY1`. That's the correct anchor depth.

**Verify**: with depth-scale, `sRealHeight` at different slant
angles — does it stay within `[0.8 * sHeight, 1.2 * sHeight]`?
If so, sprites render at roughly their native size regardless of
alpha, which matches Windows behaviour ("sprites don't tilt").

If `sRealHeight` swings wildly with alpha, the depth-scale formula
isn't right; investigate using a constant sFYt (like `4096 * zoom`)
derived from `$game_temp.zoom_sprites` passed through Ruby.

### Step 6: verify surface sort order

Ruby:

```ruby
@render_surfaces.sort! {|a, b|
  b[2] - a[2] == 0 ? a[1] - b[1] : b[2] - a[2]
}
```

`a[2]` / `b[2]` = `screen_y1` in the 11-element layout = anchor_y
in my shim's synthesis. Descending by anchor_y (bottommost first),
tiebreak ascending by `a[1]` = `screen_x1`. That's correct.

But **the original 6-element form has `screen_y` at index [2]**, so
the sort also works on the original form. If my shim's 11-element
form is ever NOT applied (P6 fallback), the sort still happens but
on a different field. The bottom-most surface by screen_y = foot
anchor, which matches my synthesis — accidentally correct.

No bug here.

### Step 7: clamp the rightmost column off-by-one

The inline pass's `sX` formula produces `sX = sRowSize` exactly when
`sXt = sScreenX2 - 1` (dx2 = 0). The bounds check
`if (sX >= sRowSize) continue;` then skips that column.

That means the rightmost pixel column of every sprite is skipped.
For a 64-pixel sprite this is one column = ~1.5% loss. Not usually
noticeable, but matches the user's "right-most bird is cut-off"
observation.

**Fix**: clamp `sX` instead of continue:

```cpp
if (sX < 0) sX = 0;
else if (sX >= sRowSize) sX = sRowSize - 4;
```

Or (closer to the original intent): the `if sInverse` branch uses
`dx2` for `sX`, and `dx2` can be 0. Skipping that column is a
deliberate choice in the original. So maybe leave it as-is for
fidelity and accept the 1-column loss.

Status: cosmetic, can be fixed last.

## Actual root cause (resolved)

After running the morning diagnostics, the winner was
**Hypothesis P1 with a new twist**: the postload shim's
`HM7::Tilemap#initialize` wrapper was never installing, because of
a Ruby visibility gotcha.

### The Ruby gotcha

`Class#method_defined?(name)` returns `true` only for **public** and
**protected** instance methods. It does NOT return true for private
ones. And `initialize` is **always private** in Ruby, regardless of
how the class declares it:

```ruby
class Foo
  def initialize; end  # looks public, but Ruby makes it private
end

Foo.method_defined?(:initialize)          # => false (!)
Foo.private_method_defined?(:initialize)  # => true
```

My shim checked `HM7::Tilemap.method_defined?(:initialize)` and
silently skipped the patch when it returned `false`. So the 2×-width
`sScreenBitmap` reallocation never happened; the port was writing
8-byte slots into a bitmap with pitch = 4 bytes × render_width.

### Why it looked like a positioning / mirror / z-ordering bug all at once

With pitch = `render_w × 4` bytes and slot stride = 8 bytes, only
`render_w / 2 = 320` column slots fit in each row. Any sprite write
at `sXt ∈ [320, render_w)` wrapped into the **next row at byte 0**,
which is equivalent to:

- column slot 0..(sXt - render_w/2) of the next row
- written at vertical offset `ss_row + 1` for those columns

The symptoms this creates are exactly what the user reported:

- **Left-edge partial sprite**: a sprite rendered at `sXt = 260..323`
  writes slots 260..319 correctly but wraps slots 320..323 into
  row (ss_row + 1), columns 0..3. Reading later from row (ss_row+1)
  col 0..3 sees those pixels and paints them at the screen's left
  edge.
- **Sprite appears partially transparent in certain spots**: since
  the wrapped data corrupted `sScreenData[0]` (the claim flag) at
  other column slots, the wall-loop's occlusion check
  (`sScreenData[0] && ... && hbase_packed >= rYt`) wildly
  misbehaves: sometimes the wall loop thinks a sprite should be
  drawn there when it shouldn't (ghost at left edge), other times
  the sprite's own columns are masked by stale claim flags from
  adjacent columns (transparency artefacts at specific camera
  angles).
- **Angle-dependency**: the symptom visibility depended on camera
  angle because that's what varied `sXt` ranges. At certain angles
  more sprites crossed the `sXt = 320` wrap boundary, at others
  none did.

### The fix

Two one-line changes:

1. `scripts/postload/hmode7_shim.rb` — check both `method_defined?`
   and `private_method_defined?` for `initialize`:
   ```ruby
   tilemap_has_init =
     defined?(HM7::Tilemap) &&
     (HM7::Tilemap.method_defined?(:initialize) ||
      HM7::Tilemap.private_method_defined?(:initialize))
   ```
   And use `private_method_defined?(:_mkxp_hm7_orig_initialize)`
   inside the `class_eval` idempotency guard as well.
2. `binding/hmode7-binding.cpp` — keep the runtime width check as a
   permanent guard so any future regression of this kind is caught
   instantly instead of silently corrupting rendering:
   ```cpp
   if (!widthWarned && rp.s_screen_bitmap->w != rp.screen_bitmap->w * 2) {
       mkxp_debugLog("HM7-WARN", ..., "s_screen width=... expected=...");
   }
   ```

## Lessons to keep

1. **Never use `method_defined?(:initialize)`** in Ruby. Always
   prefer `private_method_defined?`, or simpler:
   `klass.new rescue` + recovery, or `instance_method(:initialize)`.
2. **Always add a runtime correctness check** for invariants that
   depend on a Ruby-side monkey-patch landing: if the patch silently
   fails, the native code often misbehaves in non-obvious ways that
   look like rendering bugs but are actually data-layout bugs.
3. **Hypothesis ranking was wrong**: I ranked P6 (shim fallback
   leaking fields) above P1 (shim reallocation not happening), but
   P6 never fired (fallback log was empty in actual runs) and P1 was
   in fact the bug. The lesson: if a hypothesis has a cheap runtime
   test, promote it above hypotheses that only have code-inspection
   support.
