#!/bin/bash
# vgmstream精简版Linux编译脚本
# 支持FSB5: Vorbis, MPEG, PCM16, IMAADPCM

set -e

echo "=========================================="
echo "vgmstream精简版Linux编译脚本"
echo "=========================================="

# 检查依赖
echo "检查依赖..."
if ! command -v cmake &> /dev/null; then
    echo "错误: 请先安装cmake"
    echo "  Ubuntu/Debian: sudo apt-get install cmake"
    echo "  CentOS/RHEL:   sudo yum install cmake"
    exit 1
fi

if ! command -v gcc &> /dev/null; then
    echo "错误: 请先安装gcc"
    echo "  Ubuntu/Debian: sudo apt-get install build-essential"
    exit 1
fi

# 检查libvorbis
if ! pkg-config --exists vorbis 2>/dev/null; then
    echo "警告: 未找到libvorbis，尝试安装..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get install -y libvorbis-dev libogg-dev
    elif command -v yum &> /dev/null; then
        sudo yum install -y libvorbis-devel libogg-devel
    fi
fi

# 检查libmpg123
if ! pkg-config --exists libmpg123 2>/dev/null; then
    echo "警告: 未找到libmpg123，尝试安装..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get install -y libmpg123-dev
    elif command -v yum &> /dev/null; then
        sudo yum install -y mpg123-devel
    fi
fi

# 创建编译目录
BUILD_DIR="build-linux-slim"
OUTPUT_DIR="output-slim/linux"

echo "创建编译目录: $BUILD_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# CMake配置 - 精简版
echo "CMake配置..."
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
    -DBUILD_CLI=ON \
    -DBUILD_V123=OFF \
    -DBUILD_AUDACIOUS=OFF \
    -DBUILD_SHARED_LIBS=OFF

# 编译
echo "开始编译..."
cmake --build "$BUILD_DIR" -j$(nproc)

# 复制输出
echo "整理输出文件..."
if [ -f "$BUILD_DIR/src/libvgmstream.a" ]; then
    cp "$BUILD_DIR/src/libvgmstream.a" "$OUTPUT_DIR/"
    echo "  -> $OUTPUT_DIR/libvgmstream.a"
fi

if [ -f "$BUILD_DIR/cli/vgmstream-cli" ]; then
    cp "$BUILD_DIR/cli/vgmstream-cli" "$OUTPUT_DIR/"
    echo "  -> $OUTPUT_DIR/vgmstream-cli"
fi

# 显示结果
echo ""
echo "=========================================="
echo "编译完成!"
echo "=========================================="
echo "输出目录: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"

echo ""
echo "测试命令:"
echo "  $OUTPUT_DIR/vgmstream-cli -o test.wav 你的文件.fsb"
