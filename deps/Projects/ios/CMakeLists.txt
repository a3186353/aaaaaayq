# ============================================================================
# GGELUA3 iOS 构建工程
# ============================================================================
#
# 用法 (真机):
#   cmake -G Xcode -B build-iphoneos \
#     -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake \
#     -DIOS_PLATFORM=OS
#   cmake --build build-iphoneos --config Release
#
# 用法 (模拟器):
#   cmake -G Xcode -B build-iphonesimulator \
#     -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake \
#     -DIOS_PLATFORM=SIMULATOR64
#   cmake --build build-iphonesimulator --config Release
#
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(GGELUA3_iOS C CXX ASM)  # ASM: mpg123 NEON64 汇编 (.S)


# --- 路径定义 ---
set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SOURCES_DIR "${ENGINE_ROOT}/Sources")
set(DEPS_DIR    "${ENGINE_ROOT}/Dependencies")
set(SDL_INCLUDE_DIR "${DEPS_DIR}/SDL/include")

# ============================================================================
# 1. Lua 5.4 静态库
# ============================================================================
set(LUA_SRC_DIR "${DEPS_DIR}/lua")
file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/*.c")
# 排除独立可执行文件的入口
list(FILTER LUA_SOURCES EXCLUDE REGEX "(lua\\.c|luac\\.c|onelua\\.c|ltests\\.c)$")

add_library(lua54 STATIC ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC "${LUA_SRC_DIR}")
target_compile_definitions(lua54 PUBLIC
    LUA_USE_IOS       # iOS POSIX 兼容子集 (luaconf.h 自动定义 LUA_USE_POSIX + LUA_USE_DLOPEN)
    LUA_COMPAT_APIINTCASTS  # 与 Windows/Android 保持一致: 启用 luaL_checkint/luaL_optint 等兼容宏
)

# ============================================================================
# 2. zlib 静态库
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
add_library(zlib STATIC ${ZLIB_SOURCES})
target_include_directories(zlib PUBLIC "${ZLIB_SRC_DIR}")

# ============================================================================
# 3. libhv 静态库 (网络核心, epoll→kqueue for iOS)
# ============================================================================
set(LIBHV_DIR "${DEPS_DIR}/libhv")
set(LIBHV_SOURCES
    # base
    "${LIBHV_DIR}/base/hbase.c"
    "${LIBHV_DIR}/base/herr.c"
    "${LIBHV_DIR}/base/hlog.c"
    "${LIBHV_DIR}/base/hsocket.c"
    "${LIBHV_DIR}/base/htime.c"
    "${LIBHV_DIR}/base/hversion.c"
    # event (kqueue for iOS/macOS)
    "${LIBHV_DIR}/event/hevent.c"
    "${LIBHV_DIR}/event/hloop.c"
    "${LIBHV_DIR}/event/kqueue.c"
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
    # ssl stub
    "${LIBHV_DIR}/ssl/nossl.c"
    "${LIBHV_DIR}/ssl/hssl.c"
)
add_library(libhv STATIC ${LIBHV_SOURCES})
target_include_directories(libhv PUBLIC
    "${LIBHV_DIR}"            # hconfig.h (跨平台统一版本, 含 __APPLE__ 分支)
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
    HV_STATICLIB
    HV_WITHOUT_SSL   # SSL 由 ghv_crypto 直接使用 OpenSSL，不走 libhv SSL 层
    EVENT_KQUEUE      # iOS/macOS 使用 kqueue
)

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
    "${DEPS_DIR}/openssl/include"   # OpenSSL 头文件（平台无关）
)
target_link_libraries(ghv PUBLIC lua54 libhv)

# iOS 交叉编译的 libcrypto.a 路径，可通过 cmake 参数覆盖：
#   -DOPENSSL_CRYPTO_LIB=/path/to/ios/libcrypto.a
set(OPENSSL_CRYPTO_LIB "${DEPS_DIR}/openssl/ios/lib/libcrypto.a"
    CACHE FILEPATH "Path to iOS cross-compiled libcrypto.a")
if(EXISTS "${OPENSSL_CRYPTO_LIB}")
    target_link_libraries(ghv PRIVATE "${OPENSSL_CRYPTO_LIB}")
    message(STATUS "[ghv] 链接 OpenSSL: ${OPENSSL_CRYPTO_LIB}")
else()
    message(WARNING "[ghv] libcrypto.a 未找到: ${OPENSSL_CRYPTO_LIB}\n"
                    "  ghv_crypto.cpp 将编译但最终链接时需要提供 libcrypto.a\n"
                    "  使用 -DOPENSSL_CRYPTO_LIB=<path> 指定路径")
endif()

# ============================================================================
# 5. ggelua 静态库 (引擎核心 Lua 绑定)
# ============================================================================
set(GGELUA_SRC_DIR "${SOURCES_DIR}/lib/ggelua")
set(GGELUA_SOURCES
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
    # cjson (JSON 编解码, Windows 上是独立 DLL, iOS/Android 静态链入)
    "${SOURCES_DIR}/lib/cjson/lua_cjson.c"
    "${SOURCES_DIR}/lib/cjson/fpconv.c"
    "${SOURCES_DIR}/lib/cjson/strbuf.c"
    "${GGELUA_SRC_DIR}/ios_network.m"
    "${GGELUA_SRC_DIR}/print/cprint.c"
    "${GGELUA_SRC_DIR}/nanoid/crypto_random.cpp"
    "${GGELUA_SRC_DIR}/nanoid/nanoid.cpp"
    "${GGELUA_SRC_DIR}/nanoid/lua.cpp"
    "${GGELUA_SRC_DIR}/uuid/lua.c"
    "${GGELUA_SRC_DIR}/uuid/uuid4.c"
)
add_library(ggelua STATIC ${GGELUA_SOURCES})
target_include_directories(ggelua PRIVATE
    "${GGELUA_SRC_DIR}"
    "${GGELUA_SRC_DIR}/uuid"
    "${GGELUA_SRC_DIR}/print"
    "${DEPS_DIR}/SQLite3MultipleCiphers/src"  # rijndael.h (AES)
    "${SDL_INCLUDE_DIR}"                      # main.c/script.c 依赖 SDL 头文件
    "${SOURCES_DIR}/lib/cjson"                # cjson 头文件 (strbuf.h, fpconv.h)
)
target_link_libraries(ggelua PUBLIC lua54 zlib ghv)
target_compile_definitions(ggelua PRIVATE
    LUA_USE_POSIX
    HAS_AES_HARDWARE=0  # 禁用 AES-NI/NEON 硬件加速 (lua_aes.c 内联 #include rijndael.c → aes_hardware.c)
)

# ============================================================================
# 6. mygxy 静态库 (游戏特有模块)
# ============================================================================
set(MYGXY_SRC_DIR "${SOURCES_DIR}/grr/mygxy")
set(MYGXY_SOURCES
    "${MYGXY_SRC_DIR}/fsb.c"
    "${MYGXY_SRC_DIR}/hash.c"
    "${MYGXY_SRC_DIR}/lua_proxy.c"
    "${MYGXY_SRC_DIR}/sdl_proxy.c"
    "${MYGXY_SRC_DIR}/map.c"
    "${MYGXY_SRC_DIR}/SDL_list.c"
    "${MYGXY_SRC_DIR}/tcp.c"
    "${MYGXY_SRC_DIR}/wdf.c"
    "${MYGXY_SRC_DIR}/wpk.c"
    "${MYGXY_SRC_DIR}/ujpeg/ujpeg.c"
    # zstd (解压)
    "${DEPS_DIR}/zstd/common/debug.c"
    "${DEPS_DIR}/zstd/common/entropy_common.c"
    "${DEPS_DIR}/zstd/common/error_private.c"
    "${DEPS_DIR}/zstd/common/fse_decompress.c"
    "${DEPS_DIR}/zstd/common/xxhash.c"
    "${DEPS_DIR}/zstd/common/zstd_common.c"
    "${DEPS_DIR}/zstd/decompress/huf_decompress.c"
    "${DEPS_DIR}/zstd/decompress/zstd_ddict.c"
    "${DEPS_DIR}/zstd/decompress/zstd_decompress.c"
    "${DEPS_DIR}/zstd/decompress/zstd_decompress_block.c"
    # lz4
    "${DEPS_DIR}/lz4/lz4.c"
    "${DEPS_DIR}/lz4/lz4hc.c"
    "${DEPS_DIR}/lz4/lz4frame.c"
    "${DEPS_DIR}/lz4/xxhash.c"
)
add_library(mygxy STATIC ${MYGXY_SOURCES})
target_include_directories(mygxy PRIVATE
    "${MYGXY_SRC_DIR}"
    "${SDL_INCLUDE_DIR}"            # sdl_proxy.h → SDL.h
    "${DEPS_DIR}/SDL_image"         # sdl_proxy.h → SDL_image.h
    "${DEPS_DIR}/zstd"
    "${DEPS_DIR}/zstd/common"
    "${DEPS_DIR}/lz4"
    "${DEPS_DIR}/zlib"
    "${ENGINE_ROOT}/external/vgmstream/src"   # libvgmstream.h (FSB 解码)
)
# 链接 zlib 静态库（不再内联 zlib 源文件，避免重复符号）
target_link_libraries(mygxy PUBLIC lua54 zlib)
target_compile_definitions(mygxy PRIVATE
    ZSTD_DISABLE_ASM=1  # iOS ARM64 禁用内联汇编，使用纯 C 实现
)

# lz4 内的 xxhash 需要命名空间隔离，防止与 zstd 冲突
# 注意：lz4hc.c 和 lz4frame.c 也会 #include "xxhash.h"，需要同样定义，
# 否则 inline 展开的 xxhash 符号将与 zstd 内的 xxhash 冲突
set_source_files_properties(
    "${DEPS_DIR}/lz4/xxhash.c"
    "${DEPS_DIR}/lz4/lz4hc.c"
    "${DEPS_DIR}/lz4/lz4frame.c"
    PROPERTIES COMPILE_DEFINITIONS "XXH_NAMESPACE=LZ4_"
)

# ============================================================================
# 7. hiredis 静态库 (Redis 客户端)
# ============================================================================
set(HIREDIS_SRC_DIR "${SOURCES_DIR}/lib/hiredis")
set(HIREDIS_LIB_DIR "${HIREDIS_SRC_DIR}/hiredis")
set(HIREDIS_SOURCES
    "${HIREDIS_LIB_DIR}/alloc.c"
    "${HIREDIS_LIB_DIR}/async.c"
    "${HIREDIS_LIB_DIR}/dict.c"
    "${HIREDIS_LIB_DIR}/hiredis.c"
    "${HIREDIS_LIB_DIR}/net.c"
    "${HIREDIS_LIB_DIR}/read.c"
    "${HIREDIS_LIB_DIR}/sds.c"
    "${HIREDIS_LIB_DIR}/sockcompat.c"
    "${HIREDIS_SRC_DIR}/lua-hiredis.c"
)
add_library(hiredis STATIC ${HIREDIS_SOURCES})
target_include_directories(hiredis PRIVATE
    "${HIREDIS_SRC_DIR}"
    "${HIREDIS_LIB_DIR}"
)
target_link_libraries(hiredis PUBLIC lua54)

# ============================================================================
# 8. sqlite3 静态库 (SQLite3MultipleCiphers 加密数据库)
# ============================================================================
set(SQLITE3MC_SRC_DIR "${DEPS_DIR}/SQLite3MultipleCiphers/src")
add_library(sqlite3 STATIC "${SQLITE3MC_SRC_DIR}/sqlite3mc.c")
target_include_directories(sqlite3 PUBLIC "${SQLITE3MC_SRC_DIR}")
# iOS SDK 虽然定义了 __MAC_10_12 但没有 <sys/random.h>，
# 用空 shim 头文件避免 chacha20poly1305.c 编译失败
target_include_directories(sqlite3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_compile_definitions(sqlite3 PRIVATE
    HAS_AES_HARDWARE=0  # 禁用 AES-NI/NEON 硬件加速，使用纯 C 软件实现
                        # (模拟器 x86_64 Clang 无 AES target feature；ARM64 走软件也足够)
    SQLITE_HAS_CODEC
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_MAX_ATTACHED=10
    SQLITE_ENABLE_COLUMN_METADATA=1
    SQLITE_SECURE_DELETE=1
    SQLITE_ENABLE_FTS3=1
    SQLITE_ENABLE_FTS3_PARENTHESIS=1
    SQLITE_ENABLE_FTS4=1
    SQLITE_ENABLE_FTS5=1
    SQLITE_ENABLE_JSON1=1
    SQLITE_ENABLE_RTREE=1
    SQLITE_ENABLE_GEOPOLY=1
    SQLITE_CORE=1
    SQLITE_ENABLE_MATH_FUNCTIONS=1
    SQLITE_TEMP_STORE=2
    SQLITE_USE_URI=1
)

# ============================================================================
# 9. lsqlite3 静态库 (SQLite3 Lua 绑定)
# ============================================================================
set(LSQLITE3_SRC_DIR "${SOURCES_DIR}/lib/lsqlite3")
add_library(lsqlite3 STATIC "${LSQLITE3_SRC_DIR}/lsqlite3.c")
target_include_directories(lsqlite3 PRIVATE "${SQLITE3MC_SRC_DIR}")
target_link_libraries(lsqlite3 PUBLIC lua54 sqlite3)
target_compile_definitions(lsqlite3 PRIVATE SQLITE_HAS_CODEC)

# ============================================================================
# 10. gastar 静态库 (A* 寻路算法)
# ============================================================================
set(GASTAR_SRC_DIR "${SOURCES_DIR}/lib/gastar")
set(GASTAR_SOURCES
    "${GASTAR_SRC_DIR}/Astart.cpp"
    "${GASTAR_SRC_DIR}/DLinkList.cpp"
    "${GASTAR_SRC_DIR}/lua.cpp"
    "${GASTAR_SRC_DIR}/memorypool.c"
)
add_library(gastar STATIC ${GASTAR_SOURCES})
target_include_directories(gastar PRIVATE "${GASTAR_SRC_DIR}")
target_link_libraries(gastar PUBLIC lua54)

# ============================================================================
# 11. gsdl2 静态库 (SDL2 Lua 绑定 — 图形/音频/字体)
# ============================================================================
set(GSDL2_SRC_DIR "${SOURCES_DIR}/lib/gsdl2")
set(GSDL2_SOURCES
    "${GSDL2_SRC_DIR}/audio.c"
    "${GSDL2_SRC_DIR}/blendmode.c"
    "${GSDL2_SRC_DIR}/clipboard.c"
    "${GSDL2_SRC_DIR}/cpuinfo.c"
    "${GSDL2_SRC_DIR}/error.c"
    "${GSDL2_SRC_DIR}/events.c"
    "${GSDL2_SRC_DIR}/filesystem.c"
    "${GSDL2_SRC_DIR}/gamecontroller.c"
    "${GSDL2_SRC_DIR}/gesture.c"
    "${GSDL2_SRC_DIR}/gge.c"
    "${GSDL2_SRC_DIR}/haptic.c"
    "${GSDL2_SRC_DIR}/hints.c"
    "${GSDL2_SRC_DIR}/joystick.c"
    "${GSDL2_SRC_DIR}/keyboard.c"
    "${GSDL2_SRC_DIR}/locale.c"
    "${GSDL2_SRC_DIR}/log.c"
    "${GSDL2_SRC_DIR}/messagebox.c"
    "${GSDL2_SRC_DIR}/metal.c"
    "${GSDL2_SRC_DIR}/misc.c"
    "${GSDL2_SRC_DIR}/mouse.c"
    "${GSDL2_SRC_DIR}/opengl.c"
    "${GSDL2_SRC_DIR}/pixels.c"
    "${GSDL2_SRC_DIR}/platform.c"
    "${GSDL2_SRC_DIR}/power.c"
    "${GSDL2_SRC_DIR}/rect.c"
    "${GSDL2_SRC_DIR}/render.c"
    "${GSDL2_SRC_DIR}/rwops.c"
    "${GSDL2_SRC_DIR}/sdl.c"
    "${GSDL2_SRC_DIR}/sdl_image.c"
    "${GSDL2_SRC_DIR}/sdl_mixer.c"
    "${GSDL2_SRC_DIR}/sdl_net.c"
    "${GSDL2_SRC_DIR}/sdl_ttf.c"
    "${GSDL2_SRC_DIR}/sensor.c"
    "${GSDL2_SRC_DIR}/shape.c"
    "${GSDL2_SRC_DIR}/stdinc.c"
    "${GSDL2_SRC_DIR}/surface.c"
    "${GSDL2_SRC_DIR}/system.c"
    "${GSDL2_SRC_DIR}/syswm.c"
    "${GSDL2_SRC_DIR}/thread.c"
    "${GSDL2_SRC_DIR}/timer.c"
    "${GSDL2_SRC_DIR}/touch.c"
    "${GSDL2_SRC_DIR}/version.c"
    "${GSDL2_SRC_DIR}/video.c"
    # SDL_FontCache
    "${DEPS_DIR}/SDL_FontCache/SDL_FontCache.c"
)
add_library(gsdl2 STATIC ${GSDL2_SOURCES})
target_include_directories(gsdl2 PRIVATE
    "${GSDL2_SRC_DIR}"
    "${SDL_INCLUDE_DIR}"                       # SDL.h
    "${DEPS_DIR}/SDL_image"                    # SDL_image.h
    "${DEPS_DIR}/SDL_Mixer_X/include"          # SDL_mixer_ext.h
    "${DEPS_DIR}/SDL_ttf"                      # SDL_ttf.h
    "${DEPS_DIR}/SDL_FontCache"                # SDL_FontCache.h
    "${DEPS_DIR}/libsndfile/include"            # sndfile.h (sdl_mixer.c 高质量 WAV 加载)
)
target_link_libraries(gsdl2 PUBLIC lua54)

# ============================================================================
# 11b. gimgui 静态库 (ImGui Lua 绑定)
# ============================================================================
set(GIMGUI_SRC_DIR "${SOURCES_DIR}/lib/gimgui")
set(GIMGUI_SOURCES
    "${GIMGUI_SRC_DIR}/imgui/imgui.cpp"
    "${GIMGUI_SRC_DIR}/imgui/imgui_demo.cpp"
    "${GIMGUI_SRC_DIR}/imgui/imgui_draw.cpp"
    "${GIMGUI_SRC_DIR}/imgui/imgui_tables.cpp"
    "${GIMGUI_SRC_DIR}/imgui/imgui_widgets.cpp"
    "${GIMGUI_SRC_DIR}/imgui/imgui_user.cpp"
    "${GIMGUI_SRC_DIR}/backends/imgui_impl_sdl2.cpp"
    "${GIMGUI_SRC_DIR}/backends/imgui_impl_sdlrenderer2.cpp"
    "${GIMGUI_SRC_DIR}/lua_DrawList.cpp"
    "${GIMGUI_SRC_DIR}/lua_ImGui.cpp"
    "${GIMGUI_SRC_DIR}/help.cpp"
    "${GIMGUI_SRC_DIR}/main.cpp"
)

# ============================================================================
# =================== SDL2 生态系统 (与 PC 对齐) ============================
# ============================================================================

# ============================================================================
# 12. SDL2 静态库 (核心渲染/事件框架)
# ============================================================================
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/SDL" "${CMAKE_BINARY_DIR}/SDL2" EXCLUDE_FROM_ALL)

# SDL_image/SDL_ttf 的 sdl_find_sdl2() 宏需要 SDL2::SDL2-static 目标
# SDL2 的 CMakeLists 不会自动创建此别名
if(TARGET SDL2-static AND NOT TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2-static ALIAS SDL2-static)
endif()
# 覆盖工具链 -fvisibility=hidden，确保 SDL_* 符号在动态 framework 中可见
target_compile_options(SDL2-static PRIVATE "-fvisibility=default")

# ============================================================================
# 13. SDL_image 静态库 (图片加载)
# ============================================================================
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# iOS 上使用 ImageIO 原生加载 (支持 JPG/PNG/WEBP 等所有格式)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_IMAGEIO ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
# 不需要外部 libpng/libjpeg (ImageIO + stb 已覆盖)
set(SDL2IMAGE_JPG OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JXL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/SDL_image" "${CMAKE_BINARY_DIR}/SDL_image" EXCLUDE_FROM_ALL)
# 覆盖工具链 -fvisibility=hidden，确保 IMG_* 符号在动态 framework 中可见
target_compile_options(SDL2_image PRIVATE "-fvisibility=default")

# ============================================================================
# 14. SDL_ttf 静态库 (字体渲染 — VENDORED 模式自动构建 freetype + harfbuzz)
# ============================================================================
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)  # 让 SDL_ttf 内部管理 freetype/harfbuzz
set(SDL2TTF_HARFBUZZ ON CACHE BOOL "" FORCE)
# freetype 配置 (通过 SDL_ttf VENDORED 传递)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
# harfbuzz 配置
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CORETEXT ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/SDL_ttf" "${CMAKE_BINARY_DIR}/SDL_ttf" EXCLUDE_FROM_ALL)
# 覆盖工具链 -fvisibility=hidden，确保 FT_*/hb_*/TTF_* 符号在动态 framework 中可见
target_compile_options(SDL2_ttf PRIVATE "-fvisibility=default")
target_compile_options(freetype PRIVATE "-fvisibility=default")
target_compile_options(harfbuzz PRIVATE "-fvisibility=default")

