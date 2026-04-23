// hm7_apply_lighting.h — public surface for the applyLighting port.

#ifndef HM7_APPLY_LIGHTING_H
#define HM7_APPLY_LIGHTING_H

#include <cstdint>

namespace hm7 {

// Walk the per-pixel heightmap horizontally and write simulated
// highlight/shadow values (signed int8 in plane 1) based on height
// changes. Called once per map after `drawHeightmap`, and
// occasionally after map updates. Not performance-critical.
//
// `heightmap_data`: pointer to the heightmap Table's int16_t storage.
//   The table is laid out as two interleaved planes:
//     plane 0 (`data[x*2]`):    pixel height used for rendering.
//     plane 1 (`data[x*2 + 1]`): shadow/highlight delta (output).
// `raw_xsize`: the Table's stored xsize (logical_width * 2, since
//   two planes are packed along X).
// `raw_ysize`: the Table's stored ysize.
//
// Only rows `[0, raw_ysize*2/3)` are processed (the "ground" region).
// The last third is the "bush/cliff" region handled by renderHM7.
//
// Original: MGC_Hmode7_1_4_4.cpp lines 452-551, ~100 LOC.
int apply_lighting(std::int16_t *heightmap_data,
                   int raw_xsize, int raw_ysize);

}  // namespace hm7

#endif  // HM7_APPLY_LIGHTING_H
