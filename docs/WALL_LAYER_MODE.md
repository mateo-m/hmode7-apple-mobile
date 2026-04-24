# H-Mode7 Wall-Layer Selection Mode

The port supports two algorithms for the wall-loop's layer-selection
step. The algorithm picks WHICH tile-layer's colormap a given wall
pixel samples (strip 1..4 for the chosen direction), which in turn
decides whether multi-layer tiles (e.g. ground + wall + roof) show
each layer's texture at its correct vertical band on the extruded
wall.

## Rationale

The MGC H-Mode7 engine went through two major DLL revisions:

- **pre-V1.3** ("ancient", 8-export DLL, distributed via MediaFire
  before September 2011). Confirmed shipping with Pokemon
  Insurgence (`MGC_Hmode7.dll`, MD5 `d8e5b905ea25664a104058699db4344e`,
  timestamped 2011-05-15).
- **V1.3+** ("modern", includes `applyZoom` export, DLL rewritten
  for "wall events", public source available as
  `MGC_Hmode7_1_4_4.cpp`).

The V1.3 changelog says *"the DLL part is entirely rewritten"* — and
the wall-loop's layer selection clearly diverges. Both loops iterate
`itLayer` from top to bottom with a `break`-on-match condition, but
the threshold they check against differs:

- **Ancient / pre-V1.3**: accumulates the current layer's height
  from the top. For each layer, checks `dy - yd <= cum_from_top`.
  The first layer whose band contains the wall-pixel depth wins.
  Multi-layer tiles naturally expose each layer's colormap in its
  own vertical band.

- **Modern / V1.4.4**: uses the layer's `dA` entry, where `dA` is
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
degenerate loop is a red flag. Either way: any game still shipping
the pre-v1.3 DLL depends on the top-cumulative behaviour, while any
game shipping the v1.4+ DLL depends on whatever the v1.4+ DLL does.

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

## Auto-detection (future work)

The port does not currently auto-detect which DLL a game was
originally designed for. Possible heuristics a future postload shim
could use:

1. **Count of exports the HM7 Ruby layer calls.** The Insurgence
   scripts never call `HM7.apply_zoom` — that's a V1.4+ addition.
   A shim could set `WALL_LAYER_MODE = :v1_4` if the HM7 module
   defines an `apply_zoom` wrapper method.
2. **Version string in the HM7 script header comment.** MGC's
   distribution includes a comment like `# H-Mode7 V1.4.4` near
   the top of its Ruby file. Parse the number from that comment.
3. **Presence of post-V1.3 features** like `hm7_set_pivot`,
   the `@wall_type` attribute on `HM7::Surface`, or the `WIDTH` /
   `HEIGHT` / `X` / `Y` low-resolution constants (added in V1.3).

None of these are implemented in-tree yet because Insurgence is the
only target being exercised against this port. If a second game
with different version assumptions comes in, the auto-detection
shim would be the first place to add support.