# ============================================================================
# 15. libsndfile 静态库 (高质量音频文件 I/O)
# ============================================================================
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_REGTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_MPEG OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/libsndfile" "${CMAKE_BINARY_DIR}/libsndfile" EXCLUDE_FROM_ALL)
# 覆盖工具链 -fvisibility=hidden，确保 sndfile 符号在 SDL_Mixer_X_fw 中可见
target_compile_options(sndfile PRIVATE "-fvisibility=default")

# ============================================================================
# 16a. libogg 静态库 (OGG 容器格式 — vorbisidec 的依赖)
# ============================================================================
set(AUDIO_CODECS_DIR "${DEPS_DIR}/audio_codecs")
set(LIBOGG_DIR "${AUDIO_CODECS_DIR}/libogg-1.3.2")
set(LIBOGG_SOURCES
    "${LIBOGG_DIR}/src/bitwise.c"
    "${LIBOGG_DIR}/src/framing.c"
)
add_library(ogg STATIC ${LIBOGG_SOURCES})
target_include_directories(ogg PUBLIC
    "${LIBOGG_DIR}/include"
    "${LIBOGG_DIR}/android"           # config_types.h (Android/iOS 通用)
)
target_compile_options(ogg PRIVATE "-fvisibility=default")

# ============================================================================
# 16b. libvorbisidec 静态库 (OGG Tremor 整数解码器 — 与 PC/Android 一致)
# ============================================================================
set(VORBISIDEC_DIR "${AUDIO_CODECS_DIR}/libvorbisidec-1.2.1")
set(VORBISIDEC_SOURCES
    "${VORBISIDEC_DIR}/block.c"
    "${VORBISIDEC_DIR}/codebook.c"
    "${VORBISIDEC_DIR}/floor0.c"
    "${VORBISIDEC_DIR}/floor1.c"
    "${VORBISIDEC_DIR}/info.c"
    "${VORBISIDEC_DIR}/mapping0.c"
    "${VORBISIDEC_DIR}/mdct.c"
    "${VORBISIDEC_DIR}/registry.c"
    "${VORBISIDEC_DIR}/res012.c"
    "${VORBISIDEC_DIR}/sharedbook.c"
    "${VORBISIDEC_DIR}/synthesis.c"
    "${VORBISIDEC_DIR}/window.c"
    "${VORBISIDEC_DIR}/vorbisfile.c"
)
add_library(vorbisidec STATIC ${VORBISIDEC_SOURCES})
target_include_directories(vorbisidec PUBLIC "${VORBISIDEC_DIR}")
target_compile_definitions(vorbisidec PRIVATE
    HAVE_ALLOCA
    FLOAT_LOOKUP
    INT_LOOKUP
)
target_link_libraries(vorbisidec PUBLIC ogg)
target_compile_options(vorbisidec PRIVATE "-fvisibility=default")

