LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= su.c

LOCAL_MODULE:= suw

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_STATIC_LIBRARIES := libc libcutils

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := $(TARGET_BUILD_VARIANT)

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS) 


LOCAL_SRC_FILES += allowsu.wmt!sh:system/xbin/allowsu.wmt!sh
LOCAL_SRC_FILES += prohibitsu.wmt!sh:system/xbin/prohibitsu.wmt!sh

include $(WMT_PREBUILT)
