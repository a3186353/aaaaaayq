#!/bin/bash
# 编译vgmstream共享库(.so)版本

set -e

echo "编译libvgmstream.so..."

BUILD_DIR="build-linux-shared"
OUTPUT_DIR="output-slim/linux-so"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# CMake配置 - 共享库
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
    -DBUILD_SHARED_LIBS=ON

cmake --build "$BUILD_DIR" -j$(nproc)

cp -f "$BUILD_DIR/src/libvgmstream.so"* "$OUTPUT_DIR/" 2>/dev/null || true

echo "输出目录: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