# ============================================================================
# 16c. libmodplug 静态库 (MOD 音乐格式解码器)
# ============================================================================
set(MODPLUG_DIR "${AUDIO_CODECS_DIR}/libmodplug-0.8.9.0")
set(MODPLUG_SOURCES
    "${MODPLUG_DIR}/src/fastmix.cpp"
    "${MODPLUG_DIR}/src/load_669.cpp"
    "${MODPLUG_DIR}/src/load_abc.cpp"
    "${MODPLUG_DIR}/src/load_amf.cpp"
    "${MODPLUG_DIR}/src/load_ams.cpp"
    "${MODPLUG_DIR}/src/load_dbm.cpp"
    "${MODPLUG_DIR}/src/load_dmf.cpp"
    "${MODPLUG_DIR}/src/load_dsm.cpp"
    "${MODPLUG_DIR}/src/load_far.cpp"
    "${MODPLUG_DIR}/src/load_gdm.cpp"
    "${MODPLUG_DIR}/src/load_it.cpp"
    "${MODPLUG_DIR}/src/load_mdl.cpp"
    "${MODPLUG_DIR}/src/load_med.cpp"
    "${MODPLUG_DIR}/src/load_mid.cpp"
    "${MODPLUG_DIR}/src/load_mod.cpp"
    "${MODPLUG_DIR}/src/load_mt2.cpp"
    "${MODPLUG_DIR}/src/load_mtm.cpp"
    "${MODPLUG_DIR}/src/load_okt.cpp"
    "${MODPLUG_DIR}/src/load_pat.cpp"
    "${MODPLUG_DIR}/src/load_psm.cpp"
    "${MODPLUG_DIR}/src/load_ptm.cpp"
    "${MODPLUG_DIR}/src/load_s3m.cpp"
    "${MODPLUG_DIR}/src/load_stm.cpp"
    "${MODPLUG_DIR}/src/load_ult.cpp"
    "${MODPLUG_DIR}/src/load_umx.cpp"
    "${MODPLUG_DIR}/src/load_wav.cpp"
    "${MODPLUG_DIR}/src/load_xm.cpp"
    "${MODPLUG_DIR}/src/mmcmp.cpp"
    "${MODPLUG_DIR}/src/modplug.cpp"
    "${MODPLUG_DIR}/src/snd_dsp.cpp"
    "${MODPLUG_DIR}/src/snd_flt.cpp"
    "${MODPLUG_DIR}/src/snd_fx.cpp"
    "${MODPLUG_DIR}/src/sndfile.cpp"
    "${MODPLUG_DIR}/src/sndmix.cpp"
)
add_library(modplug STATIC ${MODPLUG_SOURCES})
target_include_directories(modplug PUBLIC
    "${MODPLUG_DIR}/src"
    "${MODPLUG_DIR}/src/libmodplug"
)
target_compile_definitions(modplug PRIVATE
    MODPLUG_BUILD
    MODPLUG_STATIC
    MMCMP_SUPPORT
    HAVE_STDINT_H
    HAVE_SINF
    NO_CXX_EXPORTS
)
target_compile_options(modplug PRIVATE "-fvisibility=default")

