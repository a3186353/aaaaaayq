#!/bin/bash

set -e

BUILD_DIR="build-macos-shared"
OUTPUT_DIR="output-slim/macos-dylib"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_VORBIS=ON \
    -DUSE_MPEG=ON \
    -DUSE_FFMPEG=OFF \
    -DUSE_CELT=OFF \
    -DUSE_ATRAC9=OFF \
    -DUSE_SPEEX=OFF \
    -DUSE_G719=OFF \
    -DUSE_G7221=ON \
    -DBUILD_CLI=OFF \
    -DBUILD_V123=OFF \
    -DBUILD_AUDACIOUS=OFF \
    -DBUILD_SHARED_LIBS=ON

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" -j"$JOBS"

cp -f "$BUILD_DIR/src/libvgmstream.dylib" "$OUTPUT_DIR/" 2>/dev/null || true

echo "输出目录: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
