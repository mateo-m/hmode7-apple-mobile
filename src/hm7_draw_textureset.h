// hm7_draw_textureset.h — public surface for the drawTextureset port.

#ifndef HM7_DRAW_TEXTURESET_H
#define HM7_DRAW_TEXTURESET_H

#include <cstdint>

struct SDL_Surface;

namespace hm7 {

// Per-tile entry processor. Called once per iteration over the
// textureHash; writes 5 strip columns into colormap at the cell
// corresponding to `tile_num`.
//
// `colormap` — destination bitmap (`LPBYTE colormapBegin` in
//              original; the big wall-strip atlas).
// `tile_num` — Ruby hash key (integer tile number).
// `tile_value` — entry[0]; determines whether this entry is an
//                autotile (`< 384`) or a tileset tile (`>= 384`).
//                Used to select between `texture` and `texture_auto`
//                as the source.
// `texture` — the texture bitmap from entry[1] (tile-specific).
// `texture_auto` — the shared autotile texture bitmap.
// `anim_nbr`, `anim_index` — frame index math for animated tiles.
void draw_textureset_entry(SDL_Surface *colormap,
                           int tile_num,
                           int tile_value,
                           SDL_Surface *texture,
                           SDL_Surface *texture_auto,
                           int anim_nbr, int anim_index);

}  // namespace hm7

#endif  // HM7_DRAW_TEXTURESET_H
