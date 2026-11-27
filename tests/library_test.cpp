// library_test.cpp: drives the hm7_* library the way the game scripts
// that use it do, and checks the pixels that come out.
//
//   scripts/test.sh

#include "hm7_library.h"

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace hm7::library;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr int kMapW = 20;
constexpr int kMapH = 15;
constexpr int kDistanceH = 480;
constexpr int kPivot = 240;

struct Rgba {
    int r, g, b, a;
    bool operator==(const Rgba &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};
constexpr Rgba kRed = {255, 0, 0, 255};
constexpr Rgba kBlue = {0, 0, 255, 255};

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string &what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

void check_equal(long expected, long actual, const std::string &what) {
    check(expected == actual,
          what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

std::string text(const Rgba &c) {
    return "[" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
           std::to_string(c.a) + "]";
}

void check_pixel(const Rgba &expected, const Rgba &actual, const std::string &what) {
    check(expected == actual, what + ": expected " + text(expected) + ", got " + text(actual));
}

void check_error(const char *expected, const std::string &what) {
    check(std::strcmp(expected, last_error()) == 0,
          what + ": expected \"" + expected + "\", got \"" + last_error() + "\"");
}

std::vector<std::uint8_t> pixels(int w, int h, const Rgba &c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = static_cast<std::uint8_t>(c.r);
        px[i + 1] = static_cast<std::uint8_t>(c.g);
        px[i + 2] = static_cast<std::uint8_t>(c.b);
        px[i + 3] = static_cast<std::uint8_t>(c.a);
    }
    return px;
}

int image(int w, int h, const Rgba &c) {
    const std::vector<std::uint8_t> px = pixels(w, h, c);
    return image_create(px.data(), px.size(), w, h);
}

// The values of HM7::Tilemap#refresh_alpha with the defaults of
// Game_System#hm7_reset, at 100 % zoom.
std::vector<std::int32_t> parameters(double alpha) {
    const double rad = M_PI * alpha / 180;
    const int cos_alpha = static_cast<int>(2048 * std::cos(rad));
    const int sin_alpha = static_cast<int>(2048 * std::sin(rad));
    const int depth = kDistanceH << 11;
    const int h0 = (-kDistanceH * kPivot * cos_alpha) / (depth + kPivot * sin_alpha) + kPivot;
    const double z0 = static_cast<double>(depth) / (depth + kPivot * sin_alpha);
    const double slope = (1.0 - z0) / (kPivot - h0);
    const double corrective = 1.0 - kPivot * slope;
    const int last_line = -kPivot - 960;
    int height_limit = (kDistanceH * last_line * cos_alpha) / (depth - last_line * sin_alpha) + kPivot;
    if (height_limit < 0)
        height_limit = 0;
    return {cos_alpha,
            sin_alpha,
            kDistanceH,
            kPivot,
            static_cast<std::int32_t>(131072 * slope),
            static_cast<std::int32_t>(131072 * corrective),
            height_limit,
            2048,
            0,
            0,
            4096,
            0};
}

struct Map {
    int map_w = kMapW;
    int map_h = kMapH;
    int render_w = kScreenW;
    int render_h = kScreenH;
    int loop_x = 0;
    int loop_y = 0;
    int less_cut = 0;
};

// A map of tileset tile 384 on every cell: one tiles combination, flat.
// The calls come in the order of HM7::Tilemap#initialize.
int build_map(const Map &m) {
    const int ctx =
        ctx_create(m.map_w, m.map_h, m.render_w, m.render_h, m.loop_x, m.loop_y, 0, m.less_cut, 0);
    set_option(ctx, 0, 2);
    const int tileset = image(256, 32, kRed);
    const int heightset = image(256, 32, {0, 0, 0, 0});
    map_set_tileset(ctx, tileset, heightset);
    image_free(tileset);
    image_free(heightset);
    const std::vector<std::int16_t> table(static_cast<std::size_t>(m.map_w) * 4 * m.map_h, 0);
    map_set_tiletable(ctx, table.data(), table.size() * 2, m.map_w * 4, m.map_h);
    const std::int32_t combo[] = {0, 384, 0, 0, 1};
    map_set_combos(ctx, combo, sizeof combo, 1);
    map_set_animated(ctx, nullptr, 0, 0);
    const int texture_auto = image(256, 192, {0, 0, 0, 0});
    map_set_texture_auto(ctx, texture_auto);
    image_free(texture_auto);
    check_equal(0, map_build(ctx, 1, 1, 0, 0), std::string("map_build: ") + last_error());
    return ctx;
}

int build_map() {
    return build_map(Map());
}

int run_compute(int ctx, double alpha) {
    const std::vector<std::int32_t> p = parameters(alpha);
    return compute(ctx, p.data(), p.size() * 4, 0, 0, 255, 1);
}

struct Frame {
    int ret;
    int w;
    std::vector<std::uint8_t> buf;

    Rgba at(int x, int y) const {
        const std::uint8_t *p = &buf[(static_cast<std::size_t>(y) * w + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }
};

struct View {
    int w = kScreenW;
    int h = kScreenH;
    int filter = 0;
    int display_x = 0;
};

Frame run_render(int ctx, double alpha, const std::vector<std::int32_t> &surfaces, const View &v,
                 int height_limit) {
    Frame f = {0, v.w, std::vector<std::uint8_t>(static_cast<std::size_t>(v.w) * v.h * 4)};
    const int n = static_cast<int>(surfaces.size() / 6);
    f.ret =
        render(ctx, f.buf.data(), f.buf.size(), static_cast<long>(f.buf.size()), height_limit, v.display_x, 0,
               v.filter, 0, surfaces.empty() ? nullptr : surfaces.data(), surfaces.size() * 4, n);
    return f;
}

Frame run_render(int ctx, double alpha, const std::vector<std::int32_t> &surfaces = {},
                 const View &v = View()) {
    return run_render(ctx, alpha, surfaces, v, parameters(alpha)[6]);
}

// The horizon fade of the game is additive blue (Color.new(0, 0, 255, 1)).
// Seen from above, compute_m7 makes it 255 * (pivot - y) / 960 on each
// row over the pivot.
int fade_from_above(int y) {
    return 255 * (kPivot - y) / 960;
}

void test_abi_version() {
    check_equal(1, abi_version(), "abi_version");
}

void test_flat_map() {
    const int ctx = build_map();
    check(ctx > 0, "ctx_create returned " + std::to_string(ctx));
    check_equal(0, run_compute(ctx, 0), "compute");
    const Frame f = run_render(ctx, 0);
    check(f.ret >= 0, std::string("render: ") + last_error());
    // The zoom of 4108 / 4096 puts the last row just past the map.
    for (int y : {kPivot, 300, kScreenH - 2})
        check_pixel(kRed, f.at(320, y), "flat map under the pivot, row " + std::to_string(y));
    for (int y : {0, 100, kPivot - 1}) {
        const Rgba faded = {255, 0, fade_from_above(y), 255};
        check_pixel(faded, f.at(320, y), "flat map over the pivot, row " + std::to_string(y));
    }
    ctx_destroy(ctx);
}

void test_slanted_map() {
    // At 60 degrees the bottom screen row looks past a 15-tile map, so
    // the map loops vertically.
    Map m;
    m.loop_y = 1;
    const int ctx = build_map(m);
    run_compute(ctx, 60);
    const Frame f = run_render(ctx, 60);
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kRed, f.at(320, kScreenH - 1), "ground under the camera");
    check(!(f.at(320, 0) == kRed), "the top row is not the ground: " + text(f.at(320, 0)));
    ctx_destroy(ctx);
}

// compute_m7 packs the zoom of each row as (a << 12) / xp0, with
// xp0 = zoom * ((a << 18) / (slope * y + corrective)) >> 12, and the
// slope and corrective values in the doubled scale of the DLL.
int expected_row_zoom(double alpha, int y, int render_w = kScreenW) {
    const std::vector<std::int32_t> p = parameters(alpha);
    const int a = render_w >> 1;
    const int slope = (p[4] << 1) | 1;
    const int corrective = (p[5] << 1) | 1;
    const int xp0 = p[10] * ((a << 18) / (slope * y + corrective)) >> 12;
    return (a << 12) / xp0;
}

void test_row_zoom() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int zoom = row_zoom(ctx, kPivot);
    check_equal(expected_row_zoom(0, kPivot), zoom, "row_zoom of the pivot row at 0 degrees");
    // The integer steps of compute_m7 make 100 % come out a little
    // over 4096.
    check(zoom > 4096 && zoom < 4096 * 101 / 100, "row_zoom near 4096: " + std::to_string(zoom));
    run_compute(ctx, 60);
    check_equal(expected_row_zoom(60, 400), row_zoom(ctx, 400), "row_zoom of row 400 at 60 degrees");
    check_equal(0, row_zoom(ctx, -1), "row_zoom over the top");
    ctx_destroy(ctx);
}

void test_tall_narrow_screen() {
    // The rows data has one record for each screen row, also when the
    // screen is narrower than it is tall.
    Map m;
    m.render_w = 64;
    m.render_h = 480;
    const int ctx = build_map(m);
    check_equal(0, run_compute(ctx, 0), "compute");
    View v;
    v.w = 64;
    v.h = 480;
    const Frame f = run_render(ctx, 0, {}, v);
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kRed, f.at(32, 478), "low row of a 64 x 480 screen");
    check_equal(expected_row_zoom(0, 479, 64), row_zoom(ctx, 479), "row_zoom of row 479");
    ctx_destroy(ctx);
}

void test_sprite() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int sprite = image(32, 48, kBlue);
    const Frame f = run_render(ctx, 0, {0, 320, 300, sprite, 0, 0});
    check(f.ret >= 0, std::string("render: ") + last_error());
    check_pixel(kBlue, f.at(320, 290), "pixel in the sprite");
    check_pixel(kRed, f.at(100, 400), "pixel away from the sprite");
    image_free(sprite);
    ctx_destroy(ctx);
}

