// hm7_bindings.h — Ruby 3.1 MRI ↔ mkxp-z glue layer.
//
// The original Windows plugin took tagged Ruby `VALUE`s as raw
// `(__id__ << 1)` integers and reached directly into RGSS1 struct
// layouts. On Ruby 3.1 those structs are gone (or are private and
// different). This header declares the single boundary where we cross
// from Ruby VALUE land into C++ structs the pixel loops understand.
//
// All binding helpers are written against the Ruby MRI C API and
// mkxp-z's own C++ Bitmap/Table classes. None of them touch RGSS1
// ABI, so they compile cleanly under any modern MRI version and stay
// stable against future MRI layout changes.

#ifndef HM7_BINDINGS_H
#define HM7_BINDINGS_H

#include <cstddef>
#include <cstdint>

// MRI's ruby.h defines a huge amount of macro machinery. Forward-
// declare just the bits we need so this header stays light.
typedef unsigned long VALUE;
struct SDL_Surface;
class Bitmap;
class Table;

namespace hm7 {

// Acquire a writable SDL_Surface* for the Bitmap wrapped by `value`.
//
// mkxp-z keeps Bitmap contents on the GPU by default and uses a
// shadow SDL_Surface when CPU access is needed. Calling this forces
// the shadow surface to exist and syncs it from GPU if necessary.
// After writing, the caller must eventually call `bitmap_commit` to
// push changes back to the GPU.
//
// Returns nullptr if the VALUE isn't a Bitmap, is disposed, or is a
// "mega" surface larger than the GL max texture size. Mega surfaces
// are never inputs to H-Mode7 paths (screen-sized render targets and
// tile-strip atlases only), so refusing to operate on them is safe.
SDL_Surface *bitmap_lock(VALUE value);

// Commit pixel changes made through a `bitmap_lock`'d surface back to
// the GPU texture. Marks the full bitmap as tainted so mkxp-z re-
// uploads on the next draw.
//
// If your modification only touched a subrect, prefer
// `bitmap_commit_rect` to upload just that region.
void bitmap_commit(VALUE value);
void bitmap_commit_rect(VALUE value, int x, int y, int w, int h);

// Return the Bitmap's width/height in pixels (shorthand so ports
// don't have to call `bitmap_lock(v)->w`).
int bitmap_width(VALUE value);
int bitmap_height(VALUE value);

// Acquire a raw int16_t pointer into the backing storage of a Table.
// Tables are 3D arrays of signed 16-bit integers used by RGSS for
// maps, heightmaps, and generic lookup buffers. mkxp-z stores them
// contiguously in `[z][y][x]` order, same as the RGSS spec, so the
// raw pointer is directly usable by the ported pixel loops.
//
// `out_xsize` / `out_ysize` / `out_zsize` are filled with the
// table's dimensions. Returns nullptr if `value` isn't a Table.
std::int16_t *table_data(VALUE value, int *out_xsize, int *out_ysize, int *out_zsize);

// Convenience: index a Table element with bounds (x,y,z) mapping to
// `data[z*xsize*ysize + y*xsize + x]`. Inline to avoid overhead in
// the per-pixel loops of the original plugin.
inline std::int16_t &table_at(std::int16_t *data, int xsize, int ysize, int x, int y, int z) {
    return data[z * xsize * ysize + y * xsize + x];
}

// Array of Fixnums → fixed-size int32_t buffer. Used for the `params`
// array passed to `computeM7` (16 elements) and several others. Fills
// up to `max_count` entries and returns the actual count. The ported
// functions read exactly the indices they expect; extras are ignored.
int array_to_ints(VALUE array, int *out, int max_count);

// Iterate a Ruby Hash whose keys are Fixnums and values are Arrays of
// four elements: `[tile_value_Fixnum, bitmap_Bitmap, anim_nbr_Fixnum,
// anim_index_Fixnum]`. Callback receives each entry one at a time.
//
// This is the one hash-iteration pattern the plugin uses (in
// drawMapTileset, refreshMapTileset, drawTextureset). Encoding it as
// a typed iterator avoids repeating the rb_hash_foreach boilerplate.
struct TileHashEntry {
    int key;            // the tile-number Ruby key
    int tile_value;     // from entry[0]
    VALUE tile_bitmap;  // from entry[1]: the tile / texture image
    int anim_nbr;       // from entry[2]
    int anim_index;     // from entry[3]
};

typedef void (*TileHashCallback)(const TileHashEntry &entry, void *user_data);

void hash_each_tile(VALUE hash, TileHashCallback cb, void *user_data);

// Utility: convert a Ruby Fixnum VALUE to an int32_t. Equivalent to
// `FIX2INT(v)` but exposed as a plain function so it can be used from
// translation units that don't include ruby.h.
int fixnum_to_int(VALUE value);

// Reverse: make a Ruby Fixnum from an int32_t. Used by ported
// functions that return integers (e.g. renderHM7 returns `oCamera`).
VALUE int_to_fixnum(int value);

}  // namespace hm7

#endif  // HM7_BINDINGS_H
