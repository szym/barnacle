
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dhcp

LOCAL_SRC_FILES := main.cc dhcp.cc
LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS := -Wall -Wextra -Werror -O3
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
LOCAL_LDLIBS := -llog

include $(BUILD_EXECUTABLE)
