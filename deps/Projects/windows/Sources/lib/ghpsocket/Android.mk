# Save the local path
LUA_LOCAL_PATH := $(call my-dir)

# Restore local path
LOCAL_PATH := $(LUA_LOCAL_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := ghpsocket

DEPENDENT_LIB_PATH := $(LOCAL_PATH)/../../../dependent
GGE_LIB_PATH := $(LOCAL_PATH)/../../../source

LUA_PATH := $(GGE_LIB_PATH)/lua
SDL_PATH := $(DEPENDENT_LIB_PATH)/SDL
HPSOCKET_PATH := $(DEPENDENT_LIB_PATH)/HP-Socket/Linux/include/hpsocket

LOCAL_C_INCLUDES := $(SDL_PATH)/include
LOCAL_C_INCLUDES += $(LUA_PATH)
LOCAL_C_INCLUDES += $(HPSOCKET_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)

LOCAL_SRC_FILES :=  common.cpp			\
					HP_HttpClient.cpp	\
					HP_PackAgent.cpp	\
					HP_PackClient.cpp	\
					HP_PackServer.cpp	\
					HP_PushClient.cpp	\
					HP_UdpNode.cpp		\
					HP_Buffer.cpp		\
					HP_PullClient.cpp	\
					HP_PullAgent.cpp	

LOCAL_SHARED_LIBRARIES := lua SDL2 zlib

LOCAL_STATIC_LIBRARIES := hpsocket_a

LOCAL_EXPORT_C_INCLUDES += $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -fPIC -DLUA_USE_DLOPEN -DLUA_USE_POSIX

LOCAL_CXXFLAGS := -DHAVE_PTHREADS

include $(BUILD_SHARED_LIBRARY)
