# MGC H-Mode7 Plugin — iOS / mkxp-z Port Design Document

**Source:** `MGC_Hmode7_1_4_4.cpp` (1897 lines, Win32 DLL, RGSS1 ABI)
**Target:** `mkxp-z-apple-mobile` engine, Ruby 3.1 MRI C API, Apple iOS
**Purpose:** Analysis-only document describing algorithms, data shapes, quirks, and port complexity. No code in this doc.

---

## 0. Executive Summary

MGC H-Mode7 is a CPU software renderer written for RPG Maker XP (RGSS1) that projects a 2D tile map into a pseudo-3D Mode-7-style view with per-tile height, animated autotiles, vertical wall textures, dynamic lighting, shadows, and object "surfaces" (billboard sprites) with opacity / blend modes. It is a **per-pixel, per-scanline software rasterizer** — no GPU, no shaders — that reads/writes directly into RGSS `Bitmap` and `Table` memory.

The entire DLL is self-contained and uses essentially **no Windows APIs**. The only Win32-specific content is:
- `<windows.h>` typedefs (`DWORD`, `LPBYTE`, `RGBQUAD`, `BITMAPINFOHEADER`) — all trivially replaceable with `uint32_t` / `uint8_t*` / equivalent.
- `__declspec(dllexport)` / `__declspec(dllimport)` — remove.
- `Win32API.new` binding on the Ruby side — replace with direct Ruby C extension method bindings (`rb_define_module_function`).

There is **no GDI**, **no DirectX**, **no threading**, **no IO**, **no CRT beyond malloc/free**. Math uses only integer arithmetic (no `<math.h>` despite being included). The code is almost trivially portable once the **RGSS1 ABI handling** is replaced with Ruby 3.1 MRI equivalents and the **Bitmap pixel layout** is adapted to mkxp's SDL/GL-backed `Bitmap`.

**Biggest porting challenges, in order:**
1. **Bitmap pixel access.** RGSS1 gave a flat pointer to a bottom-up DIB. mkxp-z `Bitmap` wraps a GL texture + SDL_Surface. A `raw_data`/`rawData`-style accessor must exist (mkxp-z already has `Bitmap#raw_data` and internal `SDL_Surface*` access). The plugin works on **raw BGRA bytes**, so we must guarantee linear CPU-visible memory, and sync it back to the GPU texture when done.
2. **Table ABI.** RGSS1 exposed `Table` as a Ruby object whose memory layout held `SWORD* data` at a known offset. mkxp-z re-implements `Table` in C++; we need a `Table*`→raw `int16_t*` accessor. mkxp-z already has this (`Table::at(x,y,z)` and a data pointer).
3. **Hash iteration.** The original DLL walked `st_table` bin chains directly. In Ruby 3.1, `st_table` internals are opaque and have changed (open addressing). We **must iterate hashes via `rb_hash_foreach`** instead of peeking bins.
4. **Integer `__id__` tagging.** RGSS1 produced tagged integer IDs where `id << 1` yielded the raw `VALUE` pointer. In Ruby 3.1, `__id__` returns a Fixnum ID that is **not** the raw VALUE; instead we should take Ruby objects by `VALUE` directly via `rb_define_method` with the usual `VALUE self, VALUE arg1, ...` signature, bypassing `__id__` entirely.
5. **The big kernel `renderHM7`** (lines 763–1767, ~1010 LOC). It's a single giant function with deeply nested branches; splitting it into helpers while preserving semantics will dominate the port effort.

---

## 1. Algorithm Summary (Per Exported Function)

Below, "BGRA" means a 4-byte per pixel layout `{B, G, R, A}` in memory order (DIB native on Windows, identical to `SDL_PIXELFORMAT_ARGB8888` on little-endian or `SDL_PIXELFORMAT_BGRA32` depending on SDL version — see §3).

### 1.1 `computeM7(dataTable, lightline, params)` — ~119 LOC
**What:** Precomputes, for each screen row × column inside the clipped draw region, the **source-map (x,y) coordinates** corresponding to the projected mode-7 ray. Also precomputes per-row **lighting attenuation** and a per-row **scale factor** used to convert map Y-heights to on-screen vertical extents. This is a "tile-projection LUT" builder — it does not draw anything.

- Read rotation (`cosTheta`,`sinTheta`), slant (`cosAngle`,`sinAngle`), pivot, slope, correction, projection distance, zoom from `params` (Ruby `Array` of 16 Fixnums).
- For each screen row `yt` in `[max(heightLimit, yMin), yMax)`:
  - Project screen Y → world Y (`yp`): `yp = pivot + (altitude * (yt-pivot)) / ((altitude-distProj)*cosAngle + (yt-pivot)*sinAngle >> 12) * zoom >> 12`.
  - Compute horizon lighting attenuation `ypl = pivot - yp`. When above horizon, write attenuated B/G/R plus untouched "flag" (add/sub) into `lightline[row=0, x=yt]`.
  - On first iteration of the column loop, compute a "relief coefficient" and a "horizontal zoom" value and stash them into row `-1` of the lightline bitmap (rows above `firstRow` are valid because lightline bitmap is allocated taller — used as a scratch area).
  - For each column `xt` from `xMin` up to `a = screenWidth/2`:
    - Compute `xp` (projected X scale) via `zoom * ((a - xt) << 18) / val_1 >> 12`.
    - Apply 2D rotation (cos/sin theta) around the pivot `(a, pivot)`.
    - Write the resulting `(xr, yr)` map coords into `dataTable[xt, yt, 0]` (xsize×ysize×2 Table); also mirror on the right half (`xsize-1-xt`) for cheap horizontal symmetry.
- Returns 0.

**Inputs:**
- `dataReference` (Table): `xsize × ysize × 2` `int16_t`. Plane 0 = precomputed source X; plane 1 = precomputed source Y.
- `lightlineReference` (Bitmap): one row is used as an index-by-screen-Y cache (row 0 = RGBA lighting attenuation for that row; row `-1` = scratch with per-column relief/zoom).
- `paramsId` (Array of 16 Fixnums — see §2.1).

