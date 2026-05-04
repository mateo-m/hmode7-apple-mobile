# H-Mode7 wall-layer selection mode

The wall-loop in `renderHM7` picks one of a tile's vertical layers to sample (strip 1..4 for the chosen direction). Multi-layer tiles like ground + wall + roof depend on this pick to show each layer's texture in its correct vertical band on the extruded wall.

The port supports two algorithms for that pick. They diverge on which threshold the loop checks.

## The two algorithms

Both loops walk `itLayer` from top to bottom, breaking on the first match. The threshold differs:

- **Top-cumulative (pre-V1.4).** Accumulates layer height from the top. For each layer, checks `dy - yd <= cum_from_top`. The first layer whose band contains the wall-pixel depth wins. Multi-layer tiles surface each layer's colormap in its own vertical band.

- **Bottom-cumulative (V1.4+).** Uses `dA[layer]`, the sum of all layer heights up to and including that layer. In the common case `dy == dA[top]` (no ground heightmap bump), `dy - yd <= dA[top]` is true for any wall pixel, so the loop picks the topmost layer at iteration 0 and exits. Only the top layer's colormap ever gets sampled. When the roof's colormap is transparent over the wall area (typical of pre-V1.3-era tilesets), walls fall through to the flat tile-color path and render as uniform columns. MGC called these "inelegant vertical walls" in the original release notes.

Whether V1.4.4's behaviour is intentional or a regression isn't clear. The public source faithfully reproduces it, but the loop degenerates in the common case. A pre-V1.4 game depends on top-cumulative; a V1.4+ game depends on whatever V1.4+ does.

## Why the split lands at V1.4

MGC published two relevant DLL rewrites:

- **V1.3** rewrote the wall-events system (`HM7::Surface`, sprite-like walls attached to map events). That's a separate system from tile-layer wall extrusion and doesn't touch this algorithm.
- **V1.4** generalized layer handling from a hardcoded 3 layers to configurable `n` (with the trade-off MGC noted: "the more layers, the more lag"). The bottom-cumulative threshold visible in the public V1.4.4 source most plausibly originates here.

The port's auto-detection uses V1.4 as the boundary.

## Empirical evidence

Two data points fixed the design choice:

- A pre-V1.3 DLL (`MGC_Hmode7.dll`, MD5 `d8e5b905ea25664a104058699db4344e`, 8 exports, 2011-05-15) shipped by an RGSS1 fan game widely used as the port reference. Top-cumulative renders its buildings correctly. Bottom-cumulative shows them as uniform teal blocks with no facade.
- MGC's public V1.4.4 source uses bottom-cumulative in `renderHM7`.

## Configuration

The port defaults to **top-cumulative**. The reference RGSS1 fan game ships a pre-V1.3 DLL and its tilesets expect that algorithm.

A V1.4+ game can opt into the reference behaviour from a postload script:

```ruby
HM7::Native::WALL_LAYER_MODE = :bottom_cumulative
# Aliases:
# HM7::Native::WALL_LAYER_MODE = :v1_4
# HM7::Native::WALL_LAYER_MODE = :reference
```

`:top_cumulative` is the default and rarely needs setting explicitly:

```ruby
HM7::Native::WALL_LAYER_MODE = :top_cumulative
```

The binding reads the constant on every `render_hm7` call, so toggling per-map mid-game is fine if some scenario calls for it.

## Auto-detection

The postload shim at `scripts/postload/hmode7_shim.rb` checks for `HM7.apply_zoom`, the Ruby wrapper around the 9th DLL export (`applyZoom`, added in V1.4):

```ruby
detected_mode = HM7.respond_to?(:apply_zoom) ?
                  :bottom_cumulative :  # V1.4+
                  :top_cumulative       # pre-V1.4
```

Detection runs once at shim install time and stores the result in `HM7::Native::WALL_LAYER_MODE`. A game can override that value from another postload script:

```ruby
HM7::Native.send(:remove_const, :WALL_LAYER_MODE) \
  if HM7::Native.const_defined?(:WALL_LAYER_MODE)
HM7::Native.const_set(:WALL_LAYER_MODE, :top_cumulative)
```

If a future game breaks the heuristic, two fallback signals to try first:

- Whether `HM7::Tilemap` computes `nb_layers` dynamically vs. uses a hardcoded 3.
- The version string in the H-Mode7 Ruby script header comment.
