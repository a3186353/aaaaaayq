# ============================================================================
# iOS CMake Toolchain File
# GGELUA3 引擎 — iOS ARM64 交叉编译工具链
# ============================================================================
#
# 用法:
#   cmake -G Xcode -B build \
#     -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake \
#     -DIOS_PLATFORM=OS         # OS=真机, SIMULATOR64=模拟器
#
# 支持变量:
#   IOS_PLATFORM       - OS | SIMULATOR64 (默认: OS)
#   IOS_DEPLOYMENT_TARGET - 最低 iOS 版本 (默认: 15.0)
# ============================================================================

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# --- 平台选择 ---
if(NOT DEFINED IOS_PLATFORM)
    set(IOS_PLATFORM "OS")
endif()

if(IOS_PLATFORM STREQUAL "OS")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
    set(CMAKE_OSX_SYSROOT "iphoneos" CACHE STRING "")
elseif(IOS_PLATFORM STREQUAL "SIMULATOR64")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_SYSROOT "iphonesimulator" CACHE STRING "")
else()
    message(FATAL_ERROR "不支持的 IOS_PLATFORM: ${IOS_PLATFORM}，可选: OS, SIMULATOR64")
endif()

# --- 部署目标 ---
if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
    set(IOS_DEPLOYMENT_TARGET "15.0")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET ${IOS_DEPLOYMENT_TARGET} CACHE STRING "")

# --- 编译器设置 ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Xcode 属性 ---
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)  # Apple 已弃用 Bitcode

# --- 安全加固 (全局) ---
set(CMAKE_C_FLAGS_INIT "-fvisibility=hidden -fstack-protector-strong -fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fvisibility=hidden -fvisibility-inlines-hidden -fstack-protector-strong -fPIC")

# --- Release 优化 ---
set(CMAKE_C_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")

# --- 查找规则 (不搜索 macOS 路径) ---
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
