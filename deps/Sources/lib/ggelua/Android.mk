# Save the local path
LUA_LOCAL_PATH := $(call my-dir)

# Restore local path
LOCAL_PATH := $(LUA_LOCAL_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := ggelua

GGE_LIB_PATH := $(LOCAL_PATH)/../../../source
LUA_PATH := $(GGE_LIB_PATH)/lua
LOCAL_C_INCLUDES += $(LUA_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)

DEPENDENT_LIB_PATH := $(LOCAL_PATH)/../../../../Dependencies
RIJNDAEL_PATH := $(DEPENDENT_LIB_PATH)/SQLite3MultipleCiphers/src
LOCAL_C_INCLUDES += $(RIJNDAEL_PATH)

LOCAL_SRC_FILES :=  cprint.c		\
                    hash.c			\
                    lbase64.c		\
                    lfs.c			\
                    lua_cmsgpack.c	\
                    lua_zlib.c		\
                    luuid.c			\
                    main.c			\
                    md5.c			\
                    lua_aes.c		\
                    core.c

LOCAL_SHARED_LIBRARIES := lua SDL2 zlib iconv

LOCAL_EXPORT_C_INCLUDES += $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -fPIC -DLUA_USE_DLOPEN -DLUA_USE_POSIX

include $(BUILD_SHARED_LIBRARY)
