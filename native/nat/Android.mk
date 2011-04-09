
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := nat

LOCAL_SRC_FILES := main.cc barnacle.cc
LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS := -Wall -Wextra -Werror -O3
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
LOCAL_LDLIBS := -llog

include $(BUILD_EXECUTABLE)

LOCAL_MODULE := local

LOCAL_SRC_FILES := local.cc
LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS := -Wall -Wextra -Werror -O3
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include

LOCAL_LDLIBS := -llog

include $(BUILD_EXECUTABLE)