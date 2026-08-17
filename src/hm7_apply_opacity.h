// hm7_apply_opacity.h — public surface for the applyOpacity port.
//
// See hm7_apply_opacity.cpp for a description of the algorithm and
// rationale for choosing this as the proof-of-concept function.
//
// The public signature takes a raw SDL_Surface* instead of a Ruby
// VALUE so the math is testable from C++ without a running Ruby VM.
// The Ruby glue lives in the host engine, at
// //mkxp-z-apple-mobile/binding/hmode7-binding.cpp. It unwraps a
// Bitmap VALUE to the underlying surface before it calls this.

#ifndef HM7_APPLY_OPACITY_H
#define HM7_APPLY_OPACITY_H

#include <SDL_surface.h>

namespace hm7 {

// In-place multiply of the bitmap's alpha channel by
// `(opacity + 1) / 256`. Returns 0 on success, non-zero on error
// (e.g. null surface). Clamps `opacity` to `[0, 255]`.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 1773-1801, 29 LOC.
int apply_opacity(SDL_Surface *bmp, int opacity);

}  // namespace hm7

#endif  // HM7_APPLY_OPACITY_H
