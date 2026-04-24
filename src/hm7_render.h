// hm7_render.h — public surface for renderHM7.
//
// This is the heart of the H-Mode7 software rasterizer. Takes the
// precomputed projection LUT from computeM7, a heightmap, a wall-
// strip colormap, the tilemap + compact tileset, a sorted list of
// surfaces (billboard sprites), and writes the final screen frame.
//
// All structured arguments are passed as typed structs rather than
// Ruby Arrays; the binding layer unpacks on the Ruby side. This
// keeps the pixel-math function pure C++ (trivially unit-testable
// with capture-and-replay tests).

#ifndef HM7_RENDER_H
#define HM7_RENDER_H

#include <cstdint>

struct SDL_Surface;

namespace hm7 {

// Parameters array from the Ruby side's @params. See design doc
// §2.2 for the full mapping. All fields are integers; booleans are
// 0/1.
struct RenderParams {
    SDL_Surface *screen_bitmap;    // [0] destination render target
    std::int16_t *data_table;      // [1] Table backing from computeM7
    int data_xsize;                //     Table xsize
    int data_ysize_real;           //     Table ysize (raw)
    SDL_Surface *lightline;        // [2] 3-row scratch/lighting bitmap
    std::int16_t *heightmap;       // [3] Table backing: per-pixel heights
    int heightmap_xsize;           //     Table xsize (already 2*logical)
    SDL_Surface *map_tileset;      // [4] compact tile atlas
    std::int16_t *tilemap_data;    // [5] tile indices per cell
    int tilemap_xsize;             //     Table xsize
    int tilemap_ysize;             //     Table ysize
    SDL_Surface *colormap;         // [6] wall-strip texture atlas
    int loop_x;                    // [7]
    int loop_y;                    // [8]
    int cam;                       // [9] camera mode 0/1/2
    SDL_Surface *s_screen_bitmap;  // [10] 2×-wide surface-compositing scratch
    int less_cut;                  // [11]
    int no_black;                  // [12]
    int x_min, x_max;              // [13,14]
    int y_min;                     // [15]
    int y_max_draw;                // [16] clip bottom
};

// Per-frame vars array from the Ruby side's @vars. Design doc §2.3.
struct RenderVars {
    int height_limit;   // [0] horizon clip
    int display_x;      // [1]
    int display_y;      // [2]
    int filter;         // [3] 0=no filter, 1=even cols, 2=odd cols
    int o_scr_y;        // [4] camera Y offset
};

// One surface (billboard sprite) entry. Design doc §2.4. Fields
// named after the original's `sPtr[]` indices.
struct RenderSurface {
    int type;             // [0] 0 or 1 (controls which lightline channel
                          //     pair holds the fading lookup)
    int screen_x1;        // [1]
    int screen_y1;        // [2]
    int screen_x2;        // [3]
    int screen_y2;        // [4]
    int inverse;          // [5]
    SDL_Surface *bitmap;  // [6]
    int dh;               // [7] vertical displacement
    int blend;            // [8] 0=normal, 1=add, 2=subtract
    int disp_width;       // [9]
    int disp_offset;      // [10]
};

// Wall-layer selection algorithm. Between v1.2.x and v1.4 the DLL
// was reworked (see the forum changelog: V1.3 "the DLL part is
// entirely rewritten"; V1.4 "can now handle n layers"). The two
// eras disagree on which layer's colormap a wall pixel samples.
//
//   `WallLayerMode::TopCumulative`  - matches pre-v1.3 DLLs
//       (Pokemon Insurgence ships one at MGC_Hmode7.dll, 8 exports,
//       stamped 2011-05-15). Iterating top-to-bottom, the first
//       layer whose *own* height exceeds the wall-pixel's depth
//       from the tile's crown wins. A 3-layer house (ground /
//       wall / roof) thus surfaces each layer's colormap in its
//       correct vertical band, giving visible facades even when
//       the ground heightmap contributes no extra relief.
//
//   `WallLayerMode::BottomCumulative` - matches v1.4.4 reference
//       source. Condition is `dy - yd <= dA[layer]` where dA is
//       *bottom-cumulative*, which in the common case where `dy ==
//       dA[top]` (no ground-heightmap contribution) always picks
//       the top layer. Faithful to v1.4.4's code but the
//       layer-selection loop is effectively degenerate.
//
// Default is TopCumulative because Insurgence (and every other
// pre-v1.3 title) depends on it to render buildings correctly. To
// run a game shipping a v1.3+ DLL faithfully, the caller can flip
// the mode via the Ruby-side HM7::Native::WALL_LAYER_MODE constant
// which the binding layer forwards as `params.wall_layer_mode`.
enum class WallLayerMode : int {
    TopCumulative    = 0,  // v1.2.x / pre-V1.3 DLL
    BottomCumulative = 1,  // v1.4.4 reference
};

// Main entry point. Returns `oCamera` — the maximum ody seen at
// `yt == ysize-1`, used on the Ruby side to auto-adjust the camera
// Y offset in modes 1/2.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 763-1767, ~1010 LOC.
int render_hm7(const RenderParams &p,
               const RenderVars &v,
               const RenderSurface *surfaces,
               int surface_count,
               int nb_layers,
               WallLayerMode wall_layer_mode = WallLayerMode::TopCumulative);

}  // namespace hm7

#endif  // HM7_RENDER_H