### 1.2 `drawHeightmap(heightmap, heightpattern, mapTileset, tilemapData, nbLayers)` — ~83 LOC
**What:** Builds the **giant per-pixel heightmap** for the entire map (size `(mapW*32)*2 × (mapH*32 + mapH*16)`, an `int16_t` Table). Plane 0 rows carry the summed ground height per pixel; additional rows carry "bush" (obstacle) heights. Uses bilinear sampling of a small "height pattern" texture to derive per-pixel wobble, then adds per-tile layer heights from the precomputed map tileset.

- For each pixel `(xt, yt)` in the map (going bottom-to-top because bitmaps are bottom-up):
  - Bilinearly sample `heightpattern` to produce `hGround` (scaled 0..255).
  - Look up the tile at that map cell in `tilemapData[cellX, cellY, 0]`.
  - Fetch corresponding row/col in `mapTileset` (where `draw_map_tileset` earlier packed per-layer heights into bytes `[4..4+nbLayers]` and the "bush layer index" at `[4+nbLayers]`).
  - Sum heights for the "occluder" (non-bush) layers into `oGround`, and for all layers into `tGround`.
  - Write:
    - `heightmap[xt, yt, 0] = hGround + tGround` — full solid height for rendering.
    - `heightmap[xt, yt, 1]` (i.e. `heightmapDataBush[xt]`) `= hGround + oGround` — bush/standable height.

The bilinear sampling of the small `heightpattern` (typically 32×32) lets you express global terrain (hills) cheaply.

### 1.3 `drawTextureset(textureHash, colormap, textureAuto)` — ~102 LOC
**What:** For each distinct (non-autotile) tile registered in `textureHash` that has an associated texture bitmap, **splats** that texture into four rotated "strips" in `colormap` — one for each cardinal direction a vertical wall might face — plus a single-byte "dominant direction" channel derived from the original tile colors (red/green/blue/black).

- Walks the Ruby Hash (`tileNum → [tileValueFixnum, textureBitmap, animNbr, animIndex]`).
- For each 32×32 source block, emits:
  - Byte 0 of column 0 = direction code (32/64/96/128 for red/green/blue/black).
  - Columns 1..4 at offsets `k*128` pixels = the texture data, with some mirrored (`31-j`) — one pair is "left" oriented, another "right", for walls on each side.
- Autotile branch: same logic but using the `textureAuto` bitmap (shared autotile texture).

`colormap` ends up as a very wide bitmap (`width = 5*32 = 160`, height proportional to tile count) that subsequent `renderHM7` walls sample vertically via `colormapData = colormapBegin - ((tileIndex*10) << 6) + …`.

### 1.4 `drawHeightmap` pre-requisite: `drawMapTileset(mapTileset, tileset, heightset, tilemapHash, autoTilesets, nbLayers)` — ~105 LOC
**What:** Builds the per-map **compacted 3-layer (or N-layer) tileset bitmap**. For every unique `(layer1,layer2,layer3,bush)` combination used anywhere on the map (precollected on Ruby side into `tilemap_hash`), it produces a 32×32 cell in `mapTileset` that contains:
- Bytes 0..2: BGR of the topmost opaque layer pixel (composited top-down).
- Byte 3: 255 if any layer has an opaque pixel there, else 0.
- Bytes 4..4+nbLayers-1: per-layer height (from `heightset` or per-autotile `autoHeightset`).
- Byte 4+nbLayers: bush value.

Tile values `< 384` are autotiles (48-per-autotile blocks); `>= 384` are tileset tiles. Autotile heights come from separate autotile-heightset bitmaps at `autoTilesets[7..13]`; autotile graphics from `autoTilesets[0..6]`.

The "nbBlocks = (nbLayers + 8) >> 2" factor accounts for how many horizontal 32×32 blocks are needed to pack per-cell metadata: it reserves 4 extra BGRA bytes for layer heights + bush.

### 1.5 `refreshMapTileset(mapTileset, tileset, tilemapHash, autoTilesets, nbLayers)` — ~90 LOC
**What:** Same as `drawMapTileset` but **only refreshes the BGRA color bytes** (not heights, not bush) — used when animated autotiles advance a frame. Does not reset alpha to 255, preserving prior compositing decisions from the full `drawMapTileset` pass.

### 1.6 `applyLighting(heightmap)` — ~100 LOC
**What:** Walks the heightmap **horizontally** (row-by-row, left-to-right) and applies a simulated fixed-direction (east-to-west-ish) **lighting / shadow pass**, writing a signed int8 "shadow delta" into `heightmap[x, y, 1]` (plane 1, alternating entries in flat `SWORD` memory) for each column whose current height rises or falls relative to the running reference height `dyRef`.

- Uses a hard-coded exponential-ish falloff curve `valuesAsc[51]` (precomputed falloffs for distance `dist = 0..50`).
- On an ascending edge (`dy > dyRef`): illuminates the tail of length `dist`.
- On a descending edge of height 1: marks "light descent mode" (`lumDesc = 1`) and keeps walking.
- On a descending edge > 1: casts `shadow = dyRef` so subsequent pixels with `dy < shadow` get `-50` (deep shadow); otherwise applies negative `valuesAsc` to previously-lit tail.
- Only processes rows `0..(ysize*2/3)` — the **ground** region (the last third is the bush region with different semantics).

This produces soft highlights on east-facing slopes and dark shadows in valleys / west-facing cliffs without per-pixel raytracing.

### 1.7 `renderHM7(params, vars, surfaces, nbLayers)` — **~1010 LOC, the main render loop**
**What:** The rasterizer. For each screen row top-to-bottom (actually **bottom-to-top** because heights grow upwards), for each screen column (with 1×/2× horizontal filter step), it:
1. Looks up precomputed map (xs, ys) from the `dataTable` built by `computeM7`.
2. Reads map tile at (xs/32, ys/32), reads ground/bush heights from `heightmap`.
3. Iterates **surfaces** (billboard sprites: characters, events, tile-sprites) that intersect this scanline and rasterizes their vertical columns into a **double-wide scratch buffer** (`sScreenBitmap`, 8 bytes per screen pixel: flag, blend, hbase_hi, hbase_lo, B, G, R, A) with depth-compositing against other surfaces.
4. For each screen row-column, draws a **vertical "wall" strip** from the ground up `dy` pixels, sampling BGR from `colormap` (the wall texture strip, indexed by tile + direction code) on the sides and from `mapTileset` (the top) on the top.
5. Applies per-row lighting (`lux_b/g/r/d`), per-column shadow (`oShadow`), and optional per-pixel blend against the surface scratch buffer (4 blend modes: opaque, normal-alpha, add, subtract).
6. Writes final BGRA to `screenBitmap`.
7. Tracks the highest `ody` so far per column — the final `oCamera` return value is used on the Ruby side for auto-camera vertical-offset adjustment (camera mode 1/2).

