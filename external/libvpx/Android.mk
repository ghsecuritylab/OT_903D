LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES = \
	vpx/src/vpx_codec.c \
	vpx/src/vpx_decoder.c \
	vpx/src/vpx_image.c \
	vpx_mem/vpx_mem.c \
	vpx_scale/generic/vpxscale.c \
	vpx_scale/generic/yv12config.c \
	vpx_scale/generic/yv12extend.c \
	vpx_scale/generic/scalesystemdependant.c \
	vpx_scale/generic/gen_scalers.c \
	vp8/common/alloccommon.c \
    vp8/common/arm/arm_systemdependent.c \
    vp8/common/arm/reconintra_arm.c \
	vp8/common/blockd.c \
	vp8/common/debugmodes.c \
	vp8/common/entropy.c \
	vp8/common/entropymode.c \
	vp8/common/entropymv.c \
	vp8/common/extend.c \
	vp8/common/filter_c.c \
	vp8/common/findnearmv.c \
	vp8/common/generic/systemdependent.c \
	vp8/common/idctllm.c \
	vp8/common/invtrans.c \
	vp8/common/loopfilter.c \
	vp8/common/loopfilter_filters.c \
	vp8/common/mbpitch.c \
	vp8/common/modecont.c \
	vp8/common/modecontext.c \
	vp8/common/predictdc.c \
	vp8/common/quant_common.c \
	vp8/common/recon.c \
	vp8/common/reconinter.c \
	vp8/common/reconintra.c \
	vp8/common/reconintra4x4.c \
	vp8/common/setupintrarecon.c \
	vp8/common/swapyv12buffer.c \
	vp8/common/textblit.c \
	vp8/common/treecoder.c \
	vp8/common/postproc.c \
	vp8/vp8_cx_iface.c \
	vp8/vp8_dx_iface.c \
    vp8/decoder/arm/arm_dsystemdependent.c \
	vp8/decoder/dboolhuff.c \
	vp8/decoder/decodemv.c \
	vp8/decoder/decodframe.c \
	vp8/decoder/dequantize.c \
	vp8/decoder/detokenize.c \
	vp8/decoder/generic/dsystemdependent.c \
	vp8/decoder/onyxd_if.c \
	vp8/decoder/reconintra_mt.c \
	vp8/decoder/threading.c \
	vpx_config.c \
	tools_common.c \

LOCAL_CFLAGS := \
    -DHAVE_CONFIG_H=vpx_config.h

LOCAL_MODULE := libvpx

ifeq ($(ARCH_ARM_HAVE_NEON),true)

LOCAL_SRC_FILES += \
    vp8/common/arm/loopfilter_arm.c \
    vp8/decoder/arm/dequantize_arm.c \
    vp8/decoder/arm/neon/idct_blk_neon.c \

LOCAL_CFLAGS += -D__ARM_HAVE_NEON

LOCAL_MODULE_CLASS := STATIC_LIBRARIES
intermediates := $(call local-intermediates-dir)

ASM_FILES = \
    vp8/common/arm/neon/bilinearpredict16x16_neon.s \
    vp8/common/arm/neon/bilinearpredict4x4_neon.s \
    vp8/common/arm/neon/bilinearpredict8x4_neon.s \
    vp8/common/arm/neon/bilinearpredict8x8_neon.s \
    vp8/common/arm/neon/buildintrapredictorsmby_neon.s \
    vp8/common/arm/neon/copymem16x16_neon.s \
    vp8/common/arm/neon/copymem8x4_neon.s \
    vp8/common/arm/neon/copymem8x8_neon.s \
    vp8/common/arm/neon/iwalsh_neon.s \
    vp8/common/arm/neon/loopfiltersimplehorizontaledge_neon.s \
    vp8/common/arm/neon/loopfiltersimpleverticaledge_neon.s \
    vp8/common/arm/neon/recon16x16mb_neon.s \
    vp8/common/arm/neon/recon2b_neon.s \
    vp8/common/arm/neon/recon4b_neon.s \
    vp8/common/arm/neon/reconb_neon.s \
    vp8/common/arm/neon/save_neon_reg.s \
    vp8/common/arm/neon/shortidct4x4llm_1_neon.s \
    vp8/common/arm/neon/shortidct4x4llm_neon.s \
    vp8/common/arm/neon/sixtappredict16x16_neon.s \
    vp8/common/arm/neon/sixtappredict4x4_neon.s \
    vp8/common/arm/neon/sixtappredict8x4_neon.s \
    vp8/common/arm/neon/sixtappredict8x8_neon.s \
    vp8/common/arm/neon/dc_only_idct_add_neon.s \
    vp8/decoder/arm/neon/dequantizeb_neon.s \
    vp8/decoder/arm/neon/dequant_idct_neon.s \
    vp8/decoder/arm/neon/idct_dequant_0_2x_neon.s \
    vp8/decoder/arm/neon/idct_dequant_dc_0_2x_neon.s \
    vp8/decoder/arm/neon/idct_dequant_dc_full_2x_neon.s \
    vp8/decoder/arm/neon/idct_dequant_full_2x_neon.s \
    vp8/common/arm/neon/loopfilter_neon.s \
    vp8/common/arm/neon/mbloopfilter_neon.s \

