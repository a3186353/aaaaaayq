# ============================================================================
# GGELUA3 Android 构建工程 (NDK CMake)
# ============================================================================
#
# 产出: 与预编译模板 jniLibs-all-abis/ 结构一致的 .so 共享库
#   {ABI}/liblua54.so        Lua 5.4 运行时
#   {ABI}/libggelua.so       引擎核心 (静态链入 ghv+libhv+zlib+cjson+hiredis)
#   {ABI}/libmygxy.so        游戏特有模块 (静态链入 zstd+lz4+zlib解压子集)
#   {ABI}/libgastar.so       A* 寻路
#   {ABI}/liblsqlite3.so     SQLite Lua 绑定 (静态链入 sqlite3)
#   {ABI}/libgsdl2.so        SDL2 Lua 绑定
#   SDL 系列 .so 由预编译提供, 不在此处构建
#
# 源文件列表基于旧 android-vs/ VS 项目精确对齐, 非 GLOB
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(GGELUA3_Android C CXX)

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SOURCES_DIR "${ENGINE_ROOT}/Sources")
set(DEPS_DIR    "${ENGINE_ROOT}/Dependencies")
set(SDL_INCLUDE_DIR "${DEPS_DIR}/SDL/include")
set(LIBHV_DIR   "${DEPS_DIR}/libhv")

# ============================================================================
# 预编译 SDL 系列 .so — IMPORTED (Android 7+ 要求 DT_NEEDED 显式依赖)
# PREBUILT_SDL_DIR 由 CI 传入, 本地构建时指向 jniLibs-all-abis/${ANDROID_ABI}
# ============================================================================
set(PREBUILT_SDL_DIR "" CACHE PATH "Directory containing prebuilt SDL .so files for current ABI")
if(NOT PREBUILT_SDL_DIR OR NOT EXISTS "${PREBUILT_SDL_DIR}/libSDL2.so")
    # 回退: 尝试旧 VS 项目输出目录
    set(_FALLBACK_DIR "${ENGINE_ROOT}/Projects/android-vs/app/GGELUA/app/src/main/jniLibs/${ANDROID_ABI}")
    if(EXISTS "${_FALLBACK_DIR}/libSDL2.so")
        set(PREBUILT_SDL_DIR "${_FALLBACK_DIR}")
    endif()
endif()

if(PREBUILT_SDL_DIR AND EXISTS "${PREBUILT_SDL_DIR}/libSDL2.so")
    message(STATUS "[SDL] 使用预编译 .so: ${PREBUILT_SDL_DIR}")

    add_library(SDL2_prebuilt SHARED IMPORTED)
    set_target_properties(SDL2_prebuilt PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_SDL_DIR}/libSDL2.so"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL_INCLUDE_DIR}"
    )

    add_library(SDL_image_prebuilt SHARED IMPORTED)
    set_target_properties(SDL_image_prebuilt PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_SDL_DIR}/libSDL_image.so"
    )

    add_library(SDL_mixer_prebuilt SHARED IMPORTED)
    set_target_properties(SDL_mixer_prebuilt PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_SDL_DIR}/libSDL_mixer.so"
    )

    add_library(SDL_ttf_prebuilt SHARED IMPORTED)
    set_target_properties(SDL_ttf_prebuilt PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_SDL_DIR}/libSDL_ttf.so"
    )

    set(HAS_PREBUILT_SDL TRUE)
else()
    message(WARNING "[SDL] 预编译 .so 未找到 (PREBUILT_SDL_DIR='${PREBUILT_SDL_DIR}'), "
                    "将回退到 --unresolved-symbols=ignore-all")
    set(HAS_PREBUILT_SDL FALSE)
endif()

# ============================================================================
# 1. lua54 — SHARED (所有模块的基础依赖)
# ============================================================================
set(LUA_SRC_DIR "${DEPS_DIR}/lua")
file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "(lua\\.c|luac\\.c|onelua\\.c|ltests\\.c)$")

