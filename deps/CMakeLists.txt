# ============================================================================
# GGELUA3 跨平台构建 — 核心模块
# ============================================================================
#
# 阶段 2：5 个核心静态库 (lua54, zlib, libhv, ghv, ggelua_core)
# 支持平台：Windows (MSVC) / iOS (Apple Clang)
#
# 用法 (Windows):
#   cmake -G "Visual Studio 17 2022" -A x64 -B build-win
#   cmake --build build-win --config Release
#
# 用法 (iOS 真机):
#   cmake -G Xcode -B build-iphoneos \
#     -DCMAKE_TOOLCHAIN_FILE=Projects/ios/ios.toolchain.cmake \
#     -DIOS_PLATFORM=OS
#   cmake --build build-iphoneos --config Release
#
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(GGELUA3 C CXX)

# --- 路径定义 ---
set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCES_DIR "${ENGINE_ROOT}/Sources")
set(DEPS_DIR    "${ENGINE_ROOT}/Dependencies")
set(SDL_INCLUDE_DIR "${DEPS_DIR}/SDL/include")

# --- 平台检测与配置加载 ---
if(WIN32)
    message(STATUS "[GGELUA3] Platform: Windows (MSVC)")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform_windows.cmake")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR IOS)
    message(STATUS "[GGELUA3] Platform: iOS (Apple Clang)")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform_ios.cmake")
    enable_language(ASM)  # mpg123 NEON64 汇编 (阶段 3)
