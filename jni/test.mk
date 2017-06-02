LOCAL_PATH := $(call my-dir)
MY_WEBRTC_SRC_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/android-webrtc.mk
LOCAL_ARM_MODE := arm
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := libwebrtc-voice-demo-jni
LOCAL_CPP_EXTENSION := .cc
LOCAL_SRC_FILES :=MyAudio.cc
LOCAL_CFLAGS := \
    '-DWEBRTC_TARGET_PC' \
    '-DWEBRTC_ANDROID'
	
LOCAL_WHOLE_STATIC_LIBRARIES := \
    libwebrtc_system_wrappers \
    libwebrtc_audio_device \
    libwebrtc_pcm16b \
    libwebrtc_cng \
    libwebrtc_audio_coding \
    libwebrtc_rtp_rtcp \
    libwebrtc_media_file \
    libwebrtc_udp_transport \
    libwebrtc_utility \
    libwebrtc_neteq \
    libwebrtc_audio_conference_mixer \
    libwebrtc_isac \
    libwebrtc_ilbc \
    libwebrtc_isacfix \
    libwebrtc_g722 \
    libwebrtc_g711 \
    libwebrtc_voe_core \
	libwebrtc_spl \
    libwebrtc_resampler \
    libwebrtc_apm \
    libwebrtc_apm_utility \
    libwebrtc_vad \
    libwebrtc_ns \
    libwebrtc_agc \
    libwebrtc_aec \
    libwebrtc_aecm \
    libwebrtc_system_wrappers \
	
LOCAL_C_INCLUDES := \
    $(MY_WEBRTC_SRC_PATH)/sources/voice_engine/test/auto_test \
    $(MY_WEBRTC_SRC_PATH)/sources/voice_engine/test \
    $(MY_WEBRTC_SRC_PATH) \
	$(MY_WEBRTC_SRC_PATH)/sources/voice_engine/include \
    $(MY_WEBRTC_SRC_PATH)/sources/system_wrappers/interface \
    $(MY_WEBRTC_SRC_PATH)/sources/modules/interface

LOCAL_PRELINK_MODULE := false

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libstlport \
    libandroid \
    libwebrtc \
    libGLESv2
	
LOCAL_LDLIBS := \
    -lgcc \
    -llog \
    -lOpenSLES


include $(BUILD_SHARED_LIBRARY)