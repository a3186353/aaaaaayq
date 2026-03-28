# ============================================================================
# platform_windows.cmake — Windows (MSVC) 平台配置
# ============================================================================

# --- 编译器标准 ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 全局编译选项 ---
add_compile_options(
    /utf-8          # 源文件和执行字符集均为 UTF-8
    /FS             # 多进程编译时避免 PDB 写入冲突
    /W3             # Warning Level 3 (与 vcxproj 一致)
)

# --- CRT 链接模式 ---
# Release: /MT (静态 CRT)   Debug: /MTd
# 与现有 vcxproj 保持一致，避免混用导致链接冲突
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Release 优化 ---
# /O2 MaxSpeed + /Oi 内联函数 + /Gy 函数级链接
# 这些由 CMAKE_C_FLAGS_RELEASE / CMAKE_CXX_FLAGS_RELEASE 默认提供

# --- 平台变量 ---
set(GGELUA_PLATFORM "windows")
set(GGELUA_EVENT_BACKEND "IOCP")

# --- OpenSSL 路径 ---
set(OPENSSL_INCLUDE_DIR "${DEPS_DIR}/openssl/include")
set(OPENSSL_CRYPTO_LIB  "${DEPS_DIR}/openssl/lib/libcrypto.lib")

# --- Windows 系统链接库 (ghv / ggelua DLL 链接时需要) ---
set(GGELUA_PLATFORM_LIBS ws2_32 crypt32)
