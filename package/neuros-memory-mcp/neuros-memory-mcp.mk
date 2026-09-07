################################################################################
#
# neuros-memory-mcp
#
# A tiny MCP server (streamable HTTP, stdlib-only Go) that exposes the agent
# memory directory as read/write tools, so Claude Code on the device and Claude
# on claude.ai (via a tunnelled custom connector) share one memory.
#
################################################################################

NEUROS_MEMORY_MCP_VERSION = 0.1.0
NEUROS_MEMORY_MCP_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-memory-mcp/src
NEUROS_MEMORY_MCP_SITE_METHOD = local
NEUROS_MEMORY_MCP_LICENSE = MIT

# stdlib only - no module downloads
NEUROS_MEMORY_MCP_BUILD_TARGETS = .

define NEUROS_MEMORY_MCP_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 0644 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-memory-mcp.service \
		$(TARGET_DIR)/usr/lib/systemd/system/neuros-memory-mcp.service
	$(INSTALL) -D -m 0644 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-memory-tunnel.service \
		$(TARGET_DIR)/usr/lib/systemd/system/neuros-memory-tunnel.service
endef

define NEUROS_MEMORY_MCP_INSTALL_TARGET_CMDS_EXTRA
	$(INSTALL) -D -m 0755 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-mcp-url \
		$(TARGET_DIR)/usr/bin/neuros-mcp-url
endef
NEUROS_MEMORY_MCP_POST_INSTALL_TARGET_HOOKS += NEUROS_MEMORY_MCP_INSTALL_TARGET_CMDS_EXTRA

$(eval $(golang-package))