void test_many_sprites() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int sprite = image(8, 8, kBlue);
    std::vector<std::int32_t> records;
    for (int row = 3; row >= 0; --row) {
        for (int column = 0; column < 80; ++column) {
            const std::int32_t record[] = {0, 4 + column * 8, 260 + row * 40, sprite, 0, 0};
            records.insert(records.end(), record, record + 6);
        }
    }
    const Frame f = run_render(ctx, 0, records);
    check(f.ret >= 0, std::string("render with 320 sprites: ") + last_error());
    check_pixel(kBlue, f.at(4 + 79 * 8, 260 - 2), "the last of 320 sprites");
    image_free(sprite);
    ctx_destroy(ctx);
}

// The game sets up each map once, before map_build. A new map input
// after it would not match the heightmap and the atlas of the build.
void test_setters_after_build() {
    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    const std::int32_t combo[] = {0, 384, 0, 0, 1};
    const int texture = image(160, 32, kBlue);
    const int sheet = image(256, 192, kBlue);
    const int tileset = image(256, 32, kBlue);
    const struct {
        const char *name;
        std::function<int(int)> call;
    } setters[] = {
        {"map_set_tiletable",
         [&](int ctx) { return map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW * 4, kMapH); }},
        {"map_set_combos", [&](int ctx) { return map_set_combos(ctx, combo, sizeof combo, 1); }},
        {"map_set_animated", [&](int ctx) { return map_set_animated(ctx, nullptr, 0, 0); }},
        {"map_set_tileset", [&](int ctx) { return map_set_tileset(ctx, tileset, tileset); }},
        {"map_set_autotile", [&](int ctx) { return map_set_autotile(ctx, 0, 0, sheet, 0); }},
        {"map_add_texture", [&](int ctx) { return map_add_texture(ctx, 0, 384, texture); }},
        {"map_set_texture_auto", [&](int ctx) { return map_set_texture_auto(ctx, sheet); }},
        {"map_build", [&](int ctx) { return map_build(ctx, 1, 1, 0, 0); }},
    };
    const int ctx = build_map();
    run_compute(ctx, 0);
    for (const auto &setter : setters) {
        check_equal(-1, setter.call(ctx), std::string(setter.name) + " on a built map");
        check_error((std::string(setter.name) + ": map already built").c_str(),
                    std::string(setter.name) + " on a built map");
    }
    const Frame f = run_render(ctx, 0);
    check(f.ret >= 0, std::string("render after the refused calls: ") + last_error());
    check_pixel(kRed, f.at(320, 300), "the built map after the refused calls");
    ctx_destroy(ctx);
    image_free(texture);
    image_free(sheet);
    image_free(tileset);
}

