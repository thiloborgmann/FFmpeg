



fate-cuvid-h264: CMD = framecrc -vcodec h264_cuvid -i $(TARGET_SAMPLES)/cuvid/h264_cuvid.h264
fate-cuvid-hevc: CMD = framecrc -vcodec hevc_cuvid -i $(TARGET_SAMPLES)/cuvid/hevc_cuvid.mov


FATE_CUVID-$(call DEMDEC, H264, H264_CUVID) += fate-cuvid-h264
FATE_CUVID-$(call DEMDEC,  MOV, HEVC_CUVID) += fate-cuvid-hevc

FATE_SAMPLES_AVCONV += $(FATE_CUVID-yes)
fate-cuvid: $(FATE_CUVID-yes)


