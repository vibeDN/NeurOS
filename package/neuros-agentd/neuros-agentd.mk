################################################################################
#
# neuros-agentd  (prototype - POSIX shell)
#
################################################################################

NEUROS_AGENTD_VERSION = 0.1.0
NEUROS_AGENTD_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-agentd/files
NEUROS_AGENTD_SITE_METHOD = local
NEUROS_AGENTD_LICENSE = MIT

define NEUROS_AGENTD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/neuros-agentd $(TARGET_DIR)/usr/lib/neuros/neuros-agentd
	$(INSTALL) -D -m 0755 $(@D)/tts-filter $(TARGET_DIR)/usr/lib/neuros/tts-filter
	$(INSTALL) -D -m 0755 $(@D)/agent-status $(TARGET_DIR)/usr/lib/neuros/agent-status
	$(INSTALL) -D -m 0755 $(@D)/mock-agent $(TARGET_DIR)/usr/lib/neuros/mock-agent
	$(INSTALL) -D -m 0755 $(@D)/neuros-stt $(TARGET_DIR)/usr/bin/neuros-stt
	$(INSTALL) -D -m 0755 $(@D)/neuros-listen $(TARGET_DIR)/usr/lib/neuros/neuros-listen
	$(INSTALL) -D -m 0755 $(@D)/neuros-mic $(TARGET_DIR)/usr/bin/neuros-mic
	$(INSTALL) -D -m 0755 $(@D)/neuros-clock $(TARGET_DIR)/usr/lib/neuros/neuros-clock
	$(INSTALL) -D -m 0755 $(@D)/neuros-lock $(TARGET_DIR)/usr/bin/neuros-lock
	$(INSTALL) -D -m 0755 $(@D)/neuros-camera $(TARGET_DIR)/usr/bin/neuros-camera
	$(INSTALL) -D -m 0755 $(@D)/neuros-agent-hook $(TARGET_DIR)/usr/lib/neuros/neuros-agent-hook
	$(INSTALL) -D -m 0644 $(@D)/agentd.conf $(TARGET_DIR)/etc/neuros/agentd.conf
	$(INSTALL) -D -m 0755 $(@D)/neurofetch $(TARGET_DIR)/usr/bin/neurofetch
	$(INSTALL) -D -m 0644 $(@D)/fastfetch.jsonc $(TARGET_DIR)/etc/fastfetch/config.jsonc
	$(INSTALL) -D -m 0644 $(@D)/logos/robot.txt $(TARGET_DIR)/usr/share/neuros/logos/robot.txt
endef

$(eval $(generic-package))