add_library(lua54 SHARED ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC "${LUA_SRC_DIR}")
target_compile_definitions(lua54 PUBLIC
    LUA_USE_LINUX
    LUA_COMPAT_APIINTCASTS
    _LARGEFILE_SOURCE
    _FILE_OFFSET_BITS=64
)

# ============================================================================
# 2. zlib — STATIC (libggelua.so 静态链入)
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
set_target_properties(zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 3. libhv — STATIC (libggelua.so 静态链入)
# ============================================================================
set(LIBHV_SOURCES
    "${LIBHV_DIR}/base/hbase.c"
    "${LIBHV_DIR}/base/herr.c"
    "${LIBHV_DIR}/base/hlog.c"
    "${LIBHV_DIR}/base/hsocket.c"
    "${LIBHV_DIR}/base/htime.c"
    "${LIBHV_DIR}/base/hversion.c"
    "${LIBHV_DIR}/base/hmain.c"
    "${LIBHV_DIR}/event/hevent.c"
    "${LIBHV_DIR}/event/hloop.c"
    "${LIBHV_DIR}/event/epoll.c"
    "${LIBHV_DIR}/event/nio.c"
    "${LIBHV_DIR}/event/unpack.c"
    "${LIBHV_DIR}/event/noevent.c"
    "${LIBHV_DIR}/event/select.c"
    "${LIBHV_DIR}/http/HttpMessage.cpp"
    "${LIBHV_DIR}/http/HttpParser.cpp"
    "${LIBHV_DIR}/http/Http1Parser.cpp"
    "${LIBHV_DIR}/http/WebSocketParser.cpp"
    "${LIBHV_DIR}/http/WebSocketChannel.cpp"
    "${LIBHV_DIR}/http/websocket_parser.c"
    "${LIBHV_DIR}/http/wsdef.c"
    "${LIBHV_DIR}/http/client/AsyncHttpClient.cpp"
    "${LIBHV_DIR}/http/client/HttpClient.cpp"
    "${LIBHV_DIR}/http/server/HttpServer.cpp"
    "${LIBHV_DIR}/http/server/HttpHandler.cpp"
    "${LIBHV_DIR}/http/server/HttpService.cpp"
    "${LIBHV_DIR}/http/server/HttpMiddleware.cpp"
    "${LIBHV_DIR}/http/server/HttpResponseWriter.cpp"
    "${LIBHV_DIR}/http/server/FileCache.cpp"
    "${LIBHV_DIR}/http/server/http_page.cpp"
    "${LIBHV_DIR}/http/http_content.cpp"
    "${LIBHV_DIR}/http/httpdef.c"
    "${LIBHV_DIR}/http/http_parser.c"
    "${LIBHV_DIR}/http/multipart_parser.c"
    "${LIBHV_DIR}/cpputil/ThreadLocalStorage.cpp"
    "${LIBHV_DIR}/cpputil/hstring.cpp"
    "${LIBHV_DIR}/cpputil/hurl.cpp"
    "${LIBHV_DIR}/cpputil/hasync.cpp"
    "${LIBHV_DIR}/cpputil/hdir.cpp"
    "${LIBHV_DIR}/util/base64.c"
    "${LIBHV_DIR}/util/md5.c"
    "${LIBHV_DIR}/util/sha1.c"
    "${LIBHV_DIR}/ssl/nossl.c"
    "${LIBHV_DIR}/ssl/hssl.c"
)
add_library(libhv STATIC ${LIBHV_SOURCES})
target_include_directories(libhv PUBLIC
    "${LIBHV_DIR}" "${LIBHV_DIR}/base" "${LIBHV_DIR}/event"
    "${LIBHV_DIR}/evpp" "${LIBHV_DIR}/http" "${LIBHV_DIR}/http/client"
    "${LIBHV_DIR}/http/server" "${LIBHV_DIR}/ssl"
    "${LIBHV_DIR}/cpputil" "${LIBHV_DIR}/util"
)
target_compile_definitions(libhv PUBLIC HV_STATICLIB HV_WITHOUT_SSL EVENT_EPOLL)
set_target_properties(libhv PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 4. ghv — STATIC (libggelua.so 静态链入)
# ============================================================================
set(GHV_SRC_DIR "${SOURCES_DIR}/lib/ghv")
add_library(ghv STATIC
    "${GHV_SRC_DIR}/ghv_tcp_client.cpp"
    "${GHV_SRC_DIR}/ghv_tcp_server.cpp"
    "${GHV_SRC_DIR}/ghv_crypto.cpp"
    "${GHV_SRC_DIR}/ghv_http.cpp"
    "${GHV_SRC_DIR}/ghv_download.cpp"
    "${GHV_SRC_DIR}/ghv_websocket_server.cpp"
    "${GHV_SRC_DIR}/ghv_loop_init.c"
)
target_include_directories(ghv PRIVATE
    "${GHV_SRC_DIR}" "${DEPS_DIR}/openssl/include"
    "${LIBHV_DIR}/http/server" "${LIBHV_DIR}/evpp"
)
target_link_libraries(ghv PUBLIC lua54 libhv)
set_target_properties(ghv PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(OPENSSL_CRYPTO_LIB "" CACHE FILEPATH "Path to Android libcrypto.a (required for AEAD)")
if(OPENSSL_CRYPTO_LIB AND EXISTS "${OPENSSL_CRYPTO_LIB}")
    target_link_libraries(ghv PRIVATE "${OPENSSL_CRYPTO_LIB}")
    message(STATUS "[ghv] 链接 OpenSSL: ${OPENSSL_CRYPTO_LIB}")
else()
    message(FATAL_ERROR
        "[ghv] libcrypto.a 未找到: '${OPENSSL_CRYPTO_LIB}'\n"
        "  ghv_crypto.cpp 依赖 OpenSSL 3.x (EVP_CIPHER_fetch 等符号)\n"
        "  请使用 -DOPENSSL_CRYPTO_LIB=<path> 指定 libcrypto.a 路径\n"
        "  CI 环境: 参考 build_android.yml 中的 OpenSSL 交叉编译步骤")
endif()

# ============================================================================
# 5. hiredis — STATIC (libggelua.so 静态链入)
# ============================================================================
set(HIREDIS_SRC_DIR "${SOURCES_DIR}/lib/hiredis")
set(HIREDIS_LIB_DIR "${HIREDIS_SRC_DIR}/hiredis")
add_library(hiredis STATIC
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
target_include_directories(hiredis PRIVATE "${HIREDIS_SRC_DIR}" "${HIREDIS_LIB_DIR}")
target_link_libraries(hiredis PUBLIC lua54)
set_target_properties(hiredis PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 5b. physfs — STATIC (libggelua.so 静态链入, 虚拟文件系统)
# ============================================================================
set(PHYSFS_SRC_DIR "${DEPS_DIR}/physfs/src")
add_library(physfs STATIC
    "${PHYSFS_SRC_DIR}/physfs.c"
    "${PHYSFS_SRC_DIR}/physfs_byteorder.c"
    "${PHYSFS_SRC_DIR}/physfs_unicode.c"
    "${PHYSFS_SRC_DIR}/physfs_archiver_dir.c"
    "${PHYSFS_SRC_DIR}/physfs_archiver_unpacked.c"
    "${PHYSFS_SRC_DIR}/physfs_archiver_zip.c"
    "${PHYSFS_SRC_DIR}/physfs_platform_android.c"
    "${PHYSFS_SRC_DIR}/physfs_platform_posix.c"
    "${PHYSFS_SRC_DIR}/physfs_platform_unix.c"
)
target_include_directories(physfs PUBLIC "${PHYSFS_SRC_DIR}")
target_compile_definitions(physfs PRIVATE
    PHYSFS_SUPPORTS_ZIP=1
    PHYSFS_SUPPORTS_7Z=0
    PHYSFS_SUPPORTS_GRP=0
    PHYSFS_SUPPORTS_WAD=0
    PHYSFS_SUPPORTS_HOG=0
    PHYSFS_SUPPORTS_MVL=0
    PHYSFS_SUPPORTS_QPAK=0
    PHYSFS_SUPPORTS_SLB=0
    PHYSFS_SUPPORTS_VDF=0
    PHYSFS_SUPPORTS_ISO9660=0
    PHYSFS_SUPPORTS_CSM=0
    PHYSFS_SUPPORTS_LECARCHIVES=0
    PHYSFS_SUPPORTS_POD=0
    PHYSFS_SUPPORTS_ROFS=0
)
set_target_properties(physfs PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 6. libggelua.so — SHARED (引擎核心, 静态链入 ghv+libhv+zlib+cjson+hiredis+physfs)
# ============================================================================
set(GGELUA_SRC_DIR "${SOURCES_DIR}/lib/ggelua")
add_library(ggelua SHARED
    "${GGELUA_SRC_DIR}/hash.c"
    "${GGELUA_SRC_DIR}/lbase64.c"
    "${GGELUA_SRC_DIR}/lfs.c"
    "${GGELUA_SRC_DIR}/lua_cmsgpack.c"
    "${GGELUA_SRC_DIR}/lua_zlib.c"
    "${GGELUA_SRC_DIR}/main.c"
    "${GGELUA_SRC_DIR}/md5.c"
    "${GGELUA_SRC_DIR}/script.c"
    "${GGELUA_SRC_DIR}/lua_aes.c"
    "${GGELUA_SRC_DIR}/print/cprint.c"
    "${GGELUA_SRC_DIR}/nanoid/crypto_random.cpp"
    "${GGELUA_SRC_DIR}/nanoid/nanoid.cpp"
    "${GGELUA_SRC_DIR}/nanoid/lua.cpp"
    "${GGELUA_SRC_DIR}/uuid/lua.c"
    "${GGELUA_SRC_DIR}/uuid/uuid4.c"
    # cjson 内嵌
    "${SOURCES_DIR}/lib/cjson/lua_cjson.c"
    "${SOURCES_DIR}/lib/cjson/fpconv.c"
    "${SOURCES_DIR}/lib/cjson/strbuf.c"
    # PhysFS Lua 绑定
    "${SOURCES_DIR}/lib/gphysfs/gphysfs.c"
)
# core.c 仅存在于 Android/iOS 构建
if(EXISTS "${GGELUA_SRC_DIR}/core.c")
    target_sources(ggelua PRIVATE "${GGELUA_SRC_DIR}/core.c")
endif()
target_include_directories(ggelua PRIVATE
    "${GGELUA_SRC_DIR}" "${GGELUA_SRC_DIR}/uuid" "${GGELUA_SRC_DIR}/print"
    "${SOURCES_DIR}/lib/cjson" "${DEPS_DIR}/SQLite3MultipleCiphers/src"
    "${SDL_INCLUDE_DIR}" "${PHYSFS_SRC_DIR}"
)
target_link_libraries(ggelua PRIVATE ghv hiredis zlib physfs log)
target_link_libraries(ggelua PUBLIC lua54)
target_compile_definitions(ggelua PRIVATE LUA_USE_POSIX HAS_AES_HARDWARE=0)
target_compile_options(ggelua PRIVATE -Wno-format-security)
# main.c/script.c 引用 SDL 符号 + 其他未解析符号(libsndfile等)需容忍
target_link_options(ggelua PRIVATE "-Wl,--unresolved-symbols=ignore-all")
if(HAS_PREBUILT_SDL)
    target_link_libraries(ggelua PRIVATE SDL2_prebuilt)
endif()

# ============================================================================
# 7. libmygxy.so — SHARED (基于旧 android-vs/grr/mygxy 精确文件列表)
# ============================================================================
set(MYGXY_SRC_DIR "${SOURCES_DIR}/grr/mygxy")
add_library(mygxy SHARED
    # 核心源文件
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
    # zstd 解压
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
    # zlib 解压子集 (与旧 VS 项目一致: 仅 inflate 链路)
    "${DEPS_DIR}/zlib/adler32.c"
    "${DEPS_DIR}/zlib/crc32.c"
    "${DEPS_DIR}/zlib/inffast.c"
    "${DEPS_DIR}/zlib/inflate.c"
    "${DEPS_DIR}/zlib/inftrees.c"
    "${DEPS_DIR}/zlib/zutil.c"
)
target_include_directories(mygxy PRIVATE
    "${MYGXY_SRC_DIR}" "${SDL_INCLUDE_DIR}" "${DEPS_DIR}/SDL_image"
    "${DEPS_DIR}/zstd" "${DEPS_DIR}/zstd/common"
    "${DEPS_DIR}/lz4" "${DEPS_DIR}/zlib"
    "${ENGINE_ROOT}/external/vgmstream/src"
)
target_link_libraries(mygxy PUBLIC lua54 PRIVATE log)
# SDL 符号链接 + 其他未解析符号需容忍
target_link_options(mygxy PRIVATE "-Wl,--unresolved-symbols=ignore-all")
if(HAS_PREBUILT_SDL)
    target_link_libraries(mygxy PRIVATE SDL2_prebuilt SDL_image_prebuilt)
endif()
target_compile_definitions(mygxy PRIVATE ZSTD_DISABLE_ASM=1)
set_source_files_properties(
    "${DEPS_DIR}/lz4/xxhash.c" "${DEPS_DIR}/lz4/lz4hc.c" "${DEPS_DIR}/lz4/lz4frame.c"
    PROPERTIES COMPILE_DEFINITIONS "XXH_NAMESPACE=LZ4_"
)

# ============================================================================
# 8. liblsqlite3.so — SHARED (静态链入 sqlite3)
# ============================================================================
set(SQLITE3MC_SRC_DIR "${DEPS_DIR}/SQLite3MultipleCiphers/src")
add_library(sqlite3 STATIC "${SQLITE3MC_SRC_DIR}/sqlite3mc.c")
target_include_directories(sqlite3 PUBLIC "${SQLITE3MC_SRC_DIR}")
target_compile_definitions(sqlite3 PRIVATE
    HAS_AES_HARDWARE=0 SQLITE_HAS_CODEC SQLITE_THREADSAFE=1 SQLITE_DQS=0
    SQLITE_MAX_ATTACHED=10 SQLITE_ENABLE_COLUMN_METADATA=1
    SQLITE_SECURE_DELETE=1 SQLITE_ENABLE_FTS3=1 SQLITE_ENABLE_FTS3_PARENTHESIS=1
    SQLITE_ENABLE_FTS4=1 SQLITE_ENABLE_FTS5=1 SQLITE_ENABLE_JSON1=1
    SQLITE_ENABLE_RTREE=1 SQLITE_ENABLE_GEOPOLY=1 SQLITE_CORE=1
    SQLITE_ENABLE_MATH_FUNCTIONS=1 SQLITE_TEMP_STORE=2 SQLITE_USE_URI=1
)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(lsqlite3 SHARED "${SOURCES_DIR}/lib/lsqlite3/lsqlite3.c")
target_include_directories(lsqlite3 PRIVATE "${SQLITE3MC_SRC_DIR}")
target_link_libraries(lsqlite3 PUBLIC lua54 PRIVATE sqlite3)
target_compile_definitions(lsqlite3 PRIVATE SQLITE_HAS_CODEC)

# ============================================================================
# 9. libgastar.so — SHARED (基于旧 android-vs 精确文件列表: C++ 版)
# ============================================================================
set(GASTAR_SRC_DIR "${SOURCES_DIR}/lib/gastar")
add_library(gastar SHARED
    "${GASTAR_SRC_DIR}/Astart.cpp"
    "${GASTAR_SRC_DIR}/DLinkList.cpp"
    "${GASTAR_SRC_DIR}/lua.cpp"
    "${GASTAR_SRC_DIR}/memorypool.c"
)
target_include_directories(gastar PRIVATE "${GASTAR_SRC_DIR}")
target_link_libraries(gastar PUBLIC lua54)

# ============================================================================
# 10. libgsdl2.so — SHARED (SDL2 Lua 绑定)
# ============================================================================
set(GSDL2_SRC_DIR "${SOURCES_DIR}/lib/gsdl2")
file(GLOB GSDL2_SOURCES "${GSDL2_SRC_DIR}/*.c")
add_library(gsdl2 SHARED
    ${GSDL2_SOURCES}
    "${DEPS_DIR}/SDL_FontCache/SDL_FontCache.c"
)
target_include_directories(gsdl2 PRIVATE
    "${GSDL2_SRC_DIR}" "${SDL_INCLUDE_DIR}"
    "${DEPS_DIR}/SDL_image" "${DEPS_DIR}/SDL_Mixer_X/include"
    "${DEPS_DIR}/SDL_ttf" "${DEPS_DIR}/SDL_FontCache"
    "${DEPS_DIR}/libsndfile/include"
)
target_link_libraries(gsdl2 PUBLIC lua54)
# SDL 全家桶链接 + 其他未解析符号(libsndfile等)需容忍
target_link_options(gsdl2 PRIVATE "-Wl,--unresolved-symbols=ignore-all")
if(HAS_PREBUILT_SDL)
    target_link_libraries(gsdl2 PRIVATE SDL2_prebuilt SDL_image_prebuilt SDL_mixer_prebuilt SDL_ttf_prebuilt)
endif()

# ============================================================================
# 11. libmain.so — SHARED (SDL_main 入口 + GGE_LoadScript 脚本包加载)
#     包含 app/ggelua/main.c: SDL_main → GGE_LoadScript → ggelua.lua 引导
#     将 ggelua.com 中的 ggecore/ggepack 读入 Lua registry，
#     供 libggelua.so 中的 luaopen_ggelua 使用
# ============================================================================
set(APP_MAIN_SRC_DIR "${SOURCES_DIR}/app/ggelua")
add_library(main SHARED
    "${APP_MAIN_SRC_DIR}/main.c"
)
target_include_directories(main PRIVATE
    "${GGELUA_SRC_DIR}" "${SDL_INCLUDE_DIR}"
)
target_link_libraries(main PUBLIC lua54 PRIVATE ggelua)
# SDL 符号链接 + 未解析符号容忍
target_link_options(main PRIVATE "-Wl,--unresolved-symbols=ignore-all")
if(HAS_PREBUILT_SDL)
    target_link_libraries(main PRIVATE SDL2_prebuilt)
endif()

# ============================================================================
# 安装: 收集 .so 到 {ABI}/ 目录
# ============================================================================
set(SHARED_TARGETS lua54 ggelua mygxy lsqlite3 gastar gsdl2 main)
foreach(T IN LISTS SHARED_TARGETS)
    install(TARGETS ${T} LIBRARY DESTINATION ${ANDROID_ABI})
endforeach()

message(STATUS "=== GGELUA3 Android ${ANDROID_ABI} (${CMAKE_BUILD_TYPE}) ===")
message(STATUS "  Shared: ${SHARED_TARGETS}")
message(STATUS "  Static: zlib libhv ghv hiredis sqlite3 physfs")

