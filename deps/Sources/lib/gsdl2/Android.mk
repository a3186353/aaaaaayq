# Save the local path
LUA_LOCAL_PATH := $(call my-dir)

# Restore local path
LOCAL_PATH := $(LUA_LOCAL_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := gsdl2

DEPENDENT_LIB_PATH := $(LOCAL_PATH)/../../../dependent
GGE_LIB_PATH := $(LOCAL_PATH)/../../../source

LUA_PATH := $(GGE_LIB_PATH)/lua
SDL_PATH := $(DEPENDENT_LIB_PATH)/SDL
IMAGE_PATH := $(BASE_LIB_PATH)/SDL_image
MIXER_PATH := $(BASE_LIB_PATH)/SDL_mixer
TTF_PATH := $(BASE_LIB_PATH)/SDL_ttf

LOCAL_C_INCLUDES := $(SDL_PATH)/include
LOCAL_C_INCLUDES += $(SDL_PATH)/src
LOCAL_C_INCLUDES += $(IMAGE_PATH)
LOCAL_C_INCLUDES += $(MIXER_PATH)
LOCAL_C_INCLUDES += $(TTF_PATH)
LOCAL_C_INCLUDES += $(LUA_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)

LOCAL_SRC_FILES :=  audio.c			\
                    blendmode.c		\
                    clipboard.c		\
                    cpuinfo.c		\
                    error.c			\
                    events.c		\
                    filesystem.c	\
                    gamecontroller.c	\
                    gesture.c		\
                    gge.c			\
                    haptic.c		\
                    hints.c			\
                    joystick.c		\
                    keyboard.c		\
                    locale.c		\
                    log.c			\
                    messagebox.c	\
                    metal.c			\
                    misc.c			\
                    mouse.c			\
                    mutex.c			\
                    opengl.c		\
                    pixels.c		\
                    platform.c		\
                    power.c			\
                    rect.c			\
                    renderer.c		\
                    rwops.c			\
                    sdl_image.c		\
                    sdl_mixer.c		\
                    sdl_net.c		\
                    sdl_ttf.c		\
                    sdl.c			\
                    sensor.c		\
                    shape.c			\
                    stdinc.c		\
                    surface.c		\
                    system.c		\
                    syswm.c			\
                    thread.c		\
                    timer.c			\
                    touch.c			\
                    version.c		\
                    video.c

LOCAL_SHARED_LIBRARIES := lua SDL2 SDL2_image SDL2_ttf SDL2_mixer zlib

LOCAL_EXPORT_C_INCLUDES += $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -fPIC -DLUA_USE_DLOPEN -DLUA_USE_POSIX

include $(BUILD_SHARED_LIBRARY)
