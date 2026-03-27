# Save the local path
LUA_LOCAL_PATH := $(call my-dir)

# Restore local path
LOCAL_PATH := $(LUA_LOCAL_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := lua

#LOCAL_MODULE_FILENAME := lua

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_SRC_FILES :=  lapi.c                  \
                    lauxlib.c               \
                    lbaselib.c              \
                    lcode.c                 \
                    lcorolib.c              \
                    lctype.c                \
                    ldblib.c                \
                    ldebug.c                \
                    ldo.c                   \
                    ldump.c                 \
                    lfunc.c                 \
                    lgc.c                   \
                    linit.c                 \
                    liolib.c                \
                    llex.c                  \
                    lmathlib.c              \
                    lmem.c                  \
                    loadlib.c               \
                    lobject.c               \
                    lopcodes.c              \
                    loslib.c                \
                    lparser.c               \
                    lstate.c                \
                    lstring.c               \
                    lstrlib.c               \
                    ltable.c                \
                    ltablib.c               \
                    ltm.c                   \
                    lundump.c               \
                    lutf8lib.c              \
                    lvm.c                   \
                    lzio.c

LOCAL_EXPORT_C_INCLUDES += $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -fPIC -DLUA_USE_POSIX -DLUA_UCID -DLUA_COMPAT_MATHLIB -DLUA_USE_DLOPEN

LOCAL_CFLAGS += -D"lua_getlocaledecpoint()='.'"

LOCAL_LDFLAGS += -lm

include $(BUILD_SHARED_LIBRARY)
