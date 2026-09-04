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
	$(INSTALL) -D -m 0644 $(@D)/agentd.conf $(TARGET_DIR)/etc/neuros/agentd.conf
endef

$(eval $(generic-package))
