#!/bin/sh
# Build and run the host tests under AddressSanitizer and UBSan.
# Needs a C++14 compiler, pkg-config, and SDL2.

set -e

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# shellcheck disable=SC2046
c++ -std=c++14 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$REPO_ROOT/src" $(pkg-config --cflags sdl2) \
    "$REPO_ROOT"/src/*.cpp "$REPO_ROOT/tests/library_test.cpp" \
    $(pkg-config --libs sdl2) -o "$OUT/library_test"

# The Homebrew SDL2 is sdl2-compat. It loads SDL3 from the default
# library folders only, and Homebrew keeps SDL3 out of them.
if SDL3_LIBDIR=$(pkg-config --variable=libdir sdl3 2>/dev/null); then
    DYLD_FALLBACK_LIBRARY_PATH="$SDL3_LIBDIR${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
    export DYLD_FALLBACK_LIBRARY_PATH
fi

"$OUT/library_test"
