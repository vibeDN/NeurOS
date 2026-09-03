################################################################################
#
# piper  (prebuilt upstream release - bundles onnxruntime + espeak-ng)
#
################################################################################

PIPER_VERSION = 2023.11.14-2
PIPER_SITE = https://github.com/rhasspy/piper/releases/download/$(PIPER_VERSION)
PIPER_LICENSE = MIT
PIPER_LICENSE_FILES =

ifeq ($(BR2_x86_64),y)
PIPER_SOURCE = piper_linux_x86_64.tar.gz
else ifeq ($(BR2_aarch64),y)
PIPER_SOURCE = piper_linux_aarch64.tar.gz
endif

# the tarball has a top-level piper/ dir
PIPER_STRIP_COMPONENTS = 1

define PIPER_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/piper
	cp -a $(@D)/piper $(@D)/piper_phonemize $(@D)/espeak-ng \
		$(@D)/libpiper_phonemize.so* $(@D)/libespeak-ng.so* \
		$(@D)/libonnxruntime.so* $(@D)/libtashkeel_model.ort \
		$(TARGET_DIR)/opt/piper/
	cp -a $(@D)/espeak-ng-data $(TARGET_DIR)/opt/piper/
	$(INSTALL) -D -m 0755 $(PIPER_PKGDIR)/files/piper.sh $(TARGET_DIR)/usr/bin/piper
endef

$(eval $(generic-package))
