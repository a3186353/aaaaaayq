# Save the local path
LUA_LOCAL_PATH := $(call my-dir)

# Restore local path
LOCAL_PATH := $(LUA_LOCAL_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := gastar

GGE_LIB_PATH := $(LOCAL_PATH)/../../../../../GGELUA

LUA_PATH := $(GGE_LIB_PATH)/luadll

LOCAL_C_INCLUDES += $(LUA_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)

LOCAL_SRC_FILES :=  Astart.cpp		\
                    lua.cpp			\
					DLinkList.cpp	\
					memorypool.c

LOCAL_SHARED_LIBRARIES := lua

LOCAL_EXPORT_C_INCLUDES += $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -fPIC -DLUA_USE_DLOPEN -DLUA_USE_POSIX

include $(BUILD_SHARED_LIBRARY)