void test_unknown_ids() {
    check_equal(-1, row_zoom(9999, 0), "row_zoom");
    check_error("row_zoom: unknown context", "row_zoom");
    check_equal(-1, heightmap_get(9999, 0, 0), "heightmap_get");
    check_error("heightmap_get: unknown context", "heightmap_get");
    check_equal(-1, image_free(9999), "image_free");
    check_error("image_free: unknown image", "image_free");

    const int ctx = build_map();
    run_compute(ctx, 0);
    const Frame f = run_render(ctx, 0, {0, 320, 300, 9999, 0, 0});
    check_equal(-1, f.ret, "render");
    check_error("render: surface references an unknown image", "render");
    ctx_destroy(ctx);
}

void test_bad_input() {
    const int ctx = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    check_equal(-1, heightmap_get(ctx, 0, 0), "heightmap_get before map_build");
    check_error("heightmap_get: map not built", "heightmap_get before map_build");

    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    check_equal(-1, map_set_tiletable(ctx, table.data(), table.size() * 2 - 2, kMapW * 4, kMapH),
                "short table");
    check_error("map_set_tiletable: tiletable size mismatch", "short table");
    check_equal(-1, map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW, kMapH), "narrow table");
    check_error("map_set_tiletable: expected map_w*4 x map_h", "narrow table");

    const int small = image(32, 32, kRed);
    check_equal(-1, map_set_autotile(ctx, 0, 0, small, 0), "small autotile");
    check_error("map_set_autotile: sheet must be 256x192", "small autotile");
    check_equal(-1, map_set_autotile(ctx, 7, 0, small, 0), "autotile slot 7");
    check_error("map_set_autotile: slot out of range", "autotile slot 7");
    check_equal(-1, map_set_texture_auto(ctx, small), "small texture_auto");
    check_error("map_set_texture_auto: sheet must be 256x192", "small texture_auto");
    check_equal(-1, map_add_texture(ctx, 0, 384, small), "small texture");
    check_error("map_add_texture: texture too small (need 160x32 or 128x32)", "small texture");
    check_equal(-1, map_add_texture(ctx, -1, 384, small), "negative basic index");
    check_error("map_add_texture: negative basic index", "negative basic index");
    image_free(small);
    // A regular wall texture has five 32-pixel strips, an autotile one has four.
    const int four_strips = image(128, 32, kRed);
    check_equal(-1, map_add_texture(ctx, 0, 384, four_strips), "128-pixel regular texture");
    check_error("map_add_texture: texture too small (need 160x32 or 128x32)", "128-pixel regular texture");
    check_equal(0, map_add_texture(ctx, 0, 48, four_strips), "128-pixel autotile texture");
    image_free(four_strips);

    check_equal(-1, ctx_create(kMapW, kMapH, 641, kScreenH, 0, 0, 0, 0, 0), "odd render width");
    check_error("ctx_create: bad render size", "odd render width");

    const std::int32_t combo[] = {-1, 384, 0, 0, 1};
    check_equal(-1, map_set_combos(ctx, combo, sizeof combo, 1), "negative combo");
    check_error("map_set_combos: negative combo index", "negative combo");
    check_equal(-1, map_set_combos(ctx, combo, sizeof combo, 2), "short combos");
    check_error("map_set_combos: bad array", "short combos");
    // The game only sends 2, and the renderer has no other mode.
    for (int value : {0, 1}) {
        const std::string what = "set_option " + std::to_string(value);
        check_equal(-1, set_option(ctx, 0, value), what);
        check_error("set_option: surface_zoom_field must be 2", what);
    }
    const std::vector<std::uint8_t> px = pixels(4, 4, kRed);
    check_equal(-1, image_create(px.data(), px.size() - 1, 4, 4), "image_create with a short buffer");
    check_error("image_create: pixel buffer too small", "image_create with a short buffer");
    check_equal(-1, image_create(nullptr, px.size(), 4, 4), "image_create with no pixels");
    check_error("image_create: null pixels", "image_create with no pixels");
    const int img = image(4, 4, kRed);
    check_equal(-1, image_update(img, px.data(), px.size() - 1, 4, 4, 255),
                "image_update with a short buffer");
    check_error("image_update: pixel buffer too small", "image_update with a short buffer");
    image_free(img);
    ctx_destroy(ctx);

    const int built = build_map();
    run_compute(built, 0);
    std::vector<std::uint8_t> buf(kScreenW * kScreenH * 4);
    // The renderer starts its rows at the camera offset.
    check_equal(-1, render(built, buf.data(), buf.size(), buf.size(), 0, 0, 0, 0, -1, nullptr, 0, 0),
                "negative camera offset");
    check_error("render: negative camera offset", "negative camera offset");
    check_equal(-1, render(built, buf.data(), buf.size(), buf.size(), 0, 0, 0, 3, 0, nullptr, 0, 0),
                "filter 3");
    check_error("render: filter must be 0, 1 or 2", "filter 3");
    check_equal(-1, render(built, buf.data(), buf.size() - 4, buf.size() - 4, 0, 0, 0, 0, 0, nullptr, 0, 0),
                "short buffer");
    check_error("render: output buffer must be render_w*render_h*4 bytes", "short buffer");
    check_equal(-1, compute(built, nullptr, 0, 0, 0, 0, 0), "compute with no params");
    check_error("compute: null params", "compute with no params");
    check_equal(0, heightmap_get(built, 5, 5), "heightmap_get of a flat map");
    ctx_destroy(built);

    // A tiletable index past the map atlas would make the kernels read
    // outside it.
    std::vector<std::int16_t> far(kMapW * 4 * kMapH, 0);
    far[0] = 100;
    const int unbuilt = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    const int tileset = image(256, 32, kRed);
    map_set_tileset(unbuilt, tileset, tileset);
    image_free(tileset);
    map_set_tiletable(unbuilt, far.data(), far.size() * 2, kMapW * 4, kMapH);
    check_equal(-1, map_build(unbuilt, 1, 1, 0, 0), "map_build with a far index");
    check_error("map_build: tile index out of range", "map_build with a far index");
    ctx_destroy(unbuilt);
}

