// hm7_apply_lighting.cpp — port of the original `applyLighting` function.
//
// Original source: MGC_Hmode7_1_4_4.cpp lines 452-551 (~100 LOC).
//
// Algorithm: walk each heightmap row left-to-right. Track a running
// "reference height" dyRef and a "distance since last change" dist.
// When the current column's height rises or falls by more than 1,
// write a shadow/highlight delta into that row's plane-1 entry using
// a precomputed falloff table `valuesAsc`. Separate logic handles
// "light descent" mode (small 1-step drops) vs "shadow mode" (large
// drops that cast darkness ahead) vs simple ascending edges.
//
// The Table is laid out with two planes interleaved along X: plane 0
// at even indices holds pixel heights (written earlier by
// drawHeightmap), plane 1 at odd indices holds shadow output. The
// original plugin reads `raw_xsize` as `xsize*2` (planes packed) and
// derives the logical width as `raw_xsize >> 1`.

#include "hm7_apply_lighting.h"

namespace hm7 {

namespace {

// Precomputed highlight/shadow falloff by distance. Exact values
// match the original valuesAsc[51] table.
const signed char valuesAsc[51] = {0, 45, 26, 18, 14, 11, 9, 8, 7, 6, 5, 5, 4, 4, 4, 3, 3,
                                   3, 3,  3,  2,  2,  2,  2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
                                   1, 1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

}  // namespace

int apply_lighting(std::int16_t *heightmap_data, int raw_xsize, int raw_ysize) {
    if (!heightmap_data || raw_xsize <= 0 || raw_ysize <= 0)
        return -1;

    const int heightmapWidth = raw_xsize >> 1;         // logical px width
    const int heightmapHeight = (raw_ysize << 1) / 3;  // ground region only

    // Cursor into the flat Table storage; advances by raw_xsize per
    // row processed. We use a local pointer to mirror the original's
    // `heightmapData += heightmapXsize` style.
    std::int16_t *row = heightmap_data;

    for (int yt = 0; yt < heightmapHeight; ++yt) {
        if (yt > 0)
            row += raw_xsize;

        int dist = 0;
        bool initDy = false;
        int dyRef = 0;
        int shadow = 0;
        bool lumDesc = false;

        for (int xt = 0; xt < heightmapWidth; ++xt) {
            ++dist;
            // Plane 0 entry at column xt.
            int dy = row[xt << 1] >> 3;

            if (!initDy) {
                dyRef = dy;
                initDy = true;
                continue;
            }

            if (shadow) {
                --shadow;
                if (dy < shadow) {
                    row[(xt << 1) + 1] = -50;
                    continue;
                } else {
                    shadow = 0;
                    dyRef = dy;
                    dist = 1;
                    // Fall through to the rising/falling check below
                    // with the updated dyRef (original did the same).
                }
            }

            if (dy > dyRef) {
                if ((dy - dyRef - 1) != 0) {
                    // dy - dyRef > 1: a multi-step rise. In lumDesc
                    // mode, retroactively darken the previous
                    // `dist-1` pixels.
                    if (lumDesc) {
                        int length = dist - 1;
                        if (length > 50)
                            length = 50;
                        int value = -valuesAsc[length];
                        for (int k = 1; k <= length; ++k) {
                            row[((xt - dist + k) << 1) + 1] = static_cast<std::int16_t>(value);
                        }
                    }
                } else {
                    // dy - dyRef == 1: a one-step rise. Normal mode:
                    // illuminate the previous `dist` pixels.
                    if (!lumDesc) {
                        int length = dist;
                        if (length > 51)
                            length = 51;
                        int value = valuesAsc[length - 1];
                        for (int k = length - 1; k >= 1; --k) {
                            row[((xt - k) << 1) + 1] = static_cast<std::int16_t>(value);
                        }
                    }
                    // lumDesc branch intentionally does nothing here
                    // (same as original).
                }
                lumDesc = false;
                dyRef = dy;
                dist = 1;
            } else if (dy < dyRef) {
                if (lumDesc) {
                    int length = dist - 1;
                    if (length > 50)
                        length = 50;
                    int value = -valuesAsc[length];
                    for (int k = 1; k <= length; ++k) {
                        row[((xt - dist + k) << 1) + 1] = static_cast<std::int16_t>(value);
                    }
                    if ((dyRef - dy - 1) != 0) {
                        // Multi-step drop: cast a shadow of length dyRef.
                        shadow = dyRef;
                        row[(xt << 1) + 1] = -50;
                    } else {
                        // One-step drop: enter light-descent mode
                        // (the "decay at the end of a ridge" visual).
                        lumDesc = true;
                    }
                } else {
                    if ((dyRef - dy - 1) != 0) {
                        shadow = dyRef;
                        row[(xt << 1) + 1] = -50;
                    } else {
                        lumDesc = true;
                    }
                }
                dyRef = dy;
                dist = 1;
            }
            // dy == dyRef: nothing to do.
        }
    }

    return 0;
}

}  // namespace hm7
