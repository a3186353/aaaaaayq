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
ENGINE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IOS_PROJECT_DIR="${ENGINE_ROOT}/Projects/ios"
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
# 注意: 必须同时提供 libcrypto.a 和 libssl.a:
#   libcrypto.a — ghv_crypto.cpp 的 AEAD/X25519/Ed25519 业务加密 (EVP/X509/RSA 符号)
#   libssl.a    — libhv HTTPS 客户端 (SSL_CTX_new / SSL_connect / TLS_client_method 符号),
#                 锦衣 / 祥瑞 / CDN 资源下载链路依赖
# 历史教训: 仅提供 libcrypto 时, libhv ssl/openssl.c 引用的 SSL_* 符号会被 Apple ld 标记为
#   dyld dynamic_lookup, 运行时首次 hssl_ctx_new 调用即触发 _dyld_missing_symbol_abort SIGABRT
OPENSSL_CRYPTO_LIB="${OPENSSL_CRYPTO_LIB:-${ENGINE_ROOT}/Dependencies/openssl/ios/lib/libcrypto.a}"
OPENSSL_SSL_LIB="${OPENSSL_SSL_LIB:-${ENGINE_ROOT}/Dependencies/openssl/ios/lib/libssl.a}"
CMAKE_EXTRA_ARGS=()
if [ -f "${OPENSSL_CRYPTO_LIB}" ]; then
    echo -e "${GREEN}[OpenSSL] 发现 libcrypto.a: ${OPENSSL_CRYPTO_LIB}${NC}"
    CMAKE_EXTRA_ARGS+=("-DOPENSSL_CRYPTO_LIB=${OPENSSL_CRYPTO_LIB}")
else
    echo -e "${RED}[OpenSSL] libcrypto.a 未找到: ${OPENSSL_CRYPTO_LIB}${NC}"
    echo -e "${RED}  ghv_crypto.cpp 业务加密层依赖 libcrypto${NC}"
    echo -e "${RED}  请先运行 build_openssl_ios.sh 生成 OpenSSL 静态库${NC}"
    exit 1
fi
if [ -f "${OPENSSL_SSL_LIB}" ]; then
    echo -e "${GREEN}[OpenSSL] 发现 libssl.a: ${OPENSSL_SSL_LIB}${NC}"
    CMAKE_EXTRA_ARGS+=("-DOPENSSL_SSL_LIB=${OPENSSL_SSL_LIB}")
else
    echo -e "${RED}[OpenSSL] libssl.a 未找到: ${OPENSSL_SSL_LIB}${NC}"
    echo -e "${RED}  libhv HTTPS 客户端 (锦衣 / 祥瑞 CDN 下载) 必须链接 libssl${NC}"
    echo -e "${RED}  请先运行 build_openssl_ios.sh 生成 OpenSSL 静态库${NC}"
    exit 1
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
BUILD_DIR="${IOS_PROJECT_DIR}/build-${SDK}"
INSTALL_DIR="${IOS_PROJECT_DIR}/install-${SDK}"

echo -e "\n${CYAN}>>> 配置 ${SDK} ...${NC}"
cmake -G Xcode \
    -B "${BUILD_DIR}" \
    -S "${IOS_PROJECT_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${IOS_PROJECT_DIR}/ios.toolchain.cmake" \
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
