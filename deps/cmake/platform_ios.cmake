# ============================================================================
# platform_ios.cmake — iOS (Apple Clang) 平台配置
# ============================================================================

# --- 编译器标准 ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 平台变量 ---
set(GGELUA_PLATFORM "ios")
set(GGELUA_EVENT_BACKEND "KQUEUE")

# --- OpenSSL 路径 ---
set(OPENSSL_INCLUDE_DIR "${DEPS_DIR}/openssl/include")
set(OPENSSL_CRYPTO_LIB  "${DEPS_DIR}/openssl/ios/lib/libcrypto.a"
    CACHE FILEPATH "Path to iOS cross-compiled libcrypto.a")

# --- iOS 系统 frameworks ---
set(GGELUA_PLATFORM_LIBS
    "-framework UIKit"
    "-framework Foundation"
)

# --- iOS 最低部署版本 ---
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "" FORCE)