// On a map that loops, a column whose map x is one map width left of
// the edge must wrap to column 0, not to the column past the last one.
void test_looping_map() {
    Map m;
    m.map_w = 10;
    m.loop_x = 1;
    m.loop_y = 1;
    for (double alpha : {0.0, 60.0}) {
        const int ctx = build_map(m);
        run_compute(ctx, alpha);
        const Frame f = run_render(ctx, alpha);
        const std::string what = "looping map at " + std::to_string(static_cast<int>(alpha)) + " degrees";
        check(f.ret >= 0, what + ": " + last_error());
        for (int x : {0, 320, kScreenW - 1})
            check_pixel(kRed, f.at(x, kScreenH - 1), what + ", bottom row, column " + std::to_string(x));
        ctx_destroy(ctx);
    }
}

void test_less_cut() {
    Map m;
    m.less_cut = 1;
    const int ctx = build_map(m);
    check_equal(0, run_compute(ctx, 0), "compute with less_cut");
    const Frame f = run_render(ctx, 0);
    check(f.ret >= 0, std::string("render with less_cut: ") + last_error());
    for (int y : {kPivot, 300, kScreenH - 2})
        check_pixel(kRed, f.at(320, y), "less_cut map under the pivot, row " + std::to_string(y));
    ctx_destroy(ctx);
}

