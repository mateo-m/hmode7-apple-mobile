// hm7_apply_opacity.cpp — port of the original `applyOpacity` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 1773-1801 (~29 LOC).
//
// Algorithm: in-place multiply of a bitmap's alpha channel by
// `(opacity + 1) / 256`. Straight two-nested-loop pixel walk; no
// projection math, no heightmap, no surfaces. Chosen as the first
// function to port because:
//
//   1. It's the smallest exported entry point (~23 LOC of real work).
//   2. It exercises the full binding + Bitmap-access plumbing without
//      touching Tables, Arrays, or Hashes. If applyOpacity renders a
//      game-visible result on iOS, the plumbing works for every other
//      function.
//   3. The Ruby side calls it exactly once per surface-sprite per
//      frame (210-HM7_NEW_CLASSES.rb's Surface#get_data pipeline),
//      so we'll know immediately if it's plumbed wrong during the
//      Insurgence intro.

#include "hm7_apply_opacity.h"
#include "hm7_pixels.h"

#include <SDL_surface.h>

namespace hm7 {

int apply_opacity(SDL_Surface *bmp, int opacity) {
    if (!bmp) {
        return -1;
    }
    // Clamp to [0, 255] to match the Windows implementation's assumed
    // range. The DLL relied on Ruby never passing values outside that
    // range; we're explicit to avoid undefined math on misuse.
    if (opacity < 0) {
        opacity = 0;
    } else if (opacity > 255) {
        opacity = 255;
    }

    const int w = bmp->w;
    const int h = bmp->h;
    // Original used `(opacity + 1)` and `>> 8` so that opacity=255
    // maps to identity (a *= 256 >> 8 == a) and opacity=0 zeroes
    // alpha. Keep that contract.
    const int scale = opacity + 1;

    for (int y = 0; y < h; ++y) {
        Pixel *row = hm7_row_ptr(bmp, y);
        for (int x = 0; x < w; ++x) {
            Pixel &px = row[x];
            px.a = static_cast<std::uint8_t>((px.a * scale) >> 8);
        }
    }

    return 0;
}

}  // namespace hm7
