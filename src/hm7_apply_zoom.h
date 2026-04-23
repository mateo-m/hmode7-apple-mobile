// hm7_apply_zoom.h — public surface for the applyZoom port.
//
// See hm7_apply_zoom.cpp for the algorithm. This function is not
// currently called by any Ruby shim in the H-Mode7 plugin we've seen,
// but is part of the exported DLL surface, so we port it for
// completeness / future-version compatibility.

#ifndef HM7_APPLY_ZOOM_H
#define HM7_APPLY_ZOOM_H

#include <SDL_surface.h>

namespace hm7 {

// Resample `src` into `bmp` with bilinear filtering (if `lissage !=
// 0`) or nearest-neighbor. Both surfaces must be RGBA8888, top-down
// row order. The destination size determines the output; the source
// is scaled to fill.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 1802-1896, 94 LOC. Fixed-point
// precision is 3 bits, divisor `>> 6` (= 8 × 8 bilinear weights).
// Alpha-aware: source pixels with alpha==0 are excluded from the
// bilinear blend, so a zoomed tile with alpha cutouts doesn't bleed
// transparent edges into opaque neighbors.
//
// Returns 0 on success, non-zero on error.
int apply_zoom(SDL_Surface *bmp, SDL_Surface *src, int lissage);

}  // namespace hm7

#endif  // HM7_APPLY_ZOOM_H
