################################################################################
#
# whisper-model  (data-only: ggml-small.bin)
#
################################################################################

WHISPER_MODEL_VERSION = 1
WHISPER_MODEL_SITE = https://huggingface.co/ggerganov/whisper.cpp/resolve/main
WHISPER_MODEL_SOURCE = ggml-small.bin
WHISPER_MODEL_LICENSE = MIT

# raw model file, not an archive
define WHISPER_MODEL_EXTRACT_CMDS
	mkdir -p $(@D)
endef

define WHISPER_MODEL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(WHISPER_MODEL_DL_DIR)/ggml-small.bin \
		$(TARGET_DIR)/usr/share/whisper-models/ggml-small.bin
endef

$(eval $(generic-package))