Steps in one "tick" (one call):
- **Setup (lines 811–924):** Parse params, allocate `initA/lA/hA/dA` per-layer scratch arrays with `malloc`, initialize screen/lightline/colormap/mapTileset row pointers, compute horizontal stride, decide `step=1` (no filter) / `step=2 x0=0` (even filter) / `step=2 x0=1` (odd filter alternating frames).
- **Main descending loop yt = yMax-1..y0:** cache `lux_*` and `h_coeff` from `lightline` for this row.
  - **PRE-SURFACES pass (lines 960–1201):** only on the very first row, draws any surfaces whose `screenY1` is below the first row, projecting them up to their `sHinit` top. Writes into `sScreenBitmap` scratch.
  - **Column loop xt = xMin+x0..xMax step:** looks up `(xs,ys)` from dataTable, handles out-of-map (with loopX/loopY), reads tile index + layer heights, computes `dy`, `odyh`, `ody`, `shadow`.
    - **SURFACES inline pass (lines 1354–1592):** for each pending surface with `screenX1 ≤ xt < screenX2` and `yt ≤ screenY1`, rasterizes the surface column using fading lookup from `lightline` scratch row -1 (`sCmin/sCmax/sCsl`) and bitmap bilinear scale `sFh = ((sHeight-1)<<10) / (sRealHeight-1)`.
    - **Vertical wall draw (lines 1594–1715):** for `yd = dy; rYt-yd < ym; yd--`:
      - If sScreenData has an opaque surface pixel there, copy from surface buffer, else
      - If we're at the bottom edge and `!noBlack`, draw solid black, else
      - Determine layer & pixel color in `colormap` via `pos` and `lA/hA/dA` scratch arrays, apply lighting+shadow, optionally blend against translucent surface pixel, write to screenBitmap.
    - Write updated `ym` (topmost Y already drawn at this column) to lightline scratch row -2.
- **Final pass (lines 1723–1761):** fill any remaining "above-the-topmost-height" pixels with either surface pixel or transparent (`screenData[3] = 0`).
- `free(initA/lA/hA/dA)`; return `oCamera`.

### 1.8 `applyOpacity(bitmap, opacity)` — ~23 LOC
**What:** In-place multiply of alpha channel by `(opacity+1) / 256`. Straight two-nested-loop pixel walk. `alpha' = alpha * (opacity+1) >> 8`. Used to pre-apply event transparency into per-surface bitmaps on Ruby side.

### 1.9 `applyZoom(bmpId, srcId, lissage)` — ~95 LOC
**What:** Resamples `src` bitmap into `bmp` bitmap at arbitrary scale, using a **fixed-point bilinear** filter when `lissage != 0`, else nearest neighbor. Fixed-point precision is 3 bits (`<< 3` / `>> 3`), so blend weights are 1..8 and the final divisor is `>> 6` (8×8). Alpha-aware: only samples source pixels whose alpha != 0 are weighted in; skipped pixels get weight 0. Note: **this function isn't bound on the Ruby side** in the scripts we've seen — it exists in the DLL but appears to be internal/unused (or used only by later plugin versions). We should port it for completeness but it's low priority.

---

## 2. Data Structures Consumed

### 2.1 `computeM7`: `params` — Array of 16 Fixnums
All values are **Ruby Fixnums** (so `value = *(ptr+i) >> 1` in RGSS1 to strip the tag). Units are either integer pixels or Q12 fixed-point (see §6).

| Idx | Name          | Semantics                                                                  | Format                |
|-----|---------------|----------------------------------------------------------------------------|-----------------------|
| 0   | cosAngle      | `cos(slant)` × 2048                                                        | signed int            |
| 1   | sinAngle      | `sin(slant)` × 2048                                                        | signed int            |
| 2   | altitude      | `distance_h` (camera distance) — NOTE: takes `>> 1` on tag strip twice → actually `distance_h`, not fixed-point | tagged int |
| 3   | pivot         | screen Y of the horizon line (pivot row)                                   | tagged int            |
| 4   | slope         | `slope_value_map` = `(1-z0)/(pivot-h0) * 131072`                           | Q17 (131072=2^17)     |
| 5   | correction    | `corrective_value_map` = `(1-pivot*slope)*131072 / coeff_resolution`       | Q17                   |
| 6   | heightLimit   | horizon Y clip (upper row clip), tagged                                    | tagged int            |
| 7   | cosTheta      | `cos(rotation)` × 2048                                                     | signed int            |
| 8   | sinTheta      | `sin(rotation)` × 2048                                                     | signed int            |
| 9   | distProj      | `distance_p` (projection plane distance), tagged                           | tagged int            |
| 10  | zoom          | `zoom_map` = `(1/zoom_sprites) × 4096`                                     | Q12                   |
| 11  | xMin          | clip rectangle left                                                        | tagged int            |
| 12  | xMax          | clip rectangle right (exclusive)                                           | tagged int            |
| 13  | yMin          | clip top                                                                   | tagged int            |
| 14  | yMax          | clip bottom (exclusive)                                                    | tagged int            |
| 15  | lessCut       | flag (V.1.2.1 `hm7_less_cut`): extends yMax for off-screen bottom elements | tagged bool           |