// A sprite that the column pass never reaches must not hold back the
// sprites after it in the list.
void test_sprite_out_of_reach() {
    const int sprite = image(8, 8, kBlue);
    struct Case {
        const char *name;
        int map_w;
        int filter;
        int x;
    } cases[] = {
        // With filter 1 the column pass draws even columns only, and
        // this sprite starts on column 639.
        {"a sprite on the last odd column", kMapW, 1, kScreenW},
        // The map covers columns 0 to 319, and the sprite is over the
        // space right of it.
        {"a sprite past the map edge", 10, 0, 480},
    };
    for (const Case &c : cases) {
        Map m;
        m.map_w = c.map_w;
        const int ctx = build_map(m);
        run_compute(ctx, 0);
        const int narrow = image(2, 8, kBlue);
        const int first = c.filter ? narrow : sprite;
        View v;
        v.filter = c.filter;
        const Frame f = run_render(ctx, 0, {0, c.x, 300, first, 0, 0, 0, 160, 280, sprite, 0, 0}, v);
        check(f.ret >= 0, std::string(c.name) + ": " + last_error());
        check_pixel(kBlue, f.at(160, 276), std::string("the next sprite after ") + c.name);
        image_free(narrow);
        ctx_destroy(ctx);
    }
    image_free(sprite);
}

