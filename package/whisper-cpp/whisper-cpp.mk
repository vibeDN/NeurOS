################################################################################
#
# whisper-cpp
#
################################################################################

WHISPER_CPP_VERSION = 1.9.3
WHISPER_CPP_SITE = $(call github,ggml-org,whisper.cpp,v$(WHISPER_CPP_VERSION))
WHISPER_CPP_LICENSE = MIT
WHISPER_CPP_LICENSE_FILES = LICENSE
WHISPER_CPP_INSTALL_STAGING = NO

WHISPER_CPP_CONF_OPTS = \
	-DWHISPER_BUILD_TESTS=OFF \
	-DWHISPER_BUILD_SERVER=OFF \
	-DWHISPER_BUILD_EXAMPLES=ON \
	-DGGML_NATIVE=OFF \
	-DGGML_OPENMP=OFF \
	-DBUILD_SHARED_LIBS=ON

ifeq ($(BR2_PACKAGE_OPENBLAS),y)
WHISPER_CPP_CONF_OPTS += -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS
WHISPER_CPP_DEPENDENCIES += openblas
else
WHISPER_CPP_CONF_OPTS += -DGGML_BLAS=OFF
endif

# x86_64 dev target: assume x86-64-v3 (AVX2/FMA/F16C/BMI2, any Haswell+/Zen+).
# The phone target sets ARM flags at M5.
ifeq ($(BR2_x86_64),y)
WHISPER_CPP_CONF_OPTS += -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON \
	-DGGML_F16C=ON -DGGML_BMI2=ON
endif

# CMake builds in-source; binaries + libs land in $(@D)/bin
define WHISPER_CPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/bin/whisper-cli $(TARGET_DIR)/usr/bin/whisper-cli
	$(INSTALL) -D -m 0755 $(@D)/bin/whisper-vad-speech-segments \
		$(TARGET_DIR)/usr/bin/whisper-vad-speech-segments
	cp -a $(@D)/bin/libwhisper.so* $(@D)/bin/libggml*.so* $(TARGET_DIR)/usr/lib/
endef

$(eval $(cmake-package))