# ============================================================================
# 16d. libmpg123 静态库 (MP3 解码器 — ARM64 NEON 优化)
# ============================================================================
set(MPG123_DIR "${AUDIO_CODECS_DIR}/mpg123-1.25.13")

# 公共源文件 (与 Android.mk 对齐)
set(MPG123_SOURCES
    "${MPG123_DIR}/src/libmpg123/parse.c"
    "${MPG123_DIR}/src/libmpg123/frame.c"
    "${MPG123_DIR}/src/libmpg123/format.c"
    "${MPG123_DIR}/src/libmpg123/dct64.c"
    "${MPG123_DIR}/src/libmpg123/equalizer.c"
    "${MPG123_DIR}/src/libmpg123/id3.c"
    "${MPG123_DIR}/src/libmpg123/optimize.c"
    "${MPG123_DIR}/src/libmpg123/readers.c"
    "${MPG123_DIR}/src/libmpg123/tabinit.c"
    "${MPG123_DIR}/src/libmpg123/libmpg123.c"
    "${MPG123_DIR}/src/libmpg123/index.c"
    "${MPG123_DIR}/src/compat/compat_str.c"
    "${MPG123_DIR}/src/compat/compat.c"
    # ARM64 NEON 优化源文件 (iOS 真机)
    "${MPG123_DIR}/src/libmpg123/stringbuf.c"
    "${MPG123_DIR}/src/libmpg123/icy.c"
    "${MPG123_DIR}/src/libmpg123/icy2utf8.c"
    "${MPG123_DIR}/src/libmpg123/ntom.c"
    "${MPG123_DIR}/src/libmpg123/synth.c"
    "${MPG123_DIR}/src/libmpg123/synth_8bit.c"
    "${MPG123_DIR}/src/libmpg123/layer1.c"
    "${MPG123_DIR}/src/libmpg123/layer2.c"
    "${MPG123_DIR}/src/libmpg123/layer3.c"
    "${MPG123_DIR}/src/libmpg123/synth_s32.c"
    "${MPG123_DIR}/src/libmpg123/synth_real.c"
    "${MPG123_DIR}/src/libmpg123/dither.c"
    "${MPG123_DIR}/src/libmpg123/feature.c"
    "${MPG123_DIR}/src/libmpg123/getcpuflags_arm.c"
)