// A horizon on the last row leaves one row of zoom data, and a sprite
// foot under the screen scales from it.
void test_horizon_on_last_row() {
    const int ctx = build_map();
    run_compute(ctx, 0);
    const int sprite = image(8, 48, kBlue);
    const Frame f = run_render(ctx, 0, {0, 320, kScreenH + 10, sprite, 0, 0}, View(), kScreenH - 1);
    check(f.ret >= 0, std::string("render with the horizon on the last row: ") + last_error());
    image_free(sprite);
    ctx_destroy(ctx);
}

void test_far_and_wide_sprites() {
    const int ctx = build_map();
    // On the top row the sprite columns scale by a large factor.
    run_compute(ctx, 0);
    const int top = image(1024, 4, kBlue);
    check(run_render(ctx, 0, {0, 320, 0, top, 0, 0}).ret >= 0,
          std::string("a 1024 px sprite on the top row: ") + last_error());
    image_free(top);

    run_compute(ctx, 60);
    const int sprite = image(8, 8, kBlue);
    for (int x : {INT32_MAX, INT32_MIN, -100000, 100000}) {
        const Frame f = run_render(ctx, 60, {0, x, 300, sprite, 0, 0});
        check(f.ret >= 0, "a sprite at screen x " + std::to_string(x) + ": " + last_error());
    }
    const int wide = image(4096, 2, kBlue);
    const Frame f = run_render(ctx, 60, {0, 320, kScreenH - 1, wide, 0, 0});
    check(f.ret >= 0, std::string("a 4096 px sprite: ") + last_error());
    check_pixel(kBlue, f.at(320, kScreenH - 2), "a 4096 px sprite");
    const int too_wide = image(4098, 2, kBlue);
    check_equal(-1, run_render(ctx, 60, {0, 320, 300, too_wide, 0, 0}).ret, "a 4098 px sprite");
    check_error("render: surface wider than 4096 px", "a 4098 px sprite");
    image_free(too_wide);
    image_free(wide);
    image_free(sprite);
    ctx_destroy(ctx);
}

// The game makes combination keys from 0 up, texture indexes below
// its basic tile count, and tile values from the int16 map table.
void test_out_of_range_tiles() {
    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    const int tileset = image(256, 32, kRed);
    const int texture = image(160, 32, kBlue);
    auto unbuilt = [&]() {
        const int ctx = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
        map_set_tileset(ctx, tileset, tileset);
        map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW * 4, kMapH);
        return ctx;
    };

    int ctx = unbuilt();
    const std::int32_t far_key[] = {0, 384, 0, 0, 1, INT32_MAX, 384, 0, 0, 1};
    check_equal(0, map_set_combos(ctx, far_key, sizeof far_key, 2), "map_set_combos with a far key");
    check_equal(-1, map_build(ctx, 1, 1, 0, 0), "map_build with a far key");
    check_error("map_build: combo index out of range", "map_build with a far key");
    ctx_destroy(ctx);

    ctx = unbuilt();
    for (std::int32_t value : {INT32_MAX, 32768, -1}) {
        const std::int32_t combo[] = {0, value, 0, 0, 1};
        const std::string what = "map_set_combos with tile value " + std::to_string(value);
        check_equal(-1, map_set_combos(ctx, combo, sizeof combo, 1), what);
        check_error("map_set_combos: tile value out of range", what);
    }
    ctx_destroy(ctx);

    ctx = unbuilt();
    const std::int32_t combo[] = {0, 384, 0, 0, 1};
    map_set_combos(ctx, combo, sizeof combo, 1);
    check_equal(0, map_add_texture(ctx, INT32_MAX, 384, texture), "map_add_texture with a far index");
    check_equal(-1, map_build(ctx, 1, 1, 0, 0), "map_build with a far texture index");
    check_error("map_build: texture basic index out of range", "map_build with a far texture index");
    ctx_destroy(ctx);

    image_free(tileset);
    image_free(texture);
}

