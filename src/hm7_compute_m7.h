// hm7_compute_m7.h — public surface for the computeM7 port.

#ifndef HM7_COMPUTE_M7_H
#define HM7_COMPUTE_M7_H

#include <cstdint>

struct SDL_Surface;

namespace hm7 {

// Parameters for computeM7. Mirrors the 16-element Fixnum Array
// `params` the Ruby side builds. Members are named to match the C
// variable names in the original DLL source for easy diffing.
struct ComputeM7Params {
    int cosAngle;     // [0] cos(slant) * 2048
    int sinAngle;     // [1] sin(slant) * 2048
    int altitude;     // [2] distance_h
    int pivot;        // [3] horizon screen Y
    int slope;        // [4] slope_value_map (Q17)
    int correction;   // [5] corrective_value_map (Q17)
    int heightLimit;  // [6] horizon Y upper clip
    int cosTheta;     // [7] cos(rotation) * 2048
    int sinTheta;     // [8] sin(rotation) * 2048
    int distProj;     // [9] projection plane distance
    int zoom;         // [10] zoom_map (Q12)
    int xMin;         // [11] clip left
    int xMax;         // [12] clip right
    int yMin;         // [13] clip top
    int yMax;         // [14] clip bottom
    int lessCut;      // [15] V.1.2.1 flag - extends yMax for off-screen
};

// Compute the mode7 projection LUT and per-row lighting attenuation.
//
// `data_table`: flat int16 backing of the `(xsize × ysize × 2)` Table
//   that will store projected source-map coordinates. Plane 0 = xr,
//   plane 1 = yr. Must already be allocated at the declared size.
// `xsize`, `ysize`: Table dimensions.
// `lightline`: 3-row bitmap used for scratch + per-row lighting. The
//   top-down row mapping is:
//     row 2 — lighting attenuation (BGR + flag per screen Y)
//     row 1 — per-column relief/zoom scratch
//     row 0 — reserved for renderHM7's per-column `ym` tracking
//   Must be pre-seeded: row 2 column 0 should hold the fade color
//   (see Ruby `@rowsdata.set_pixel(0, 0, hm7_fading_color)`).
// `params`: the 16-field parameter bundle. See Ruby 210-HM7_NEW_CLASSES.rb:512.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 133-251, ~120 LOC.
int compute_m7(std::int16_t *data_table,
               int xsize, int ysize,
               SDL_Surface *lightline,
               const ComputeM7Params &params);

}  // namespace hm7

#endif  // HM7_COMPUTE_M7_H
