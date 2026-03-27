# Android NDK build file for ghv (libhv Lua bindings)

LOCAL_PATH := $(call my-dir)
SOURCES_PATH := $(LOCAL_PATH)/../../../../Sources/lib/ghv
DEPS_PATH := $(LOCAL_PATH)/../../../../Dependencies

include $(CLEAR_VARS)
LOCAL_MODULE := ghv
LOCAL_C_INCLUDES := \
    $(DEPS_PATH)/libhv \
    $(DEPS_PATH)/libhv/base \
    $(DEPS_PATH)/libhv/event \
    $(DEPS_PATH)/libhv/evpp \
    $(DEPS_PATH)/libhv/http \
    $(DEPS_PATH)/libhv/http/client \
    $(LOCAL_PATH)/../../../dep/lua/include

LOCAL_SRC_FILES := \
    $(SOURCES_PATH)/ghv_tcp_client.cpp \
    $(SOURCES_PATH)/ghv_tcp_server.cpp \
    $(SOURCES_PATH)/ghv_crypto.cpp \
    $(SOURCES_PATH)/ghv_http.cpp \
    $(SOURCES_PATH)/ghv_download.cpp \
    $(SOURCES_PATH)/ghv_loop_init.c

LOCAL_CPPFLAGS := -std=c++17 -fexceptions
LOCAL_SHARED_LIBRARIES := lua54
LOCAL_STATIC_LIBRARIES := libhv_static

include $(BUILD_SHARED_LIBRARY)
