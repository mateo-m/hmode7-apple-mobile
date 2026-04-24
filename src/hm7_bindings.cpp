// hm7_bindings.cpp — Ruby 3.1 MRI ↔ mkxp-z glue layer, implementation.
//
// Every function in this file crosses the Ruby/C boundary. The ported
// pixel loops (hm7_apply_opacity.cpp and friends) never call Ruby
// themselves; they take plain C/C++ types. This keeps the math
// trivially unit-testable without a running Ruby VM.
//
// Compilation assumptions:
//   - Ruby 3.1 MRI headers on the include path (`ruby.h`).
//   - mkxp-z's `Bitmap` and `Table` C++ classes on the include path
//     (`src/display/bitmap.h`, `src/etc.h`).
//   - Build system supplies `-DHM7_HAVE_MKXP_BITMAP` and friends when
//     linking inside mkxp-z-apple-mobile. When built standalone (e.g.
//     for unit tests with a mocked `Bitmap`), leave the define unset
//     and the shims become no-ops returning nullptr so tests only
//     exercise the pure-pixel-math functions.

#include "hm7_bindings.h"

#ifdef HM7_HAVE_MKXP_BITMAP

#include <ruby.h>
#include <SDL_surface.h>

#include "bitmap.h"  // mkxp-z Bitmap
#include "etc.h"     // mkxp-z Table / IntRect
#include "binding-util.h"  // getPrivateData<>()

