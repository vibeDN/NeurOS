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

# only the CLI + libs; drop the other examples
define WHISPER_CPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/bin/whisper-cli $(TARGET_DIR)/usr/bin/whisper-cli
	cp -a $(@D)/build/src/libwhisper.so* $(TARGET_DIR)/usr/lib/
	cp -a $(@D)/build/ggml/src/libggml*.so* $(TARGET_DIR)/usr/lib/
endef

$(eval $(cmake-package))
