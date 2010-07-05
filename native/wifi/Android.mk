
LOCAL_PATH := $(call my-dir)

# libhardware_legacy stub
include $(CLEAR_VARS)

LOCAL_MODULE    := libhardware_legacy
LOCAL_SRC_FILES := hardware_legacy_stub.c

include $(BUILD_SHARED_LIBRARY)

# the main binary

include $(CLEAR_VARS)

LOCAL_MODULE := wifi

LOCAL_SRC_FILES := main.cc
LOCAL_CPP_EXTENSION := .cc
LOCAL_CFLAGS := -Wall -Wextra -Werror -O3
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include

LOCAL_SHARED_LIBRARIES := libhardware_legacy 
LOCAL_LDLIBS := -llog

include $(BUILD_EXECUTABLE)
