LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := main.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_MODULE := game
LOCAL_LDLIBS += -landroid -llog -lEGL -lGLESv2 -lOpenSLES
LOCAL_LDFLAGS := -u ANativeActivity_onCreate
LOCAL_STATIC_LIBRARIES := android_native_app_glue

jnidir := $(LOCAL_PATH)/../../gamelib/src/jni
rgudir := $(jnidir)/rgu
LOCAL_C_INCLUDES += $(rgudir)/include
include $(rgudir)/sources.mk
LOCAL_SRC_FILES += $(rgusrc)

LOCAL_C_INCLUDES += $(jnidir)/game
include $(jnidir)/game/sources.mk
LOCAL_SRC_FILES += $(gamesrc)

ifeq ($(NDK_DEBUG),0)
LOCAL_CFLAGS += "-DRELEASE"
endif

include $(BUILD_SHARED_LIBRARY)
$(call import-module,android/native_app_glue)