namespace hm7 {

SDL_Surface *bitmap_lock(VALUE value) {
    if (NIL_P(value)) return nullptr;
    Bitmap *b = getPrivateDataNoRaise<Bitmap>(value);
    if (!b) return nullptr;
    if (b->isDisposed()) return nullptr;
    // mkxp-z lazily allocates the shadow SDL_Surface on first CPU
    // access (e.g. getPixel). We trigger that allocation by reading
    // one pixel; subsequent calls return the cached surface.
    b->getPixel(0, 0);
    SDL_Surface *surf = b->surface();
    if (!surf) return nullptr;
    return surf;
}

void bitmap_commit(VALUE value) {
    if (NIL_P(value)) return;
    Bitmap *b = getPrivateDataNoRaise<Bitmap>(value);
    if (!b) return;
    if (b->isDisposed()) return;
    // `replaceRaw` pushes the CPU surface back to the GPU and marks
    // the whole bitmap tainted. We use it because it's the only
    // public API mkxp-z exposes for "I wrote to the surface, please
    // sync". If this becomes a hotspot we can add a direct uploader
    // on Bitmap that skips the memcpy-in, memcpy-out roundtrip.
    SDL_Surface *surf = b->surface();
    if (!surf) return;
    const int bytes = surf->w * surf->h * 4;
    b->replaceRaw(static_cast<const char *>(surf->pixels), bytes);
}

void bitmap_commit_rect(VALUE value, int x, int y, int w, int h) {
    // For now, always full-bitmap commit. A narrower path would need
    // mkxp-z to expose sub-rect uploadSubImage as a public method.
    // TODO: add `Bitmap::uploadCPURect(x,y,w,h)` in the mkxp-z fork
    // and wire it here; the perf win matters for renderHM7 which
    // modifies every pixel of `screenBitmap` every frame.
    (void)x; (void)y; (void)w; (void)h;
    bitmap_commit(value);
}

int bitmap_width(VALUE value) {
    if (NIL_P(value)) return 0;
    Bitmap *b = getPrivateDataNoRaise<Bitmap>(value);
    if (!b || b->isDisposed()) return 0;
    return b->width();
}

int bitmap_height(VALUE value) {
    if (NIL_P(value)) return 0;
    Bitmap *b = getPrivateDataNoRaise<Bitmap>(value);
    if (!b || b->isDisposed()) return 0;
    return b->height();
}

std::int16_t *table_data(VALUE value,
                         int *out_xsize,
                         int *out_ysize,
                         int *out_zsize) {
    if (NIL_P(value)) return nullptr;
    Table *t = getPrivateDataNoRaise<Table>(value);
    if (!t) return nullptr;
    if (out_xsize) *out_xsize = t->xSize();
    if (out_ysize) *out_ysize = t->ySize();
    if (out_zsize) *out_zsize = t->zSize();
    // mkxp-z's Table keeps its storage as a private `std::vector`.
    // The public `at()` accessor returns a reference into that
    // vector, so taking its address gives us a raw pointer to the
    // contiguous `z*y*x` int16_t backing buffer. Guarded by the
    // empty-table check to avoid UB on zero-sized tables.
    if (t->xSize() == 0 || t->ySize() == 0 || t->zSize() == 0) {
        return nullptr;
    }
    return &t->at(0, 0, 0);
}

int array_to_ints(VALUE array, int *out, int max_count) {
    if (NIL_P(array) || !RB_TYPE_P(array, T_ARRAY)) return 0;
    long len = RARRAY_LEN(array);
    if (len > max_count) len = max_count;
    for (long i = 0; i < len; ++i) {
        VALUE v = RARRAY_AREF(array, i);
        // Ruby 3.1 handles Fixnum/Bignum transparently via NUM2INT.
        // The original plugin assumed Fixnum-only; we don't bother
        // rejecting Bignum because the values are all within int32.
        if (FIXNUM_P(v)) {
            out[i] = FIX2INT(v);
        } else if (v == Qtrue) {
            out[i] = 1;
        } else if (v == Qfalse || v == Qnil) {
            out[i] = 0;
        } else {
            // Force conversion; rb_num2int raises on non-numeric.
            out[i] = NUM2INT(v);
        }
    }
    return (int)len;
}

// Helper struct shared with rb_hash_foreach.
namespace {
struct HashIterContext {
    TileHashCallback cb;
    void *user_data;
};

extern "C" int hash_iter_cb(VALUE key, VALUE val, VALUE ctx_value) {
    HashIterContext *ctx = reinterpret_cast<HashIterContext *>(ctx_value);
    if (!FIXNUM_P(key)) return ST_CONTINUE;
    if (NIL_P(val) || !RB_TYPE_P(val, T_ARRAY)) return ST_CONTINUE;
    if (RARRAY_LEN(val) < 4) return ST_CONTINUE;

    TileHashEntry e;
    e.key = FIX2INT(key);
    e.tile_value = FIX2INT(RARRAY_AREF(val, 0));
    e.tile_bitmap = RARRAY_AREF(val, 1);
    e.anim_nbr = FIX2INT(RARRAY_AREF(val, 2));
    e.anim_index = FIX2INT(RARRAY_AREF(val, 3));
    ctx->cb(e, ctx->user_data);
    return ST_CONTINUE;
}
}  // namespace

void hash_each_tile(VALUE hash, TileHashCallback cb, void *user_data) {
    if (NIL_P(hash) || !RB_TYPE_P(hash, T_HASH)) return;
    HashIterContext ctx = { cb, user_data };
    rb_hash_foreach(hash, hash_iter_cb, reinterpret_cast<VALUE>(&ctx));
}

int fixnum_to_int(VALUE value) {
    if (FIXNUM_P(value)) return FIX2INT(value);
    if (value == Qtrue) return 1;
    if (value == Qfalse || value == Qnil) return 0;
    return NUM2INT(value);
}

VALUE int_to_fixnum(int value) {
    return INT2FIX(value);
}

}  // namespace hm7

#else  // !HM7_HAVE_MKXP_BITMAP — stub build for unit tests / standalone

namespace hm7 {

SDL_Surface *bitmap_lock(VALUE) { return nullptr; }
void bitmap_commit(VALUE) {}
void bitmap_commit_rect(VALUE, int, int, int, int) {}
int bitmap_width(VALUE) { return 0; }
int bitmap_height(VALUE) { return 0; }
std::int16_t *table_data(VALUE, int *, int *, int *) { return nullptr; }
int array_to_ints(VALUE, int *, int) { return 0; }
void hash_each_tile(VALUE, TileHashCallback, void *) {}
int fixnum_to_int(VALUE) { return 0; }
VALUE int_to_fixnum(int) { return 0; }

}  // namespace hm7

#endif  // HM7_HAVE_MKXP_BITMAP