Note: indices 0, 1, 4, 5, 7, 8 are **not** tagged (they're already scaled into integer space on the Ruby side and passed without `<<1`-tagging — the C code reads them with raw `*ptr` without `>>1`). This is a subtle ABI detail: some entries are Ruby Fixnums (needing `>>1`), some are raw DWORDs masquerading as VALUEs — the Ruby side actually packs them all as Fixnums (positive values, so high bit 0, and the tag bit is bit 0), but the C side is sloppy about which ones to untag. **For our port, pass them as regular `int` arguments or an `RArray` of `FIX2INT`-extracted values without any `<<1`/`>>1` voodoo.**

### 2.2 `renderHM7`: `params` — Array of 17 elements (mixed types)

| Idx | Name             | Ruby Type        | Semantics                                                  |
|-----|------------------|------------------|------------------------------------------------------------|
| 0   | screenBitmap     | Bitmap           | Destination RGBA render target (mkxp-z: `Bitmap`)          |
| 1   | dataTable        | Table            | (xsize, ysize, 2) LUT built by `computeM7`                  |
| 2   | lightline        | Bitmap           | Scratch: row 0 = lighting per row; row -1/-2 = per-column scratch |
| 3   | heightmap        | Table            | Per-pixel height (built by `drawHeightmap` + `applyLighting`) |
| 4   | mapTileset       | Bitmap           | Compact tileset (built by `drawMapTileset`)                |
| 5   | tilemapTable     | Table            | `(w>>3, h, 1)` — each cell packs `[count, layer1, layer2, layer3]` in consecutive x positions (see Ruby init, 210-HM7_NEW_CLASSES.rb:948) |
| 6   | colormap         | Bitmap           | Wall texture strips (built by `drawTextureset`)            |
| 7   | loopX            | Boolean (Fixnum) | Horizontal map looping                                     |
| 8   | loopY            | Boolean          | Vertical map looping                                       |
| 9   | cam              | Fixnum           | Camera mode 0/1/2                                          |
| 10  | sScreenBitmap    | Bitmap           | Scratch for surface compositing (2× screen width because 8 bytes/pixel) |
| 11  | lessCut          | Boolean          | Extend yMax for taller elements at bottom                  |
| 12  | noBlack          | Boolean          | Don't fill bottom-cut pixels with black                    |
| 13  | xMin             | Fixnum           | Clip left                                                  |
| 14  | xMax             | Fixnum           | Clip right                                                 |
| 15  | yMin             | Fixnum           | Clip top                                                   |
| 16  | yMaxDraw         | Fixnum           | Clip bottom for actual drawing                             |

The Ruby code (210-HM7_NEW_CLASSES.rb:512–516) builds this array once and reuses it every frame.

### 2.3 `renderHM7`: `vars` — Array of 5 Fixnums (per-frame state)

| Idx | Name            | Semantics                                         |
|-----|-----------------|---------------------------------------------------|
| 0   | heightLimit     | horizon Y (same as params[6] in computeM7)        |
| 1   | displayX        | `$game_map.display_x >> 3 + offset_x_res`         |
| 2   | displayY        | `$game_map.display_y >> 3 + offset_y_res`         |
| 3   | filter          | 0=no filter / 1=even columns / 2=odd columns      |
| 4   | oScrY           | Camera Y offset (auto-updated from `renderHM7` return value) |

### 2.4 `renderHM7`: `surfaces` — Array of Arrays; each surface = Array of 11 elements

Built by sorting on the Ruby side (`210-HM7_NEW_CLASSES.rb:781`: `sort {|a,b| b[2]-a[2]==0 ? a[1]-b[1] : b[2]-a[2]}` — descending Y, ascending X).

A single surface record from `HM7::Surface#get_data` + extras appended at call time:

| Idx | Name         | Source                      | Semantics                                |
|-----|--------------|-----------------------------|------------------------------------------|
| 0   | type         | `surface.type`              | 0 = normal (use lightline col 0,1); 2 = tile-like (use col 2,3) |
| 1   | screenX1     | `surface.screen_x - w/2`    | Left edge on screen                      |
| 2   | screenY1     | `surface.screen_y`          | Bottom edge on screen                    |
| 3   | screenX2     | `surface.screen_x + w/2`    | Right edge                               |
| 4   | screenY2     | second reference Y          | Used to compute trapezoidal scale `sSlope = (sY1-sY2 << 7) / (sX2-sX1)` |
| 5   | sInverse     | bool                        | Flip horizontal sampling direction        |
| 6   | bitmap       | Bitmap                      | Sprite bitmap (already opacity-applied)   |
| 7   | sDh          | Fixnum (altitude)           | Height offset above ground                |
| 8   | sBlend       | Fixnum                      | 0=normal, 1=add, 2=sub                    |
| 9   | sDispWidth   | Fixnum                      | Horizontal crop/source width              |
| 10  | sDispOffset  | Fixnum                      | Horizontal crop/source offset             |

The Ruby side `get_data` (210-HM7_NEW_CLASSES.rb:338) currently returns a 6-element array — the 11-element layout is what the C code expects post-plugin-upgrade. For the port, we need to canonicalize this.

### 2.5 `drawMapTileset`: `tilemapHash`
`Hash` mapping **tileCombinationID (Fixnum)** → `[layer1, layer2, ..., layerN, bush]` (Array of Fixnums). N = `nbLayers` (default 3).

### 2.6 `drawMapTileset`: `autoTilesets`
`Array` of 14 elements: `[autotile0_gfx, autotile1_gfx, ..., autotile6_gfx, autotile0_hmap, autotile1_hmap, ..., autotile6_hmap]`. Entries are either `Bitmap` or `0` (Fixnum zero, meaning no autotile in that slot).

### 2.7 `drawTextureset`: `textures`
`Hash` mapping **tileID (Fixnum)** → `[tileValue, textureBitmap, animNbr, animIndex]`. The plugin will skip entries where `textureBmp` is null.

### 2.8 `drawHeightmap`: `heightpattern`, `mapTileset`, `tilemapData`
- `heightpattern`: small bitmap (typically 32×32 RGBA, red channel = height). If absent, Ruby passes a fresh `Bitmap.new(32, 32)` (all zero).
- `mapTileset`, `tilemapData`: as in §2.5–§2.6 but already built.

---

## 3. Pixel Format Assumptions

### 3.1 Byte order
The code **unconditionally** reads `data[0]=blue, data[1]=green, data[2]=red, data[3]=alpha`. This matches **Windows DIBs** (BGRA in memory on LE x86) and **SDL's `SDL_PIXELFORMAT_ARGB8888`** when accessed byte-by-byte on little-endian (which iOS is: ARM64 is LE).

- mkxp-z's `Bitmap` typically exposes its backing `SDL_Surface` or raw GL-uploaded texture. mkxp-z uses `GL_RGBA` internally (rRGBA, not BGRA). **This is a critical discrepancy.**
- **Options for the port:**
  a. Swap to RGBA byte order in the port (rewrite all indices: `data[0]=red, data[2]=blue`).
  b. Keep BGRA order and do a channel swap when uploading to GL.
  c. Use a dedicated `SDL_Surface*` scratch in BGRA format for the plugin's working bitmaps (lightline, mapTileset, colormap, screenBitmap) and blit once to mkxp-z's Bitmap at the end.
- **Recommendation:** option (a). Rewrite all `[0]/[1]/[2]` indices during the port to match mkxp-z's RGBA layout. It's mechanical and eliminates one copy per frame.

### 3.2 Scanline order
RGSS1 bitmaps are **bottom-up DIBs**: `firstRow` is the **last** row of the image in memory, and to go up one row you **subtract** `rowSize`. Notice everywhere in the source:
```
lightlineData = firstLightlineRow - ys * patternRowSize + …
```
Every `-` should be a `+` if we adapt to mkxp-z's top-down `SDL_Surface`. **Or** we emulate bottom-up by computing `firstRow = pixels + (h-1)*pitch` and then the `-` stays. Either way, the whole codebase needs a pass to re-derive all the row-arithmetic signs.

- **Recommendation:** use top-down, change `-` to `+` systematically. Document invariant `rowPtr(y) = pixels + y * pitch`.

### 3.3 Alpha / sRGB
- The plugin assumes **straight (non-premultiplied)** alpha, and **linear-space** blending (no gamma correction). mkxp-z also uses straight alpha, so this is fine. No sRGB conversion is required.
- `renderHM7` and `applyZoom` treat alpha as an independent channel; they never premultiply. `applyOpacity` only touches alpha.

### 3.4 Row alignment / pitch
- RGSS1 `biWidth` is a DWORD and the pitch is always `biWidth * 4` (no 4-byte DIB row padding because the width is always a multiple of 1 and 4-byte pixels self-align). mkxp-z uses `SDL_Surface::pitch` which may or may not equal `w*4`. **Must use `pitch` not `w*4`.**

### 3.5 Special "negative-row" scratch pattern
Multiple places in `computeM7` and `renderHM7` access `firstRow - rowSize` (one row **above** the top of the bitmap). This is **only valid if the Ruby side allocates the bitmap 2 rows taller than needed**, treating rows -1 and -2 as scratch.

Looking at the Ruby code: `@rowsdata = Bitmap.new(@render.width, 3)` — yes, the bitmap is allocated **3 rows tall** precisely so that rows "0..2" exist but the plugin indexes them as "0, -1, -2" relative to the bottom row (the DIB's "firstRow"). This is a bottom-up-DIB convention. When ported to top-down, row 0 remains the horizon scratch and rows 1 and 2 become the "above" scratch. **Translate carefully.**

---

## 4. Per-Function Complexity / LOC Estimates

| Function           | Source LOC | Est. Port LOC (C++/Obj-C++) | Notes                                  |
|--------------------|------------|------------------------------|----------------------------------------|
| `computeM7`        | 119        | ~180                         | +validation, +replace Array access     |
| `drawHeightmap`    | 83         | ~130                         | Bilinear helper inlined                |
| `drawTextureset`   | 102        | ~150                         | Hash iteration via `rb_hash_foreach`   |
| `applyLighting`    | 100        | ~130                         | Straightforward; precomputed table is trivial |
| `drawMapTileset`   | 105        | ~160                         | Hash iter, 7 autotiles unpacked        |
| `refreshMapTileset`| 90         | ~130                         | Same shape, no heights                 |
| `renderHM7`        | **1010**   | **~1400**                    | Should be split into 5–6 helper methods for maintainability |
| `applyOpacity`     | 23         | ~45                          | Trivial                                |
| `applyZoom`        | 95         | ~140                         | Bilinear filter, skip-if-alpha-zero logic |
| **Shared helpers** | ~40        | ~250                         | Ruby binding glue, Bitmap/Table accessors, channel swap, bounds checks |
| **Total**          | **~1770**  | **~2700**                    | Excluding tests                        |

`renderHM7` should be decomposed along the structural boundaries already visible:
- `render_pre_surfaces(...)` — lines 960–1201
- `render_surface_column(...)` — lines 1358–1592 (shared with pre-surfaces — extract the inner surface loop)
- `render_wall_column(...)` — lines 1594–1715
- `render_final_overdraw(...)` — lines 1723–1761
- `compute_surface_metrics(...)` — the 20-line header that's repeated twice for surface setup

---

## 5. Shared Helpers & Macros

### 5.1 `#define DIVISE(a, b) ((b)!=0?((a)/(b)):(0))`
Safe division returning 0 on divide-by-zero. Used many times in `computeM7`. **Port as `static inline int divise(int a, int b)`** with the same semantics.

### 5.2 `#define RS(target, source, shift)` (arithmetic right-shift fix-up)
Defined but **never used** in the file. Do not port.

### 5.3 `valuesAsc[51]` — static const char[51]
The exponential-ish lighting falloff table in `applyLighting`. Port verbatim.

### 5.4 `Data_Patterns` (Ruby side — 210-HM7_NEW_CLASSES.rb:110–119)
Autotile 4-corner pattern LUT (48 entries × 4 ints). Only used in Ruby; no C port needed.

### 5.5 RGSS1 tag-stripping pattern `>>1` / `<<1`
Every C access reads a raw Fixnum VALUE as `val = tagged >> 1`, and re-derives a pointer from a Fixnum `id` as `p = (RGSSBITMAP*)(id << 1)`. **In the port, these vanish** — we receive Ruby `VALUE`s directly, unwrap `FIX2INT`/`FIX2LONG` for integers, and use `TypedData_Get_Struct` (or mkxp-z's `getPrivateData`) for bitmap/table pointers.

### 5.6 Row address helper
Every function reconstructs `firstRow ± y * rowSize + x * 4` inline. **Port as `static inline uint8_t* pixel_at(Bitmap* bmp, int x, int y)`** to centralize the row-direction + pitch handling and make the codebase self-checking.

### 5.7 Per-layer scratch arrays `initA, lA, hA, dA`
In the source, these are `malloc`'d each call and `free`'d at the end, sized `nbLayers`. **For port, use `std::vector<…>` or a stack array `char initA[8]`** (nbLayers is bounded by ~7 in practice).

---

## 6. Math Notes

### 6.1 Mode-7 projection (in `computeM7`)

For each screen row `yt` (0 at top, positive going down):
- World-Y projection (before rotation):
  ```
  yp = pivot + (altitude * (yt - pivot)) / ((altitude - distProj) * cosAngle + (yt - pivot) * sinAngle >> 12) * zoom >> 12
  ```
  Fixed-point: `cosAngle/sinAngle` are ×2048 (Q11), `zoom` is Q12, pivot/altitude/distProj are integer pixels.
- Horizontal "distance to camera" per row: encoded through the `val_1 = slope * yt + correction` where slope is Q17 and correction is Q17/coeff_resolution.
- Per-column X projection:
  ```
  xp = zoom * ((a - xt) << 18) / val_1 >> 12
  ```
  Note the `<<18 / val_1` implicitly divides by `2^17` (slope fixed-point) leaving `(a-xt)*2^18 / (slope*yt + corr)`, which is just `(a-xt) * 2 / slope_reality`, i.e. a linear horizontal projection at distance `yt`. The `* zoom >> 12` then scales by the user zoom.
- 2D rotation around `(a, pivot)` with `cosTheta/sinTheta` Q11.

**Summary:** the projection is a standard mode-7 affine homography written in pure integer Q-math. Three Q formats coexist: Q11 (trig), Q12 (zoom), Q17 (slope). No floats, no sqrt.

### 6.2 Heightmap sampling (in `drawHeightmap`)
Bilinear in a `10×10` sub-pixel grid (note `yc = (yt*patternHeight*10)/mapHeightPx; ys = yc/10; yr = yc-10*ys`). Weights are integer 0..10 and the final divisor is 100 for a 2D bilinear. Fixed-point, not floating. Edge cases (right/bottom border) fall back to 1D or nearest sampling.

### 6.3 Bilinear filtering (in `applyZoom`)
3-bit sub-pixel precision (`<< 3`). Weights in `[0, 8]`. Final divisor `>> 6` = 64 = 8×8. Alpha-aware (samples with `a==0` get zero weight; final alpha computed the same way).

### 6.4 Surface rasterization (in `renderHM7`)
For a surface with top at `(screenX1, screenY1)` and vanishing-line point `(screenX2, screenY2)`:
- Per-column slope: `sSlope = (sDy << 7) / sDx` (Q7).
- Per-column perspective correction derived from `sCmin/sCmax/sCsl` — these come from `lightline` row-1 horizontal-zoom coefficients, giving **a proper perspective projection** (not just linear interpolation). The formula:
  ```
  dx1 = (sXt - screenX1 << 12) / sC1
  dx2 = (screenX2 - 1 - sXt << 12) / sC2
  sX  = sDispOffset + (sDispWidth * dx1) / (dx1 + dx2) << 2
  ```
  is a classic rational (perspective-correct) horizontal texture mapping.
- Per-column vertical scale: `sFh = ((sHeight-1) << 10) / (sRealHeight-1)` — a Q10 fractional step through the source bitmap per screen pixel.

### 6.5 Lighting math
Per-row attenuation `ypl = pivot - yp`: the farther (higher on screen) the brighter. Scaled by `/960` against the user's fog color. Shadow pass uses the `valuesAsc[51]` LUT.

---

## 7. Threading / State

### 7.1 Mutable global state
**None.** There are **no file-scope mutable variables**. The only file-scope const is `valuesAsc[51]` in `applyLighting`, and that's declared `static const`. Every function is re-entrant in principle; all state lives in the Ruby side (`@heightmap`, `@map_tileset`, etc., managed by `HM7::Tilemap` and `HM7::Cache`).

### 7.2 Caches
The **Ruby side** (`HM7::Cache`, 210-HM7_NEW_CLASSES.rb:1005–1038) caches per-map-ID the fully-built heightmap, mapTileset, tiletable, textureset, autotiles, and animated-tilemap-hash. This is a straightforward Hash keyed by `map_id`. `HM7::Cache.clear` wipes it on map transitions and calls `GC.start`.

**For the port:** leave the cache on the Ruby side. No C-side caching is necessary.

### 7.3 Threading
- The DLL is called from the RGSS main thread only.
- mkxp-z's Ruby calls also happen on the game thread; but mkxp-z uses SDL which may off-thread bitmap uploads. **Key concern:** between calling `renderHM7` (which writes raw CPU bytes into `screenBitmap`) and the next GL upload, we must ensure the GPU texture is invalidated (call `Bitmap#taintBitmap` or whatever mkxp-z exposes) so that the next draw uses the new pixels.

### 7.4 Per-call scratch allocations
`renderHM7` does four `malloc`s of `nbLayers * sizeof(X)` bytes and frees them at the end. With `nbLayers==3` that's 4 tiny allocations per frame — negligible but **trivially improved by moving them to the stack** (`char initA[MAX_LAYERS=8]`) or a single persistent scratch buffer held in the plugin struct.

---

## 8. External Dependencies

### 8.1 Windows APIs used
- **Types only** from `<windows.h>`: `DWORD`, `LPBYTE`, `RGBQUAD`, `BITMAPINFOHEADER`. **No function calls.** Replace all with `uint32_t`, `uint8_t*`, a trivial `struct { uint8_t b,g,r,a; }`, and nothing (we don't need `BITMAPINFOHEADER` — just width/height/pitch from our own Bitmap wrapper).

### 8.2 GDI
**None.** The plugin never calls any GDI/DirectDraw/Direct3D function. All drawing is manual pixel pokes.

### 8.3 CRT
- `<stdio.h>` — included but **not used** (no printf/fopen).
- `<stdlib.h>` — `malloc`, `free` only (4 calls in `renderHM7`).
- `<string.h>` — included but **not used** (no memcpy/memset).
- `<math.h>` — included but **not used** (all trig is precomputed on the Ruby side).

### 8.4 Ruby C API (RGSS1)
- **Direct struct-casting from Fixnum `__id__` values.** No `rb_…` functions called from C. This is the **entire ABI-compatibility surface** that must be reimagined for Ruby 3.1 MRI.
- Structs assumed: `RBasic`, `RArray` (with `long len`, `VALUE *ptr`), `RHash` (with `st_table *tbl`), `st_table` (with `num_bins`, `bins`), `st_table_entry` (with `key`, `record`, `next`), `RTable` (RGSS-specific, not MRI), `RGSSBITMAP` (RGSS-specific).
- **Ruby 3.1 layout differences:**
  - `RArray`: now uses embedded storage for small arrays (`ary_embed_LEN`). **Must use `RARRAY_LEN` / `RARRAY_AREF` / `RARRAY_CONST_PTR` macros.**
  - `RHash`: `st_table` is open-addressed since 2.6. Bin-chain walking is impossible. **Must use `rb_hash_foreach(hash, callback, arg)`.**
  - `RTable`: RPG Maker-specific, mkxp-z provides its own C++ class (`Table`) exposed via `TypedData`. **Use `TypedData_Get_Struct(value, Table, &Table_type, tableptr)` or mkxp-z's helper.**
  - `RGSSBITMAP`: RPG Maker-specific. mkxp-z provides `Bitmap` class. **Use mkxp-z's `Bitmap` accessor; acquire raw `SDL_Surface*` pixels or the `uint8_t*` pointer.**
- **Fixnum tagging:** Ruby 3.1 still uses the same `RUBY_FIXNUM_FLAG = 1` (low bit) tag for 63-bit Fixnums on 64-bit. `FIX2INT` / `FIX2LONG` / `INT2FIX` handle it. **Never use `>>1` / `<<1` directly.**

### 8.5 C++ standard library
None. The source is pure C.

---

## 9. Port Strategy (summary)

1. **Binding layer:** replace the nine `Win32API.new` sites with `rb_define_module_function("hm7", "drawMapTileset", ...)` etc. Ruby-side wrappers (`HM7.draw_map_tileset` etc., 210-HM7_NEW_CLASSES.rb:53–101) should be re-pointed to the new functions; the `.__id__` dance is dropped.
2. **Accessor shims:**
   - `bitmap_raw_pixels(VALUE bmp) -> uint8_t*, int w, int h, int pitch` using mkxp-z's `Bitmap` + lock/unlock semantics (we need the Bitmap in BGRA-or-RGBA CPU memory; mkxp-z has `raw_data`/`pixels` accessors internally).
   - `table_raw_data(VALUE tbl) -> int16_t*, int xs, int ys, int zs` using mkxp-z's `Table`.
   - `hash_each(VALUE h, callback, arg)` via `rb_hash_foreach`.
3. **Pixel-order audit:** one pass to flip all `data[0]`/`data[2]` if we switch to RGBA.
4. **Row-direction audit:** one pass to flip all `firstRow - y*pitch` to `pixels + y*pitch` if we switch to top-down.
5. **Scratch-row validation:** verify that the Ruby side still allocates `@rowsdata` as a 3-tall bitmap and translate the "row -1 / row -2" scratch into "row 1 / row 2" (top-down convention).
6. **GL sync:** after each call that writes into a mkxp-z `Bitmap`, call the appropriate "pixel data changed" notification so the GL texture is refreshed on next draw.
7. **Split `renderHM7`** into the five helper functions outlined in §4 during the port — do not preserve the monolithic layout.
8. **iOS note:** ARM64-LE matches the BGRA-in-memory assumption byte-wise; no endianness porting is needed. No 32-bit integer assumptions are dangerous — all multiply-shift patterns fit in `int32_t` given practical input sizes (screen ≤ 640×480, map ≤ 200×200).
9. **Test strategy:** render a reference frame on the Windows DLL (capture the final `screenBitmap` as PNG), feed the same inputs through the port, and `pHash`/`pixel-diff` the outputs. The functions are deterministic integer math, so bit-exact agreement is achievable.

---

## 10. Quirks, Gotchas & Open Questions

1. **`drawMapTileset`, `drawTextureset`, `refreshMapTileset`, `drawHeightmap` take an `nbLayers` argument in C but the Ruby binding declares `"llll"` / `"lll"` / `"llll"` / `"llll"`** (no `nbLayers` argument passed from Ruby). The C code reads `nbLayers` from the ABI anyway, which on x86 `__stdcall` means it reads **garbage from the stack**. Looking at the Ruby code, `nbLayers` is never passed; yet the C code defaults it to `*(ptr + nbLayers)`. **Hypothesis: later versions of the plugin dropped the explicit `nbLayers` and hardcoded N=3 in the Ruby configuration.** For our port, **hardcode `nbLayers=3`** and verify against the scripts we have (which indeed pass 3-element layer data everywhere). The `renderHM7` and `compute_m7` bindings are `"lll"` (3 longs) — no nbLayers. Take nbLayers as a compile-time constant.

2. **Alpha channel convention in mapTileset byte 3.** `drawMapTileset` sets byte 3 to 255 on any opaque pixel; `refreshMapTileset` preserves whatever was there. In `renderHM7`, byte 3 (`alpha = mapTilesetData[3]`) is read and written into the final screen pixel. So the alpha is a per-tile "something is drawn here" mask, not a transparency gradient. Preserve this.

3. **Negative shadow value `-50`** in `applyLighting` and read back as `oShadow` in `renderHM7` (line 1327). This is a signed 16-bit value stored in plane 1 of the heightmap (`heightmap[x,y,1]`). When `oShadow != 0`, the wall-top or ground pixel gets `blue += oShadow; green += oShadow; red += oShadow;` clamped. With `oShadow = -50`, that darkens by 50. **Preserve signed semantics of the Table's int16 plane 1.**

4. **`renderHM7` pre-surface pass runs only on the first iteration** (`if (!(yMax - 1 - yt))`, line 960). This is the "flush surfaces below the first drawn row" logic. Easy to miss when refactoring.

5. **`sScreenBitmap` is 2× the screen width.** It's 8 bytes per screen pixel (`sScreenRowSize = screenRowSize << 1`). Layout of those 8 bytes: `[0]=flag (1 if surface pixel present), [1]=blendMode, [2]=hbaseHi, [3]=hbaseLo, [4..6]=BGR, [7]=opacity`. Ruby side allocates it as `Bitmap.new(@render.width, @render.height)` — but the C code treats every row as 8-byte pixels by using `sScreenRowSize = screenRowSize << 1`. **This means the Ruby-side bitmap must actually be 2× width, or `renderHM7` reads past the edge.** Looking at 210-HM7_NEW_CLASSES.rb:515: `Bitmap.new(@render.width, @render.height)` — that's 4 bytes/pixel × width × height bytes total. The C code reads `(xt << 3) + 7` which can go up to `(screenWidth-1)*8 + 7 = 8*screenWidth - 1` bytes from row start — but the bitmap only has `4*screenWidth` bytes per row. **This is a latent buffer overflow in the original unless the Ruby side allocates `Bitmap.new(2*@render.width, @render.height)`.** The Ruby side as provided uses `@render.width` × `@render.height` — we should **double the width for the port**. Verify by instrumenting the Windows DLL; if it works there, it's because the DIB row size has padding that happens to cover the overread on typical widths, or the fault is masked. **Safest to allocate 2× width.**

6. **"lightline" row indexing.** Three conceptual rows:
   - Row 0: per-screen-Y lighting (B,G,R,flag).
   - Row -1 (one above firstRow in BG-DIB = row 1 in top-down): per-column relief coefficient (`val_5`) and horizontal zoom data — two 16-bit values packed as 4 bytes.
   - Row -2 (two above firstRow = row 2 in top-down): per-column topmost-drawn-Y (`ym`), packed as two bytes.
   The Ruby side allocates `Bitmap.new(@render.width, 3)` so all three rows exist. **Port must match: top-down row 0 = lighting, row 1 = horizontal zoom, row 2 = per-column ym (or pick a consistent convention).**

7. **`screenY1 / screenY2` are `short` (16-bit signed).** In `renderHM7` the surface coord variables are all 16-bit shorts. Verify the Ruby side never passes values outside `[-32768, 32767]`. With a screen height of 480, this is comfortable.

8. **`applyZoom` is not bound on the Ruby side** in the scripts we have. It exists in the DLL. Port it for future-proofing but it's not on the critical path.

9. **`compute_m7` is called synchronously during `refresh_alpha/theta/zoom/fading`** (210-HM7_NEW_CLASSES.rb:571, 622, 708). These are user-input-triggered so latency spikes are acceptable.

10. **`render_hm7` returns an int** used as `@target_y` for camera auto-offset (210-HM7_NEW_CLASSES.rb:759). Ensure the port returns `oCamera` correctly via `INT2FIX`/`LONG2FIX`.

---

## 11. Suggested Port File Layout

```
src/hm7/
  hm7_plugin.cpp          -- rb_define_module_function glue, entry point
  hm7_bindings.cpp        -- argument unpack, VALUE → (Bitmap*, Table*, etc.)
  hm7_compute_m7.cpp      -- computeM7 + helpers
  hm7_draw_heightmap.cpp  -- drawHeightmap
  hm7_draw_textureset.cpp -- drawTextureset
  hm7_apply_lighting.cpp  -- applyLighting + valuesAsc table
  hm7_draw_tileset.cpp    -- drawMapTileset + refreshMapTileset
  hm7_render.cpp          -- renderHM7 main dispatcher
  hm7_render_surface.cpp  -- render_surface_column, render_pre_surfaces
  hm7_render_wall.cpp     -- render_wall_column, render_final_overdraw
  hm7_apply_opacity.cpp   -- applyOpacity
  hm7_apply_zoom.cpp      -- applyZoom
  hm7_pixels.h            -- pixel_at(), row_ptr(), channel order macros
  hm7_math.h              -- DIVISE, fixed-point helpers
```

Ruby side:
- Replace `Win32API.new` calls in `210-HM7_NEW_CLASSES.rb:41–49` with direct `HM7.draw_map_tileset(...)` calls backed by our C extension.
- Remove every `.__id__` in `HM7.draw_map_tileset` / `HM7.render_hm7` / etc.
- `sScreenBitmap` allocation: change to `Bitmap.new(@render.width * 2, @render.height)` (see §10.5).
- `get_data` (210-HM7_NEW_CLASSES.rb:338): expand from 6 to 11 elements matching §2.4.

---

## 12. Risk Assessment

| Risk                                                        | Severity | Mitigation                                             |
|-------------------------------------------------------------|----------|--------------------------------------------------------|
| Pixel-order / row-order bugs yielding garbled output        | High     | Do audit in §3 / §5.6 first, test with single function |
| `sScreenBitmap` overflow (§10.5)                            | High     | Double the width on Ruby side                          |
| Fixed-point overflow on ARM64 with 64-bit `long`            | Medium   | Force `int32_t` in hot paths to preserve Win32 `int` semantics |
| Performance regression (software rasterizer is heavy)       | High     | Profile on iPhone; potentially fall back to lower res; consider NEON for the inner loops in renderHM7 wall/surface columns |
| Hash iteration order differences (Ruby 3.1 ordered hashes)  | Low      | Code doesn't depend on iteration order                 |
| `rb_hash_foreach` re-entrancy with allocations              | Low      | Standard Ruby pattern, works fine                      |
| Bitmap GL texture not resyncing after CPU writes            | Medium   | Call mkxp-z's `taintArea` / pixel-invalidate after each write-out |
| Table plane-stride assumptions                              | Low      | mkxp-z's Table layout is documented: `data[z*xs*ys + y*xs + x]`, same as RGSS |

---

## End

This completes the analysis. Next step: prototype the binding layer + `applyOpacity` (the simplest function) end-to-end on iOS to validate the Bitmap-CPU-access story, then `computeM7`, then build up to `renderHM7`.