# All the assembly sources must be converted from ADS to GAS compatible format
VPX_GEN := $(addprefix $(intermediates)/, $(ASM_FILES))
$(VPX_GEN) : PRIVATE_PATH := $(LOCAL_PATH)
$(VPX_GEN) : PRIVATE_CUSTOM_TOOL = cat $< | perl external/libvpx/build/make/ads2gas.pl > $@
$(VPX_GEN) : $(intermediates)/%.s : $(LOCAL_PATH)/%.asm
	$(transform-generated-source)

LOCAL_GENERATED_SOURCES += $(VPX_GEN)

endif

ifeq ($(strip $(MTK_PLATFORM)),MT6573)

LOCAL_CFLAGS += -D__HAVE_ARMV6

LOCAL_CFLAGS += -march=armv6
        
LOCAL_SRC_FILES += \
	vp8/common/arm/filter_arm.c \
	vp8/common/arm/bilinearfilter_arm.c \
	vp8/common/arm/loopfilter_arm.c \
	vp8/decoder/arm/dequantize_arm.c \
	vp8/decoder/arm/armv6/idct_blk_v6.c \
	vpx_ports/arm_cpudetect.c \
        
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

intermediates := $(call local-intermediates-dir)
        
ASM_FILES = \
	vp8/common/arm/armv6/bilinearfilter_v6.s \
	vp8/common/arm/armv6/copymem8x4_v6.s \
	vp8/common/arm/armv6/copymem8x8_v6.s \
	vp8/common/arm/armv6/copymem16x16_v6.s \
	vp8/common/arm/armv6/dc_only_idct_add_v6.s \
	vp8/common/arm/armv6/filter_v6.s \
	vp8/common/arm/armv6/idct_v6.s \
	vp8/common/arm/armv6/iwalsh_v6.s \
	vp8/common/arm/armv6/loopfilter_v6.s \
	vp8/common/arm/armv6/recon_v6.s \
	vp8/common/arm/armv6/simpleloopfilter_v6.s \
	vp8/common/arm/armv6/sixtappredict8x4_v6.s \
	vp8/decoder/arm/armv6/dequantize_v6.s \
	vp8/decoder/arm/armv6/dequant_dc_idct_v6.s \
	vp8/decoder/arm/armv6/dequant_idct_v6.s

        
# All the assembly sources must be converted from ADS to GAS compatible format
VPX_GEN := $(addprefix $(intermediates)/, $(ASM_FILES))
$(VPX_GEN) : PRIVATE_PATH := $(LOCAL_PATH)
$(VPX_GEN) : PRIVATE_CUSTOM_TOOL = cat $< | perl external/libvpx/build/make/ads2gas.pl > $@
$(VPX_GEN) : $(intermediates)/%.s : $(LOCAL_PATH)/%.asm
	$(transform-generated-source)
    
LOCAL_GENERATED_SOURCES += $(VPX_GEN)

else

LOCAL_SRC_FILES += vp8/decoder/idct_blk.c

endif

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/vpx_ports \
	$(LOCAL_PATH)/vp8/common \
	$(LOCAL_PATH)/vp8/encoder \
	$(LOCAL_PATH)/vp8/decoder \
	$(LOCAL_PATH)/vp8 \
	$(LOCAL_PATH)/vpx_codec

include $(BUILD_STATIC_LIBRARY)

#
# vpxdec
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := vpxdec.c		\
                   md5_utils.c	\
                   args.c		\
                   nestegg/halloc/src/halloc.c \
                   nestegg/src/nestegg.c \
                   tools_common.c \
                   
LOCAL_C_INCLUDES := md5_utils.h \
                    args.h		\
                    vpx_ports/vpx_timer.h	\
                    vpx/vpx_integer.h		\
                    vpx_ports/config.h		\
                    nestegg/halloc/halloc.h	\
                    nestegg/halloc/src/align.h	\
                    nestegg/halloc/src/hlist.h	\
                    nestegg/halloc/src/macros.h \
                    nestegg/include/nestegg/nestegg.h \

LOCAL_SHARED_LIBRARIES := libutils

LOCAL_STATIC_LIBRARIES := libvpx

LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := vpxdec

include $(BUILD_EXECUTABLE)