constexpr Rgba kGreen = {0, 255, 0, 255};

// Autotile slot 0 has a blue frame and a green frame. Tile value 48 is
// the first tile of that slot.
void test_autotile_animation() {
    const int ctx = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    set_option(ctx, 0, 2);
    const int tileset = image(256, 32, kRed);
    map_set_tileset(ctx, tileset, tileset);
    const int blue = image(256, 192, kBlue);
    const int green = image(256, 192, kGreen);
    check_equal(0, map_set_autotile(ctx, 0, 0, blue, 0), "map_set_autotile frame 0");
    check_equal(0, map_set_autotile(ctx, 0, 1, green, 0), "map_set_autotile frame 1");
    const std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW * 4, kMapH);
    const std::int32_t combo[] = {0, 48, 0, 0, 1};
    map_set_combos(ctx, combo, sizeof combo, 1);
    const std::int32_t animated[] = {0};
    map_set_animated(ctx, animated, sizeof animated, 1);
    check_equal(0, map_build(ctx, 1, 1, 0, 0), std::string("map_build: ") + last_error());
    run_compute(ctx, 0);
    check_pixel(kBlue, run_render(ctx, 0).at(320, 300), "autotile frame 0");

    const struct {
        std::int32_t frame;
        Rgba color;
        const char *what;
    } steps[] = {
        {1, kGreen, "autotile frame 1"},
        {0, kBlue, "autotile frame 0 again"},
        // The kernel skips a frame past the last one, so the atlas keeps
        // the frame that it has.
        {7, kBlue, "autotile frame past the last one"},
    };
    for (const auto &step : steps) {
        const std::int32_t frames[] = {step.frame, 0, 0, 0, 0, 0, 0};
        check_equal(0, map_animate(ctx, frames, sizeof frames), step.what);
        check_pixel(step.color, run_render(ctx, 0).at(320, 300), step.what);
    }
    image_free(tileset);
    image_free(blue);
    image_free(green);
    ctx_destroy(ctx);
}

// A raised block in the middle of a flat map: combination 1 is tileset
// tile 385, whose heightset value lifts it.
int build_block_map(int lighting, int texture) {
    const int ctx = ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0);
    set_option(ctx, 0, 2);
    std::vector<std::uint8_t> tiles = pixels(256, 32, kRed);
    std::vector<std::uint8_t> heights = pixels(256, 32, {0, 0, 0, 0});
    for (int y = 0; y < 32; ++y) {
        for (int x = 32; x < 64; ++x) {
            std::uint8_t *h = &heights[(static_cast<std::size_t>(y) * 256 + x) * 4];
            h[0] = 64;
            h[3] = 255;
        }
    }
    const int tileset = image_create(tiles.data(), tiles.size(), 256, 32);
    const int heightset = image_create(heights.data(), heights.size(), 256, 32);
    map_set_tileset(ctx, tileset, heightset);
    image_free(tileset);
    image_free(heightset);
    std::vector<std::int16_t> table(kMapW * 4 * kMapH, 0);
    table[7 * kMapW * 4 + 10 * 4] = 1;
    map_set_tiletable(ctx, table.data(), table.size() * 2, kMapW * 4, kMapH);
    const std::int32_t combos[] = {0, 384, 0, 0, 1, 1, 385, 0, 0, 1};
    map_set_combos(ctx, combos, sizeof combos, 2);
    map_set_animated(ctx, nullptr, 0, 0);
    check_equal(0, map_add_texture(ctx, 0, 385, texture), "map_add_texture");
    check_equal(0, map_build(ctx, 2, 1, 0, lighting), std::string("map_build: ") + last_error());
    return ctx;
}

