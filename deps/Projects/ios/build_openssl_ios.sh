#!/bin/bash
# ============================================================================
# OpenSSL 3.6.1 iOS 交叉编译脚本 (arm64 真机)
# ============================================================================
#
# 用法:
#   ./build_openssl_ios.sh
#
# 前置要求:
#   - macOS + Xcode (Command Line Tools)
#   - OpenSSL 3.6.1 源码 (如果本地没有则自动下载)
#
# 产出:
#   ../../Dependencies/openssl/ios/lib/libcrypto.a
#   ../../Dependencies/openssl/ios/lib/libssl.a
#
# 此脚本编译后，运行 build_ios.sh 即可链接完整的 OpenSSL。
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_ROOT="${SCRIPT_DIR}/../.."
DEPS_DIR="${ENGINE_ROOT}/Dependencies"
OPENSSL_INSTALL_DIR="${DEPS_DIR}/openssl/ios"

# OpenSSL 版本（与项目 Dependencies/openssl/version.txt 保持一致）
OPENSSL_VERSION="3.6.1"
OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}"

# iOS 目标架构和最低版本（与 CMakeLists.txt 中 CMAKE_OSX_DEPLOYMENT_TARGET 保持一致）
IOS_ARCH="arm64"
IOS_MIN_VERSION="15.0"

# 临时构建目录
BUILD_DIR="${SCRIPT_DIR}/build-openssl-ios"
SRC_DIR="${BUILD_DIR}/openssl-${OPENSSL_VERSION}"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN} OpenSSL ${OPENSSL_VERSION} iOS 交叉编译${NC}"
echo -e "${CYAN} 架构: ${IOS_ARCH}${NC}"
echo -e "${CYAN} 最低 iOS: ${IOS_MIN_VERSION}${NC}"
echo -e "${CYAN}========================================${NC}"

# --- 检查 Xcode ---
if ! command -v xcrun &>/dev/null; then
    echo -e "${RED}错误: 未找到 xcrun，请安装 Xcode Command Line Tools${NC}"
    echo "  xcode-select --install"
    exit 1
fi

IOS_SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
if [ -z "$IOS_SDK_PATH" ]; then
    echo -e "${RED}错误: 未找到 iphoneos SDK${NC}"
    exit 1
fi
echo -e "${GREEN}iOS SDK: ${IOS_SDK_PATH}${NC}"

# --- 目录准备 ---
mkdir -p "${BUILD_DIR}"
mkdir -p "${OPENSSL_INSTALL_DIR}/lib"

# --- 获取源码 ---
if [ -d "${SRC_DIR}" ]; then
    echo -e "${YELLOW}源码目录已存在，跳过下载: ${SRC_DIR}${NC}"
else
    if [ ! -f "${BUILD_DIR}/${OPENSSL_TARBALL}" ]; then
        echo -e "${CYAN}>>> 下载 OpenSSL ${OPENSSL_VERSION} ...${NC}"
        curl -L -o "${BUILD_DIR}/${OPENSSL_TARBALL}" "${OPENSSL_URL}"
    fi
    echo -e "${CYAN}>>> 解压源码 ...${NC}"
    tar xzf "${BUILD_DIR}/${OPENSSL_TARBALL}" -C "${BUILD_DIR}"
fi

# --- 配置 ---
cd "${SRC_DIR}"

echo -e "${CYAN}>>> 配置 OpenSSL for iOS ${IOS_ARCH} ...${NC}"

# Clean previous build
if [ -f Makefile ]; then
    make clean 2>/dev/null || true
fi

# OpenSSL 的 iOS 交叉编译配置
# ios64-xcrun: OpenSSL 内置的 iOS ARM64 目标
# no-shared: 只编译静态库
# no-tests: 不编译测试
# no-ui-console: iOS 无控制台
# no-engine: 不需要 ENGINE 模块（减少体积）
# no-async: iOS 不支持 POSIX async
./Configure ios64-xcrun \
    --prefix="${OPENSSL_INSTALL_DIR}" \
    no-shared \
    no-tests \
    no-ui-console \
    no-engine \
    no-async \
    -miphoneos-version-min="${IOS_MIN_VERSION}" \
    2>&1 | tail -5

# --- 编译 ---
echo -e "${CYAN}>>> 编译 OpenSSL (这可能需要几分钟) ...${NC}"

# 使用 4 线程编译
make -j4 2>&1 | tail -3

# --- 安装 ---
echo -e "${CYAN}>>> 安装到 ${OPENSSL_INSTALL_DIR} ...${NC}"

# 只安装库文件（不覆盖项目已有的通用头文件）
make install_sw DESTDIR="" 2>&1 | tail -3

# --- 验证 ---
echo -e "\n${CYAN}>>> 验证产物 ...${NC}"

LIBCRYPTO="${OPENSSL_INSTALL_DIR}/lib/libcrypto.a"
LIBSSL="${OPENSSL_INSTALL_DIR}/lib/libssl.a"

if [ -f "$LIBCRYPTO" ]; then
    SIZE=$(stat -f%z "$LIBCRYPTO" 2>/dev/null || stat -c%s "$LIBCRYPTO" 2>/dev/null)
    SIZE_MB=$((SIZE / 1024 / 1024))
    echo -e "${GREEN}  ✅ libcrypto.a (${SIZE_MB} MB)${NC}"

    # 检查架构
    ARCH_INFO=$(lipo -info "$LIBCRYPTO" 2>/dev/null || file "$LIBCRYPTO")
    echo -e "${GREEN}     架构: ${ARCH_INFO}${NC}"

    # 验证关键符号
    echo -e "${CYAN}  验证关键符号:${NC}"
    for SYM in EVP_CIPHER_fetch EVP_EncryptInit_ex2 EVP_DecryptInit_ex2 \
               EVP_KDF_fetch EVP_KDF_derive OPENSSL_cleanse \
               EVP_PKEY_CTX_new_id EVP_PKEY_keygen EVP_PKEY_derive \
               EVP_PKEY_get_raw_public_key EVP_PKEY_new_raw_public_key \
               EVP_MD_CTX_new EVP_DigestInit_ex EVP_md5 \
               OSSL_PARAM_construct_utf8_string OSSL_PARAM_construct_octet_string \
               RAND_bytes; do
        if nm "$LIBCRYPTO" 2>/dev/null | grep -q " T _${SYM}$\| T ${SYM}$"; then
            echo -e "    ${GREEN}✓ ${SYM}${NC}"
        else
            echo -e "    ${RED}✗ ${SYM} (缺失!)${NC}"
        fi
    done
else
    echo -e "${RED}  ✗ libcrypto.a 未生成!${NC}"
    exit 1
fi

if [ -f "$LIBSSL" ]; then
    SIZE=$(stat -f%z "$LIBSSL" 2>/dev/null || stat -c%s "$LIBSSL" 2>/dev/null)
    SIZE_KB=$((SIZE / 1024))
    echo -e "${GREEN}  ✅ libssl.a (${SIZE_KB} KB)${NC}"
else
    echo -e "${YELLOW}  ⚠ libssl.a 未生成 (非必须，ghv 只依赖 libcrypto)${NC}"
fi

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN} OpenSSL iOS 编译完成!${NC}"
echo -e "${GREEN} 产出: ${OPENSSL_INSTALL_DIR}/lib/libcrypto.a${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "${CYAN}下一步: 运行 ./build_ios.sh 重新编译所有 framework${NC}"