# NEON64 汇编文件 (仅 ARM64)
set(MPG123_NEON64_ASM
    "${MPG123_DIR}/src/libmpg123/dct36_neon64.S"
    "${MPG123_DIR}/src/libmpg123/dct64_neon64_float.S"
    "${MPG123_DIR}/src/libmpg123/synth_neon64_float.S"
    "${MPG123_DIR}/src/libmpg123/synth_neon64_s32.S"
    "${MPG123_DIR}/src/libmpg123/synth_stereo_neon64_float.S"
    "${MPG123_DIR}/src/libmpg123/synth_stereo_neon64_s32.S"
    "${MPG123_DIR}/src/libmpg123/dct64_neon64.S"
    "${MPG123_DIR}/src/libmpg123/synth_neon64.S"
    "${MPG123_DIR}/src/libmpg123/synth_stereo_neon64.S"
    "${MPG123_DIR}/src/libmpg123/check_neon.S"
)
list(APPEND MPG123_SOURCES ${MPG123_NEON64_ASM})

add_library(mpg123 STATIC ${MPG123_SOURCES})
target_include_directories(mpg123 PUBLIC
    "${MPG123_DIR}/android"            # config.h + mpg123.h (预配置，Android/iOS 通用)
    "${MPG123_DIR}/src"
    "${MPG123_DIR}/src/compat"
    "${MPG123_DIR}/src/libmpg123"
)
target_compile_definitions(mpg123 PRIVATE
    OPT_MULTI OPT_GENERIC OPT_GENERIC_DITHER OPT_NEON64
    REAL_IS_FLOAT
)
target_compile_options(mpg123 PRIVATE "-fvisibility=default")

# ============================================================================
# 16. SDL_Mixer_X 静态库 (手动源文件列表 — cmake 模块文件缺失无法 add_subdirectory)
#     与 PC vcxproj 完全一致的源文件和编译定义
# ============================================================================
set(MIXERX_DIR "${DEPS_DIR}/SDL_Mixer_X")
set(MIXERX_SRC "${MIXERX_DIR}/src")
set(MIXERX_CODECS "${MIXERX_SRC}/codecs")

# --- 核心源文件 (与 PC vcxproj 一致) ---
set(SDL_MIXER_X_SOURCES
    "${MIXERX_SRC}/utils.c"
    "${MIXERX_SRC}/effects_internal.c"
    "${MIXERX_SRC}/effect_position.c"
    "${MIXERX_SRC}/effect_stereoreverse.c"
    "${MIXERX_SRC}/mixer.c"
    "${MIXERX_SRC}/music.c"
    "${MIXERX_SRC}/mixer_x_deprecated.c"
    "${MIXERX_SRC}/stream_custom.c"
    # --- Codecs ---
    "${MIXERX_CODECS}/load_aiff.c"
    "${MIXERX_CODECS}/load_voc.c"
    "${MIXERX_CODECS}/mp3utils.c"
    "${MIXERX_CODECS}/music_wav.c"
    "${MIXERX_CODECS}/music_ogg.c"
    "${MIXERX_CODECS}/music_flac.c"
    "${MIXERX_CODECS}/music_drflac.c"
    "${MIXERX_CODECS}/music_mpg123.c"
    "${MIXERX_CODECS}/music_drmp3.c"
    "${MIXERX_CODECS}/music_modplug.c"
    "${MIXERX_CODECS}/music_timidity.c"
    "${MIXERX_CODECS}/music_cmd.c"
    "${MIXERX_CODECS}/music_nativemidi.c"
    "${MIXERX_CODECS}/music_opus.c"
    "${MIXERX_CODECS}/music_fluidsynth.c"
    # --- Timidity 内置 MIDI 合成器 ---
    "${MIXERX_CODECS}/timidity/common.c"
    "${MIXERX_CODECS}/timidity/instrum.c"
    "${MIXERX_CODECS}/timidity/mix.c"
    "${MIXERX_CODECS}/timidity/output.c"
    "${MIXERX_CODECS}/timidity/playmidi.c"
    "${MIXERX_CODECS}/timidity/readmidi.c"
    "${MIXERX_CODECS}/timidity/resample.c"
    "${MIXERX_CODECS}/timidity/tables.c"
    "${MIXERX_CODECS}/timidity/timidity.c"
)

add_library(SDL2_mixer_ext_Static STATIC ${SDL_MIXER_X_SOURCES})
target_include_directories(SDL2_mixer_ext_Static PRIVATE
    "${MODPLUG_DIR}/src"                        # modplug.h
    "${MIXERX_DIR}/include"
    "${MIXERX_SRC}"
    "${MIXERX_CODECS}"
    "${MIXERX_CODECS}/timidity"
    "${MIXERX_CODECS}/dr_libs"
    "${SDL_INCLUDE_DIR}"                        # SDL.h
    "${AUDIO_CODECS_DIR}/include"               # audio_codecs 公共头文件
    "${AUDIO_CODECS_DIR}/libvorbisidec-1.2.1"   # ivorbisfile.h (OGG Tremor)
)
target_include_directories(SDL2_mixer_ext_Static PUBLIC
    "${MIXERX_DIR}/include"
)

# --- 与 PC 完全一致的编译定义 ---
target_compile_definitions(SDL2_mixer_ext_Static PRIVATE
    MUSIC_MOD_MODPLUG
    MUSIC_WAV
    MUSIC_OGG
    MUSIC_FLAC
    MUSIC_FLAC_DRFLAC
    MUSIC_MP3_MPG123
    MUSIC_MP3_DRMP3
    MUSIC_MID_TIMIDITY
    # iOS 不支持 Windows native MIDI，不定义 MUSIC_MID_NATIVE
    MODPLUG_STATIC
    HAVE_INTTYPES_H
    HAVE_SETENV
    HAVE_SINF
    PIC
    _REENTRANT
    _USE_MATH_DEFINES
    # OGG 使用 Tremor (整数解码器) — 与 PC 一致
    OGG_USE_TREMOR
    OGG_HEADER=<ivorbisfile.h>
)