else()
    message(FATAL_ERROR "[GGELUA3] Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ============================================================================
# 1. Lua 5.4 静态库
# ============================================================================
set(LUA_SRC_DIR "${DEPS_DIR}/lua")
file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/*.c")
# 排除独立可执行文件的入口
list(FILTER LUA_SOURCES EXCLUDE REGEX "(lua\\.c|luac\\.c|onelua\\.c|ltests\\.c)$")

if(WIN32)
    # Windows: 加入 luauser.c (自定义 Lua 虚拟机钩子)
    list(APPEND LUA_SOURCES "${SOURCES_DIR}/dll/lua/luauser.c")

    add_library(lua54 STATIC ${LUA_SOURCES})
    target_include_directories(lua54 PUBLIC
        "${LUA_SRC_DIR}"
        "${SOURCES_DIR}/dll/lua"    # luauser.h
    )
    target_compile_definitions(lua54 PUBLIC
        LUA_BUILD_AS_DLL           # API 导出宏 (即使是静态库也需要，因为头文件依赖此宏)
        LUA_LIB                    # 标记为库编译
        LUA_COMPAT_APIINTCASTS     # 兼容 luaL_checkint/luaL_optint
        LUA_UCID                   # GGELUA 自定义: 中文标识符支持
        LUA_COMPAT_MATHLIB         # 兼容旧 math 函数
    )
    target_compile_definitions(lua54 PRIVATE
        LUA_USER_H="luauser.h"
    )
    # NOTE: LUA_PATH_DEFAULT / LUA_CPATH_DEFAULT 搜索路径定义
    # 当前由 VS DLL 项目 (lua54.vcxproj) 通过 AdditionalOptions /D 传递
    # 阶段 3 CMake 接管 DLL 链接时再迁移此定义
else()
    # iOS: 标准 POSIX Lua
    add_library(lua54 STATIC ${LUA_SOURCES})
    target_include_directories(lua54 PUBLIC "${LUA_SRC_DIR}")
    target_compile_definitions(lua54 PUBLIC
        LUA_USE_IOS                # iOS POSIX 兼容子集
        LUA_COMPAT_APIINTCASTS     # 与 Windows 保持一致
    )
endif()

# ============================================================================
# 2. zlib 静态库 (平台无差异)
# ============================================================================
set(ZLIB_SRC_DIR "${DEPS_DIR}/zlib")
set(ZLIB_SOURCES
    "${ZLIB_SRC_DIR}/adler32.c"
    "${ZLIB_SRC_DIR}/compress.c"
    "${ZLIB_SRC_DIR}/crc32.c"
    "${ZLIB_SRC_DIR}/deflate.c"
    "${ZLIB_SRC_DIR}/infback.c"
    "${ZLIB_SRC_DIR}/inffast.c"
    "${ZLIB_SRC_DIR}/inflate.c"
    "${ZLIB_SRC_DIR}/inftrees.c"
    "${ZLIB_SRC_DIR}/trees.c"
    "${ZLIB_SRC_DIR}/uncompr.c"
    "${ZLIB_SRC_DIR}/zutil.c"
)
# Windows 额外包含 gzip I/O 和 minizip (与 vcxproj 完全一致)
if(WIN32)
    list(APPEND ZLIB_SOURCES
        "${ZLIB_SRC_DIR}/gzclose.c"
        "${ZLIB_SRC_DIR}/gzlib.c"
        "${ZLIB_SRC_DIR}/gzread.c"
        "${ZLIB_SRC_DIR}/gzwrite.c"
        "${ZLIB_SRC_DIR}/contrib/minizip/ioapi.c"
        "${ZLIB_SRC_DIR}/contrib/minizip/unzip.c"
        "${ZLIB_SRC_DIR}/contrib/minizip/zip.c"
    )
endif()
add_library(zlib STATIC ${ZLIB_SOURCES})
target_include_directories(zlib PUBLIC "${ZLIB_SRC_DIR}")
if(WIN32)
    target_compile_definitions(zlib PRIVATE
        _CRT_NONSTDC_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_WARNINGS WIN64
    )
endif()

# ============================================================================
# 3. libhv 静态库 (网络核心 — 平台 IO 多路复用分支)
# ============================================================================
set(LIBHV_DIR "${DEPS_DIR}/libhv")

# --- 公共源文件 (三平台共用) ---
set(LIBHV_SOURCES
    # base
    "${LIBHV_DIR}/base/hbase.c"
    "${LIBHV_DIR}/base/herr.c"
    "${LIBHV_DIR}/base/hlog.c"
    "${LIBHV_DIR}/base/hsocket.c"
    "${LIBHV_DIR}/base/htime.c"
    "${LIBHV_DIR}/base/hversion.c"
    # event (公共)
    "${LIBHV_DIR}/event/hevent.c"
    "${LIBHV_DIR}/event/hloop.c"
    "${LIBHV_DIR}/event/nio.c"
    "${LIBHV_DIR}/event/unpack.c"
    "${LIBHV_DIR}/event/noevent.c"
    "${LIBHV_DIR}/event/select.c"
    # http
    "${LIBHV_DIR}/http/HttpMessage.cpp"
    "${LIBHV_DIR}/http/HttpParser.cpp"
    "${LIBHV_DIR}/http/Http1Parser.cpp"
    "${LIBHV_DIR}/http/client/AsyncHttpClient.cpp"
    "${LIBHV_DIR}/http/client/HttpClient.cpp"
    "${LIBHV_DIR}/http/http_content.cpp"
    "${LIBHV_DIR}/http/httpdef.c"
    "${LIBHV_DIR}/http/http_parser.c"
    "${LIBHV_DIR}/http/multipart_parser.c"
    # cpputil
    "${LIBHV_DIR}/cpputil/ThreadLocalStorage.cpp"
    "${LIBHV_DIR}/cpputil/hstring.cpp"
    "${LIBHV_DIR}/cpputil/hurl.cpp"
    "${LIBHV_DIR}/cpputil/hasync.cpp"
    # util
    "${LIBHV_DIR}/util/base64.c"
    "${LIBHV_DIR}/util/md5.c"
    # ssl stub (SSL 由 ghv_crypto 直接使用 OpenSSL，不走 libhv SSL 层)
    "${LIBHV_DIR}/ssl/nossl.c"
    "${LIBHV_DIR}/ssl/hssl.c"
)

# --- 平台特有事件后端源文件 ---
if(WIN32)
    list(APPEND LIBHV_SOURCES
        "${LIBHV_DIR}/event/iocp.c"
        "${LIBHV_DIR}/event/overlapio.c"
        "${LIBHV_DIR}/event/wepoll/wepoll.c"
    )
else()
    list(APPEND LIBHV_SOURCES
        "${LIBHV_DIR}/event/kqueue.c"
    )
endif()

add_library(libhv STATIC ${LIBHV_SOURCES})
target_include_directories(libhv PUBLIC
    "${LIBHV_DIR}"          # hconfig.h (跨平台统一版本)
    "${LIBHV_DIR}/base"
    "${LIBHV_DIR}/event"
    "${LIBHV_DIR}/evpp"
    "${LIBHV_DIR}/http"
    "${LIBHV_DIR}/http/client"
    "${LIBHV_DIR}/ssl"
    "${LIBHV_DIR}/cpputil"
    "${LIBHV_DIR}/util"
)
target_compile_definitions(libhv PUBLIC
    HV_STATICLIB            # 静态库模式
    HV_WITHOUT_SSL          # SSL 由 ghv_crypto 直接使用 OpenSSL
)

# --- 平台特有事件后端宏 ---
if(WIN32)
    target_compile_definitions(libhv PUBLIC EVENT_IOCP)
else()
    target_compile_definitions(libhv PUBLIC EVENT_KQUEUE)
endif()

# ============================================================================
# 4. ghv 静态库 (libhv Lua 绑定 + AEAD 加密)
# ============================================================================
set(GHV_SRC_DIR "${SOURCES_DIR}/lib/ghv")
set(GHV_SOURCES
    "${GHV_SRC_DIR}/ghv_tcp_client.cpp"
    "${GHV_SRC_DIR}/ghv_tcp_server.cpp"
    "${GHV_SRC_DIR}/ghv_crypto.cpp"
    "${GHV_SRC_DIR}/ghv_http.cpp"
    "${GHV_SRC_DIR}/ghv_download.cpp"
    "${GHV_SRC_DIR}/ghv_loop_init.c"
)

add_library(ghv STATIC ${GHV_SOURCES})
target_include_directories(ghv PRIVATE
    "${GHV_SRC_DIR}"
    "${OPENSSL_INCLUDE_DIR}"     # OpenSSL 头文件
)
target_link_libraries(ghv PUBLIC lua54 libhv)

# --- OpenSSL libcrypto 链接 ---
if(EXISTS "${OPENSSL_CRYPTO_LIB}")
    target_link_libraries(ghv PRIVATE "${OPENSSL_CRYPTO_LIB}")
    if(WIN32)
        target_link_libraries(ghv PRIVATE ${GGELUA_PLATFORM_LIBS})
    endif()
    message(STATUS "[ghv] 链接 OpenSSL: ${OPENSSL_CRYPTO_LIB}")
else()
    message(WARNING "[ghv] libcrypto 未找到: ${OPENSSL_CRYPTO_LIB}\n"
                    "  ghv_crypto.cpp 将编译但最终链接时需要提供 libcrypto\n"
                    "  使用 -DOPENSSL_CRYPTO_LIB=<path> 指定路径")
endif()

# ============================================================================
# 5. ggelua_core 静态库 (引擎核心 Lua 绑定)
# ============================================================================
set(GGELUA_SRC_DIR "${SOURCES_DIR}/lib/ggelua")
set(GGELUA_CORE_SOURCES
    "${GGELUA_SRC_DIR}/hash.c"
    "${GGELUA_SRC_DIR}/lbase64.c"
    "${GGELUA_SRC_DIR}/lfs.c"
    "${GGELUA_SRC_DIR}/lua_cmsgpack.c"
    "${GGELUA_SRC_DIR}/lua_zlib.c"
    "${GGELUA_SRC_DIR}/main.c"
    "${GGELUA_SRC_DIR}/md5.c"
    "${GGELUA_SRC_DIR}/script.c"
    "${GGELUA_SRC_DIR}/lua_aes.c"
    "${GGELUA_SRC_DIR}/core.c"
    "${GGELUA_SRC_DIR}/print/cprint.c"
    "${GGELUA_SRC_DIR}/nanoid/crypto_random.cpp"
    "${GGELUA_SRC_DIR}/nanoid/nanoid.cpp"
    "${GGELUA_SRC_DIR}/nanoid/lua.cpp"
    "${GGELUA_SRC_DIR}/uuid/lua.c"
    "${GGELUA_SRC_DIR}/uuid/uuid4.c"
    # cjson (JSON 编解码, 静态编入 ggelua)
    "${SOURCES_DIR}/lib/cjson/lua_cjson.c"
    "${SOURCES_DIR}/lib/cjson/fpconv.c"
    "${SOURCES_DIR}/lib/cjson/strbuf.c"
)

# iOS 独有: Objective-C 网络权限请求
if(NOT WIN32)
    list(APPEND GGELUA_CORE_SOURCES "${GGELUA_SRC_DIR}/ios_network.m")
endif()

add_library(ggelua_core STATIC ${GGELUA_CORE_SOURCES})
target_include_directories(ggelua_core PRIVATE
    "${GGELUA_SRC_DIR}"
    "${GGELUA_SRC_DIR}/uuid"
    "${GGELUA_SRC_DIR}/print"
    "${DEPS_DIR}/SQLite3MultipleCiphers/src"    # rijndael.h (AES 实现)
    "${SDL_INCLUDE_DIR}"                        # main.c/script.c 依赖 SDL 头文件
    "${GHV_SRC_DIR}"                            # ghv_common.h (main.c 中 ghv 轮询)
    "${SOURCES_DIR}/lib/cjson"                  # cjson 头文件 (strbuf.h, fpconv.h)
)
target_link_libraries(ggelua_core PUBLIC lua54 zlib ghv)

if(WIN32)
    target_compile_definitions(ggelua_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_definitions(ggelua_core PRIVATE
        LUA_USE_POSIX
        HAS_AES_HARDWARE=0      # iOS: 禁用 AES-NI/NEON 硬件加速
    )
endif()

# --- MSVC nanoid/lua.cpp 与 uuid/lua.c 同名冲突解决 ---
# 两个文件都叫 lua.c(pp)，MSVC 默认会生成同名 .obj 导致覆盖
# 通过设置独立的 ObjectFileName 解决 (与 vcxproj 行为一致)
if(MSVC)
    set_source_files_properties(
        "${GGELUA_SRC_DIR}/nanoid/lua.cpp"
        PROPERTIES COMPILE_FLAGS "/Fo$(IntDir)nanoid/"
    )
    set_source_files_properties(
        "${GGELUA_SRC_DIR}/uuid/lua.c"
        PROPERTIES COMPILE_FLAGS "/Fo$(IntDir)uuid/"
    )
endif()

# ============================================================================
# 构建摘要
# ============================================================================
message(STATUS "==============================================")
message(STATUS " GGELUA3 Cross-Platform Build (Stage 2)")
message(STATUS " Platform:    ${GGELUA_PLATFORM}")
message(STATUS " Event:       ${GGELUA_EVENT_BACKEND}")
message(STATUS " Targets:     lua54 zlib libhv ghv ggelua_core")
message(STATUS " OpenSSL:     ${OPENSSL_CRYPTO_LIB}")
message(STATUS " Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "==============================================")
