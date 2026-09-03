################################################################################
#
# piper-voices  (data-only: ryan + ruslan, medium)
#
################################################################################

PIPER_VOICES_VERSION = 2023.11.14
PIPER_VOICES_SITE = https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium
PIPER_VOICES_SOURCE = en_US-ryan-medium.onnx.json
PIPER_VOICES_LICENSE = MIT, CC-BY-4.0 (voice data)

PIPER_VOICES_HF = https://huggingface.co/rhasspy/piper-voices/resolve/main
PIPER_VOICES_EXTRA_DOWNLOADS = \
	$(PIPER_VOICES_HF)/en/en_US/ryan/medium/en_US-ryan-medium.onnx \
	$(PIPER_VOICES_HF)/ru/ru_RU/ruslan/medium/ru_RU-ruslan-medium.onnx \
	$(PIPER_VOICES_HF)/ru/ru_RU/ruslan/medium/ru_RU-ruslan-medium.onnx.json

# sources are raw data files, not archives - skip extraction
define PIPER_VOICES_EXTRACT_CMDS
	mkdir -p $(@D)
endef

define PIPER_VOICES_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/piper-voices
	$(INSTALL) -D -m 0644 $(PIPER_VOICES_DL_DIR)/en_US-ryan-medium.onnx \
		$(TARGET_DIR)/usr/share/piper-voices/en_US-ryan-medium.onnx
	$(INSTALL) -D -m 0644 $(PIPER_VOICES_DL_DIR)/en_US-ryan-medium.onnx.json \
		$(TARGET_DIR)/usr/share/piper-voices/en_US-ryan-medium.onnx.json
	$(INSTALL) -D -m 0644 $(PIPER_VOICES_DL_DIR)/ru_RU-ruslan-medium.onnx \
		$(TARGET_DIR)/usr/share/piper-voices/ru_RU-ruslan-medium.onnx
	$(INSTALL) -D -m 0644 $(PIPER_VOICES_DL_DIR)/ru_RU-ruslan-medium.onnx.json \
		$(TARGET_DIR)/usr/share/piper-voices/ru_RU-ruslan-medium.onnx.json
	ln -sf en_US-ryan-medium.onnx $(TARGET_DIR)/usr/share/piper-voices/default.onnx
	ln -sf en_US-ryan-medium.onnx.json $(TARGET_DIR)/usr/share/piper-voices/default.onnx.json
endef

$(eval $(generic-package))
