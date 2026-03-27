#!/bin/bash
# ============================================================================
# GGELUA3 iOS 动态库一键编译脚本 (仅真机 arm64)
# ============================================================================
#
# 用法:
#   ./build_ios.sh                    # 默认 Release
#   ./build_ios.sh Debug              # Debug 模式
#
# 产出:
#   install-iphoneos/Frameworks/      # 12 个动态 .framework
#
# 注意:
#   不编译 GGELUA 主程序，使用旧模板中的预编译二进制
#
# 前置要求:
#   - macOS + Xcode (Command Line Tools)
#   - CMake 3.21+
#
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIGURATION="${1:-Release}"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN} GGELUA3 iOS 动态库编译 (仅真机)${NC}"
echo -e "${CYAN} 配置: ${CONFIGURATION}${NC}"
echo -e "${CYAN}========================================${NC}"

# --- OpenSSL for iOS ---
OPENSSL_CRYPTO_LIB="${OPENSSL_CRYPTO_LIB:-${SCRIPT_DIR}/../../Dependencies/openssl/ios/lib/libcrypto.a}"
CMAKE_EXTRA_ARGS=()
if [ -f "${OPENSSL_CRYPTO_LIB}" ]; then
    echo -e "${GREEN}[OpenSSL] 发现 libcrypto.a: ${OPENSSL_CRYPTO_LIB}${NC}"
    CMAKE_EXTRA_ARGS+=("-DOPENSSL_CRYPTO_LIB=${OPENSSL_CRYPTO_LIB}")
else
    echo -e "${YELLOW}[OpenSSL] libcrypto.a 未找到: ${OPENSSL_CRYPTO_LIB}${NC}"
    echo -e "${YELLOW}  ghv_crypto.cpp 将编译但最终链接时需要提供 libcrypto.a${NC}"
    echo -e "${YELLOW}  设置 OPENSSL_CRYPTO_LIB 环境变量以指定路径${NC}"
fi

# 预期产出的动态 framework 列表（与 CMakeLists.txt install(TARGETS ...) 保持同步）
REQUIRED_FRAMEWORKS=(
    "libggelua.framework"
    "libgsdl2.framework"
    "libmygxy.framework"
    "libgastar.framework"
    "liblsqlite3.framework"
    "libhiredis.framework"
    "SDL2.framework"
    "SDL2_image.framework"
    "SDL2_ttf.framework"
    "SDL-Mixer-X.framework"
    "openssl.framework"
    "freetype.framework"
)

SDK="iphoneos"
IOS_PLATFORM="OS"
BUILD_DIR="${SCRIPT_DIR}/build-${SDK}"
INSTALL_DIR="${SCRIPT_DIR}/install-${SDK}"

echo -e "\n${CYAN}>>> 配置 ${SDK} ...${NC}"
cmake -G Xcode \
    -B "${BUILD_DIR}" \
    -S "${SCRIPT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/ios.toolchain.cmake" \
    -DIOS_PLATFORM="${IOS_PLATFORM}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}"

echo -e "${CYAN}>>> 编译 ${SDK} (${CONFIGURATION}) ...${NC}"
cmake --build "${BUILD_DIR}" \
    --config "${CONFIGURATION}" \
    -- -sdk "${SDK}" \
       -quiet \
       ONLY_ACTIVE_ARCH=NO

echo -e "${CYAN}>>> 安装 ${SDK} ...${NC}"
cmake --install "${BUILD_DIR}" --config "${CONFIGURATION}"

# 列出产物
echo -e "\n${GREEN}=== 动态 Framework 编译产物 ===${NC}"
if [ -d "${INSTALL_DIR}/Frameworks" ]; then
    for fw in "${INSTALL_DIR}/Frameworks"/*.framework; do
        FW_NAME=$(basename "$fw")
        BIN_NAME="${FW_NAME%.framework}"
        BIN_PATH="${fw}/${BIN_NAME}"
        if [ -f "$BIN_PATH" ]; then
            SIZE=$(stat -f%z "$BIN_PATH" 2>/dev/null || stat -c%s "$BIN_PATH" 2>/dev/null)
            SIZE_KB=$((SIZE / 1024))
            echo "  ✅ ${FW_NAME} (${SIZE_KB} KB)"
        else
            echo "  ⚠️  ${FW_NAME} (binary missing)"
        fi
    done
fi

# 验证必需的 framework
echo -e "\n${CYAN}>>> 验证产物完整性 ...${NC}"
MISSING=()
for fw in "${REQUIRED_FRAMEWORKS[@]}"; do
    BIN_NAME="${fw%.framework}"
    BIN_PATH="${INSTALL_DIR}/Frameworks/${fw}/${BIN_NAME}"
    if [ ! -f "$BIN_PATH" ]; then
        MISSING+=("${fw}")
    fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo -e "${RED}错误: 缺少以下动态库:${NC}"
    for fw in "${MISSING[@]}"; do
        echo -e "${RED}  ✗ ${fw}${NC}"
    done
    exit 1
else
    echo -e "${GREEN}  ✓ 所有 ${#REQUIRED_FRAMEWORKS[@]} 个动态库验证通过${NC}"
fi

echo -e "\n${GREEN}完成！产出目录: ${INSTALL_DIR}/Frameworks/${NC}"
