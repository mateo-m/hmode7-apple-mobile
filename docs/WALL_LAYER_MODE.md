# H-Mode7 Wall-Layer Selection Mode

The port supports two algorithms for the wall-loop's layer-selection
step. The algorithm picks WHICH tile-layer's colormap a given wall
pixel samples (strip 1..4 for the chosen direction), which in turn
decides whether multi-layer tiles (e.g. ground + wall + roof) show
each layer's texture at its correct vertical band on the extruded
wall.

## Rationale

The MGC H-Mode7 engine went through two DLL rewrites relevant to
this algorithm choice:

- **V1.3** *("wall events: the class HM7::Surface has some major
  changes, and the DLL part is entirely rewritten")* — this rewrite
  targeted **wall events** (sprite-like walls attached to map
  events), which is a completely separate system from tile-layer
  wall extrusion. Does NOT change the tile-layer wall algorithm.
- **V1.4** *("can now handle n layers (but the more layers, the
  more lag)")* — this is the rewrite that generalized the
  layer-handling code from hardcoded 3 layers to configurable n.
  The bottom-cumulative threshold seen in the public V1.4.4 source
  most likely originates here.

So the algorithmic split for wall-layer selection is **V1.4**, not
V1.3. The port's autodetection reflects that.

Empirical evidence:

- **Pokemon Insurgence 1.2.7** (`MGC_Hmode7.dll`, MD5
  `d8e5b905ea25664a104058699db4344e`, 8 exports, 2011-05-15) —
  a pre-V1.3 DLL. Top-cumulative makes its buildings render
  correctly; bottom-cumulative shows them as uniform teal blocks
  with no facade.
- **MGC's public V1.4.4 source** (`MGC_Hmode7_1_4_4.cpp`, 9
  exports) — uses bottom-cumulative in `renderHM7`.

Both loops iterate `itLayer` from top to bottom with a `break`-on-
match, but the threshold they check against differs:

- **Pre-V1.4**: accumulates the current layer's height from the
  top. For each layer, checks `dy - yd <= cum_from_top`. The first
  layer whose band contains the wall-pixel depth wins. Multi-layer
  tiles naturally expose each layer's colormap in its own vertical
  band.

- **V1.4+**: uses the layer's `dA` entry, where `dA` is
  *bottom-cumulative* (sum of all layer heights up to and including
  that layer). In the common case `dy == dA[top]` (no ground
  heightmap bump), `dy - yd <= dA[top]` is trivially true for any
  wall pixel, so the loop always picks the topmost layer at
  iteration 0 and exits. Only the top layer's colormap is ever
  consulted. For tiles where the roof's colormap is transparent
  over the wall area (which Insurgence's tiles usually are), walls
  fall through to the flat tile-color path and render as uniform
  columns — "inelegant vertical walls" in MGC's own words, as if
  no texture file existed.

Whether the v1.4.4 behaviour is intentional or a regression is
unknown. It's faithfully reproduced by the public source, but the
degenerate loop is a red flag. Either way: any game shipping the
pre-V1.4 DLL depends on the top-cumulative behaviour, while any
game shipping the V1.4+ DLL depends on whatever the V1.4+ DLL does.

## Configuration

The port defaults to **top-cumulative** (pre-V1.3 behaviour) because
that is what Pokemon Insurgence — the primary driver of this port —
depends on.

Games that actually run a v1.4+ DLL on Windows can opt into the
reference behaviour by assigning any of these symbols to
`HM7::Native::WALL_LAYER_MODE` in a postload script:

```ruby
HM7::Native::WALL_LAYER_MODE = :bottom_cumulative
# aliases also accepted:
# HM7::Native::WALL_LAYER_MODE = :v1_4
# HM7::Native::WALL_LAYER_MODE = :reference
```

Aliases for explicit "top-cumulative" are also allowed but not
needed in practice since that's the default:

```ruby
HM7::Native::WALL_LAYER_MODE = :top_cumulative   # the default
```

The binding looks up the constant on every `render_hm7` call, so
it can even be toggled mid-game (e.g. per-map) if some strange
scenario calls for it.

## Auto-detection

The postload shim (`scripts/postload/hmode7_shim.rb`) auto-detects
the era based on the presence of `HM7.apply_zoom`, which is the
Ruby wrapper around the 9th DLL export (`applyZoom`) added in V1.4:

```ruby
detected_mode = HM7.respond_to?(:apply_zoom) ?
                  :bottom_cumulative :  # V1.4+
                  :top_cumulative       # pre-V1.4
```

The detection runs ONCE at shim install time and stores the
result in `HM7::Native::WALL_LAYER_MODE`. Games that want to
override (for example a fork that uses pre-V1.4 scripts but
added an `apply_zoom` wrapper that wraps a stub) can assign the
constant directly in another postload script:

```ruby
HM7::Native.send(:remove_const, :WALL_LAYER_MODE) \
  if HM7::Native.const_defined?(:WALL_LAYER_MODE)
HM7::Native.const_set(:WALL_LAYER_MODE, :top_cumulative)
```

If a future game turns up where this heuristic picks wrong, the
first fallback to try is checking for V1.4 layer-count features
(e.g. whether `HM7::Tilemap` computes `nb_layers` dynamically vs
uses a hardcoded 3), or parsing the version string from the HM7
Ruby script's header comment.