# --- 链接 SDL2 和编解码器静态库 ---
target_link_libraries(SDL2_mixer_ext_Static PRIVATE SDL2-static modplug vorbisidec ogg mpg123)

# ============================================================================
# 安装规则 — 收集所有 .a 到统一输出目录
# ============================================================================
install(TARGETS lua54 zlib libhv ghv ggelua mygxy
                hiredis sqlite3 lsqlite3 gastar gsdl2
                SDL2-static SDL2_image SDL2_ttf
                freetype harfbuzz
                sndfile ogg vorbisidec modplug mpg123
                SDL2_mixer_ext_Static
    ARCHIVE DESTINATION lib
)

# 导出头文件
install(FILES "${LUA_SRC_DIR}/lua.h" "${LUA_SRC_DIR}/luaconf.h"
              "${LUA_SRC_DIR}/lualib.h" "${LUA_SRC_DIR}/lauxlib.h"
    DESTINATION include
)

# ============================================================================
# =================== 动态 Framework 构建 ===================================
# ============================================================================
# 将静态库重新链接为动态 .framework，与旧版模版 1:1 对齐
# 每个 framework 通过 @rpath 被主可执行文件 GGELUA 加载
# ============================================================================

# --- 通用 framework 属性 ---
set(IOS_SYSTEM_FRAMEWORKS
    "-framework UIKit"
    "-framework Foundation"
)
# dylib 最低版本 (Xcode 16+ 最低支持 12.0，与 toolchain 默认 15.0 保持一致)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "" FORCE)

# ---- 辅助宏：创建 iOS 动态 framework ----
macro(add_ios_framework FW_TARGET FW_OUTPUT_NAME)
    set_target_properties(${FW_TARGET} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        OUTPUT_NAME "${FW_OUTPUT_NAME}"
        MACOSX_FRAMEWORK_IDENTIFIER "org.ggelua.${FW_OUTPUT_NAME}"
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_DYLIB_INSTALL_NAME_BASE "@rpath"
        # 避免 Xcode 生成多余的 Headers 和 Modules 子目录
        PUBLIC_HEADER ""
    )
endmacro()

# ============================================================================
# F1. libggelua.framework — 引擎核心
#     静态链接: lua54 + zlib + ghv + libhv + libcrypto
# ============================================================================
add_library(ggelua_fw SHARED
    ${GGELUA_SOURCES}
    ${LUA_SOURCES}
    ${ZLIB_SOURCES}
    ${GHV_SOURCES}
    ${LIBHV_SOURCES}
)
add_ios_framework(ggelua_fw "libggelua")
target_include_directories(ggelua_fw PRIVATE
    "${GGELUA_SRC_DIR}"
    "${GGELUA_SRC_DIR}/uuid"
    "${GGELUA_SRC_DIR}/print"
    "${LUA_SRC_DIR}"
    "${ZLIB_SRC_DIR}"
    "${GHV_SRC_DIR}"
    "${DEPS_DIR}/openssl/include"
    "${DEPS_DIR}/SQLite3MultipleCiphers/src"
    "${SDL_INCLUDE_DIR}"
    # libhv include dirs (hconfig.h 已统一至 LIBHV_DIR 根目录)
    "${LIBHV_DIR}" "${LIBHV_DIR}/base" "${LIBHV_DIR}/event"
    "${LIBHV_DIR}/evpp" "${LIBHV_DIR}/http" "${LIBHV_DIR}/http/client"
    "${LIBHV_DIR}/ssl" "${LIBHV_DIR}/cpputil" "${LIBHV_DIR}/util"
)
target_compile_definitions(ggelua_fw PRIVATE
    LUA_USE_IOS LUA_COMPAT_APIINTCASTS
    LUA_USE_POSIX
    HAS_AES_HARDWARE=0
    HV_STATICLIB HV_WITHOUT_SSL EVENT_KQUEUE
)
# 工具链全局 -fvisibility=hidden 会隐藏所有符号。
# luaconf.h 的 LUAMOD_API/LUA_API 仅在 __ELF__ 平台添加 visibility("default")，
# iOS (Mach-O) 不会自动导出。此处显式覆盖，确保 luaopen_ggelua 等符号可见。
target_compile_options(ggelua_fw PRIVATE "-fvisibility=default")
add_dependencies(ggelua_fw SDL2_fw)
target_link_libraries(ggelua_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    "${OPENSSL_CRYPTO_LIB}"
    ${IOS_SYSTEM_FRAMEWORKS}
    SDL2_fw
)