void test_heights_and_walls() {
    const int texture = image(160, 32, kGreen);
    const int lit = build_block_map(1, texture);
    const int unlit = build_block_map(0, texture);
    image_free(texture);

    // The heightmap has 2 values for each map pixel: the height, then
    // the shadow that the lighting adds.
    const int block_x = (10 * 32 + 16) * 2;
    const int block_y = 7 * 32 + 16;
    check(heightmap_get(lit, block_x, block_y) > 0,
          "height on the block: " + std::to_string(heightmap_get(lit, block_x, block_y)));
    check_equal(0, heightmap_get(lit, 2 * 2, 2), "height away from the block");
    int lit_shadows = 0;
    int unlit_shadows = 0;
    for (int x = 0; x < kMapW * 32; ++x) {
        lit_shadows += heightmap_get(lit, x * 2 + 1, block_y) != 0;
        unlit_shadows += heightmap_get(unlit, x * 2 + 1, block_y) != 0;
    }
    check(lit_shadows > 0, "shadow values on the block row with lighting");
    check_equal(0, unlit_shadows, "shadow values on the block row without lighting");

    run_compute(lit, 60);
    const Frame f = run_render(lit, 60);
    check(f.ret >= 0, std::string("render of the block: ") + last_error());
    int wall = 0;
    for (std::size_t i = 0; i < f.buf.size(); i += 4)
        wall += f.buf[i] == 0 && f.buf[i + 1] == 255 && f.buf[i + 2] == 0;
    check(wall > 0, "the wall of the block shows its texture");
    ctx_destroy(lit);
    ctx_destroy(unlit);
}

// SDL takes its allocations from these, so a test can make the large
// ones fail. SDL needs its small allocations to store the error text.
SDL_malloc_func g_malloc;
SDL_calloc_func g_calloc;
SDL_realloc_func g_realloc;
SDL_free_func g_free;
bool g_out_of_memory = false;
constexpr std::size_t kLargeAllocation = 4096;

bool refuse(std::size_t size) {
    return g_out_of_memory && size > kLargeAllocation;
}
void *test_malloc(std::size_t size) {
    return refuse(size) ? nullptr : g_malloc(size);
}
void *test_calloc(std::size_t n, std::size_t size) {
    return refuse(n * size) ? nullptr : g_calloc(n, size);
}
void *test_realloc(void *p, std::size_t size) {
    return refuse(size) ? nullptr : g_realloc(p, size);
}

void test_out_of_memory() {
    const std::vector<std::uint8_t> px = pixels(64, 64, kRed);
    const int img = image_create(px.data(), px.size(), 64, 64);
    const int live = stats();

    g_out_of_memory = true;
    check_equal(-1, image_create(px.data(), px.size(), 64, 64), "image_create with no memory");
    check_error("image_create: out of memory", "image_create with no memory");
    const std::vector<std::uint8_t> bigger = pixels(128, 128, kBlue);
    check_equal(-1, image_update(img, bigger.data(), bigger.size(), 128, 128, 255),
                "image_update with no memory");
    check_error("image_update: out of memory", "image_update with no memory");
    check_equal(-1, ctx_create(kMapW, kMapH, kScreenW, kScreenH, 0, 0, 0, 0, 0), "ctx_create with no memory");
    check_error("ctx_create: out of memory", "ctx_create with no memory");
    g_out_of_memory = false;

    check_equal(live, stats(), "live images after the failed calls");
    // The image still works after the failed update.
    check_equal(0, image_update(img, px.data(), px.size(), 64, 64, 255), "image_update at the old size");
    image_free(img);
}

}  // namespace

int main() {
    SDL_GetOriginalMemoryFunctions(&g_malloc, &g_calloc, &g_realloc, &g_free);
    SDL_SetMemoryFunctions(test_malloc, test_calloc, test_realloc, g_free);

    test_abi_version();
    test_flat_map();
    test_slanted_map();
    test_row_zoom();
    test_tall_narrow_screen();
    test_sprite();
    test_many_sprites();
    test_setters_after_build();
    test_unknown_ids();
    test_bad_input();
    test_looping_map();
    test_less_cut();
    test_sprite_out_of_reach();
    test_horizon_on_last_row();
    test_far_and_wide_sprites();
    test_out_of_range_tiles();
    test_autotile_animation();
    test_heights_and_walls();
    test_out_of_memory();

    std::printf("library_test: %d of %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