# ============================================================================
# F2. libgsdl2.framework — SDL2 Lua 绑定
# ============================================================================
add_library(gsdl2_fw SHARED ${GSDL2_SOURCES})
add_ios_framework(gsdl2_fw "libgsdl2")
target_include_directories(gsdl2_fw PRIVATE
    "${GSDL2_SRC_DIR}"
    "${SDL_INCLUDE_DIR}"
    "${DEPS_DIR}/SDL_image"
    "${DEPS_DIR}/SDL_Mixer_X/include"
    "${DEPS_DIR}/SDL_ttf"
    "${DEPS_DIR}/SDL_FontCache"
    "${DEPS_DIR}/libsndfile/include"
    "${LUA_SRC_DIR}"
)
target_compile_definitions(gsdl2_fw PRIVATE LUA_USE_IOS LUA_COMPAT_APIINTCASTS)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_gsdl2 等符号在动态 framework 中可见
target_compile_options(gsdl2_fw PRIVATE "-fvisibility=default")
# 显式链接所有 SDL framework 依赖，确保 IMG_*/TTF_*/Mix_* 等符号在 dlopen 时可解析
add_dependencies(gsdl2_fw ggelua_fw SDL2_fw SDL2_image_fw SDL2_ttf_fw SDL_Mixer_X_fw)
target_link_libraries(gsdl2_fw PRIVATE
    ggelua_fw
    SDL2_fw
    SDL2_image_fw
    SDL2_ttf_fw
    SDL_Mixer_X_fw
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F3. libmygxy.framework — 游戏特有模块
# ============================================================================
add_library(mygxy_fw SHARED ${MYGXY_SOURCES})
add_ios_framework(mygxy_fw "libmygxy")
target_include_directories(mygxy_fw PRIVATE
    "${MYGXY_SRC_DIR}"
    "${SDL_INCLUDE_DIR}"
    "${DEPS_DIR}/SDL_image"
    "${DEPS_DIR}/zstd" "${DEPS_DIR}/zstd/common"
    "${DEPS_DIR}/lz4" "${DEPS_DIR}/zlib"
    "${LUA_SRC_DIR}"
    "${ENGINE_ROOT}/external/vgmstream/src"
)
target_compile_definitions(mygxy_fw PRIVATE
    ZSTD_DISABLE_ASM=1
    LUA_USE_IOS LUA_COMPAT_APIINTCASTS
)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_mygxy 等符号在动态 framework 中可见
target_compile_options(mygxy_fw PRIVATE "-fvisibility=default")
# lz4 xxhash 命名空间隔离
set_source_files_properties(
    "${DEPS_DIR}/lz4/xxhash.c"
    "${DEPS_DIR}/lz4/lz4hc.c"
    "${DEPS_DIR}/lz4/lz4frame.c"
    TARGET_DIRECTORY mygxy_fw
    PROPERTIES COMPILE_DEFINITIONS "XXH_NAMESPACE=LZ4_"
)
add_dependencies(mygxy_fw ggelua_fw)
target_link_libraries(mygxy_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F4. libgastar.framework — A* 寻路
# ============================================================================
add_library(gastar_fw SHARED ${GASTAR_SOURCES})
add_ios_framework(gastar_fw "libgastar")
target_include_directories(gastar_fw PRIVATE "${GASTAR_SRC_DIR}" "${LUA_SRC_DIR}")
target_compile_definitions(gastar_fw PRIVATE LUA_USE_IOS LUA_COMPAT_APIINTCASTS)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_gastar 符号在动态 framework 中可见
target_compile_options(gastar_fw PRIVATE "-fvisibility=default")
add_dependencies(gastar_fw ggelua_fw)
target_link_libraries(gastar_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    "-lc++"  # Astart.cpp, DLinkList.cpp 是 C++ 代码
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F5. liblsqlite3.framework — SQLite3 Lua 绑定 + 加密数据库
# ============================================================================
add_library(lsqlite3_fw SHARED
    "${LSQLITE3_SRC_DIR}/lsqlite3.c"
    "${SQLITE3MC_SRC_DIR}/sqlite3mc.c"
)
add_ios_framework(lsqlite3_fw "liblsqlite3")
target_include_directories(lsqlite3_fw PRIVATE
    "${SQLITE3MC_SRC_DIR}" "${LUA_SRC_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
)
target_compile_definitions(lsqlite3_fw PRIVATE
    HAS_AES_HARDWARE=0
    SQLITE_HAS_CODEC SQLITE_THREADSAFE=1 SQLITE_DQS=0
    SQLITE_MAX_ATTACHED=10 SQLITE_ENABLE_COLUMN_METADATA=1
    SQLITE_SECURE_DELETE=1 SQLITE_ENABLE_FTS3=1
    SQLITE_ENABLE_FTS3_PARENTHESIS=1 SQLITE_ENABLE_FTS4=1
    SQLITE_ENABLE_FTS5=1 SQLITE_ENABLE_JSON1=1
    SQLITE_ENABLE_RTREE=1 SQLITE_ENABLE_GEOPOLY=1
    SQLITE_CORE=1 SQLITE_ENABLE_MATH_FUNCTIONS=1
    SQLITE_TEMP_STORE=2 SQLITE_USE_URI=1
    LUA_USE_IOS LUA_COMPAT_APIINTCASTS
)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_lsqlite3 符号在动态 framework 中可见
target_compile_options(lsqlite3_fw PRIVATE "-fvisibility=default")
add_dependencies(lsqlite3_fw ggelua_fw)
target_link_libraries(lsqlite3_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F6. libhiredis.framework — Redis 客户端 (新增)
# ============================================================================
add_library(hiredis_fw SHARED ${HIREDIS_SOURCES})
add_ios_framework(hiredis_fw "libhiredis")
target_include_directories(hiredis_fw PRIVATE
    "${HIREDIS_SRC_DIR}" "${HIREDIS_LIB_DIR}" "${LUA_SRC_DIR}"
)
target_compile_definitions(hiredis_fw PRIVATE LUA_USE_IOS LUA_COMPAT_APIINTCASTS)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_hiredis 符号在动态 framework 中可见
target_compile_options(hiredis_fw PRIVATE "-fvisibility=default")
add_dependencies(hiredis_fw ggelua_fw)
target_link_libraries(hiredis_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F7. libgimgui.framework — ImGui Lua 绑定
# ============================================================================
add_library(gimgui_fw SHARED ${GIMGUI_SOURCES})
add_ios_framework(gimgui_fw "libgimgui")
target_include_directories(gimgui_fw PRIVATE
    "${GIMGUI_SRC_DIR}"
    "${GIMGUI_SRC_DIR}/imgui"
    "${SDL_INCLUDE_DIR}"
    "${LUA_SRC_DIR}"
)
target_compile_definitions(gimgui_fw PRIVATE
    IMGUI_INCLUDE_IMGUI_USER_H
    LUA_USE_IOS LUA_COMPAT_APIINTCASTS
)
# 覆盖工具链 -fvisibility=hidden，确保 luaopen_gimgui 符号在动态 framework 中可见
target_compile_options(gimgui_fw PRIVATE "-fvisibility=default")
add_dependencies(gimgui_fw ggelua_fw SDL2_fw)
target_link_libraries(gimgui_fw PRIVATE
    ggelua_fw
    SDL2_fw
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F8. SDL2.framework — 核心渲染/事件 (动态导出)
# ============================================================================
# SDL2 由 add_subdirectory 生成的 SDL2-static 重链为动态库
add_library(SDL2_fw SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/fw_stub.c"
    $<TARGET_OBJECTS:SDL2-static>
)
add_dependencies(SDL2_fw SDL2-static)
add_ios_framework(SDL2_fw "SDL2")
target_include_directories(SDL2_fw PRIVATE "${SDL_INCLUDE_DIR}")
target_link_libraries(SDL2_fw PRIVATE
    "-framework UIKit" "-framework Foundation"
    "-framework Metal" "-framework CoreGraphics"
    "-framework CoreMotion" "-framework AVFoundation"
    "-framework GameController" "-framework QuartzCore"
    "-framework CoreHaptics" "-framework OpenGLES"
    "-framework AudioToolbox" "-framework CoreAudio"
    "-framework CoreBluetooth" "-framework CoreServices"
)

# ============================================================================
# F8. SDL2_image.framework
# ============================================================================
add_library(SDL2_image_fw SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/fw_stub.c"
    $<TARGET_OBJECTS:SDL2_image>
)
add_dependencies(SDL2_image_fw SDL2_image SDL2_fw)
add_ios_framework(SDL2_image_fw "SDL2_image")
target_link_libraries(SDL2_image_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    "-framework ImageIO" "-framework CoreServices"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F9. SDL2_ttf.framework (内含 freetype + harfbuzz)
# ============================================================================
add_library(SDL2_ttf_fw SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/fw_stub.c"
    $<TARGET_OBJECTS:SDL2_ttf>
    $<TARGET_OBJECTS:freetype>
    $<TARGET_OBJECTS:harfbuzz>
)
add_dependencies(SDL2_ttf_fw SDL2_ttf freetype harfbuzz SDL2_fw)
add_ios_framework(SDL2_ttf_fw "SDL2_ttf")
target_link_libraries(SDL2_ttf_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    "-framework CoreText" "-framework CoreFoundation"
    "-lc++"  # harfbuzz 是 C++ 代码 (TARGET_OBJECTS)
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F10. SDL-Mixer-X.framework (内含 sndfile + modplug + vorbisidec + mpg123)
# ============================================================================
add_library(SDL_Mixer_X_fw SHARED
    ${SDL_MIXER_X_SOURCES}
    # 使用 TARGET_OBJECTS 导入编解码器对象文件，每个编解码器用自己 target 的编译配置
    # 避免 sndfile.h 头文件冲突、NEON64 .S 汇编文件 Xcode 处理问题
    $<TARGET_OBJECTS:sndfile>
    $<TARGET_OBJECTS:modplug>
    $<TARGET_OBJECTS:vorbisidec>
    $<TARGET_OBJECTS:ogg>
    $<TARGET_OBJECTS:mpg123>
)
add_dependencies(SDL_Mixer_X_fw sndfile modplug vorbisidec ogg mpg123 SDL2_fw)
add_ios_framework(SDL_Mixer_X_fw "SDL-Mixer-X")
target_include_directories(SDL_Mixer_X_fw PRIVATE
    "${MIXERX_DIR}/include" "${MIXERX_SRC}" "${MIXERX_CODECS}"
    "${MIXERX_CODECS}/timidity" "${MIXERX_CODECS}/dr_libs"
    "${SDL_INCLUDE_DIR}"
    "${AUDIO_CODECS_DIR}/include"
    "${AUDIO_CODECS_DIR}/libvorbisidec-1.2.1"
    "${MODPLUG_DIR}/src"               # modplug.h (SDL_Mixer_X codecs 需要)
    "${DEPS_DIR}/libsndfile/include"
    "${MPG123_DIR}/android"             # mpg123.h (SDL_Mixer_X codecs 需要)
)
target_compile_definitions(SDL_Mixer_X_fw PRIVATE
    MUSIC_MOD_MODPLUG MUSIC_WAV MUSIC_OGG MUSIC_FLAC
    MUSIC_FLAC_DRFLAC MUSIC_MP3_MPG123 MUSIC_MP3_DRMP3
    MUSIC_MID_TIMIDITY MODPLUG_STATIC
    HAVE_INTTYPES_H HAVE_SETENV HAVE_SINF PIC _REENTRANT
    _USE_MATH_DEFINES OGG_USE_TREMOR OGG_HEADER=<ivorbisfile.h>
)
target_compile_options(SDL_Mixer_X_fw PRIVATE "-fvisibility=default")
target_link_libraries(SDL_Mixer_X_fw PRIVATE
    SDL2_fw
    "-framework AudioToolbox" "-framework CoreAudio"
    "-lc++"  # modplug 是 C++ 代码，需要链接 C++ 运行时
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F11. openssl.framework (libcrypto 动态导出)
# ============================================================================
add_library(openssl_fw SHARED "${CMAKE_CURRENT_SOURCE_DIR}/compat/openssl_fw_stub.c")
add_ios_framework(openssl_fw "openssl")
# -force_load 强制链入 libcrypto.a 的全部 .o，
# 否则链接器因 stub 无引用而丢弃所有 OpenSSL 符号（产出 32KB 空壳）
target_compile_options(openssl_fw PRIVATE "-fvisibility=default")
target_link_libraries(openssl_fw PRIVATE
    "-Wl,-force_load,${OPENSSL_CRYPTO_LIB}"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# F12. freetype.framework — 独立字体引擎 (兼容旧模板)
#      旧 GGELUA 主二进制对此有 LC_LOAD_DYLIB 强引用
#      旧 libgimgui.framework 导入 12 个 FT_* 符号
#      旧 SDL2_ttf.framework 导入 30 个 FT_* 符号
#      注: 新 SDL2_ttf_fw 已内联 freetype，此 framework 仅为旧模板兼容
# ============================================================================
add_library(freetype_fw SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/fw_stub.c"
    $<TARGET_OBJECTS:freetype>
)
add_dependencies(freetype_fw freetype)
add_ios_framework(freetype_fw "freetype")
target_link_libraries(freetype_fw PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    ${IOS_SYSTEM_FRAMEWORKS}
)

# ============================================================================
# 主可执行文件 GGELUA — 新增编译配置 (替代旧模板预编译二进制)
# ============================================================================
set(APP_SRC_DIR "${SOURCES_DIR}/app/ggelua")
set(APP_SOURCES
    "${APP_SRC_DIR}/main.c"
)

add_executable(GGELUA MACOSX_BUNDLE ${APP_SOURCES})
set_target_properties(GGELUA PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

target_include_directories(GGELUA PRIVATE
    "${APP_SRC_DIR}"
    "${LUA_SRC_DIR}"
    "${SDL_INCLUDE_DIR}"
)

# 确保主程序能通过头文件找到需要导出的符号
target_compile_definitions(GGELUA PRIVATE
    LUA_USE_IOS LUA_COMPAT_APIINTCASTS
)

# == 主程序属性配置 (iOS 特有) ==
set_target_properties(GGELUA PROPERTIES
    # 禁用代码签名，以便在 CI 跑通，打包 IPA 时再统一签名
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    # rpath 设置，使其能找到同级 Frameworks 目录下的动态库
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
    # --- Info.plist 元数据 (与参考模板 GGELUA.app 对齐) ---
    MACOSX_BUNDLE_BUNDLE_NAME "GGELUA"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.ggelua.game"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_COPYRIGHT "2023 baidwwy"
    MACOSX_BUNDLE_INFO_STRING "GGELUA Game Engine"
    MACOSX_BUNDLE_ICON_FILE "Icon"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.ggelua.game"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "GGEGAME"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
)

# == 链接所需的 Frameworks ==
# SDL_main 机制：main.c 定义的是 SDL_main()，真正的 main() 由
# SDL2main 静态库提供 (调用 SDL_UIKitRunApp → SDL_main)。
# SDL2 实现由 SDL2_fw 动态 framework 在运行时通过 @rpath 加载。
# 注意：不可同时链接 SDL2-static，否则两份 SDL2 代码并存导致
#   ObjC 类重复注册 + 符号冲突 → CODESIGNING Invalid Page 闪退
target_link_libraries(GGELUA PRIVATE
    ggelua_fw        # 引擎核心 (lua54+ghv+libhv+zlib, 动态)
    SDL2main         # main() → SDL_UIKitRunApp 跳板 (静态, 仅含入口代码)
    SDL2_fw          # SDL2 动态 framework (UIKit 集成 + 渲染 + 事件)
    # 系统 frameworks 由 SDL2_fw 内部链接, 此处仅需 UIKit/Foundation
    # 作为 ObjC runtime 的基础依赖
    "-framework UIKit"
    "-framework Foundation"
)

# ============================================================================
# 安装规则
# ============================================================================

# 安装所有动态 framework 到 Frameworks/ 目录
install(TARGETS
    ggelua_fw gsdl2_fw mygxy_fw gastar_fw lsqlite3_fw hiredis_fw gimgui_fw
    SDL2_fw SDL2_image_fw SDL2_ttf_fw SDL_Mixer_X_fw openssl_fw
    freetype_fw
    FRAMEWORK DESTINATION Frameworks
)

# 安装主程序
install(TARGETS GGELUA
    BUNDLE DESTINATION .
)

message(STATUS "==============================================")
message(STATUS " GGELUA3 iOS Build (With Main Executable)")
message(STATUS " 模式:       动态库 + 主执行程序")
message(STATUS " Platform:    ${IOS_PLATFORM}")
message(STATUS " Arch:        ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS " iOS Target:  ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS " Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS " Frameworks:  13 个动态库 (含 gimgui)")
message(STATUS "==============================================")
