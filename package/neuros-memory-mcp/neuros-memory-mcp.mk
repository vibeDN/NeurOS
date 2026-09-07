################################################################################
#
# neuros-memory-mcp
#
# On-device MCP server (streamable HTTP, stdlib-only Go) for the shared agent
# memory. Runs as a read-through / write-behind cache in front of the canonical
# neuros-memory Cloudflare Worker (MEMORY_MCP_URL) so the device works offline.
#
################################################################################

NEUROS_MEMORY_MCP_VERSION = 0.2.0
NEUROS_MEMORY_MCP_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-memory-mcp/src
NEUROS_MEMORY_MCP_SITE_METHOD = local
NEUROS_MEMORY_MCP_LICENSE = MIT

# stdlib only. GOMOD must match src/go.mod (buildroot can't infer it from a
# local SITE path).
NEUROS_MEMORY_MCP_GOMOD = neuros-memory-mcp
NEUROS_MEMORY_MCP_BUILD_TARGETS = .

define NEUROS_MEMORY_MCP_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 0644 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-memory-mcp.service \
		$(TARGET_DIR)/usr/lib/systemd/system/neuros-memory-mcp.service
endef

define NEUROS_MEMORY_MCP_INSTALL_EXTRA
	$(INSTALL) -D -m 0755 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-memory-mcp-run \
		$(TARGET_DIR)/usr/lib/neuros/neuros-memory-mcp-run
	$(INSTALL) -D -m 0755 $(NEUROS_MEMORY_MCP_PKGDIR)/neuros-mcp-url \
		$(TARGET_DIR)/usr/bin/neuros-mcp-url
	test -f $(TARGET_DIR)/etc/neuros/memory.conf || \
		$(INSTALL) -D -m 0644 $(NEUROS_MEMORY_MCP_PKGDIR)/memory.conf \
			$(TARGET_DIR)/etc/neuros/memory.conf
endef
NEUROS_MEMORY_MCP_POST_INSTALL_TARGET_HOOKS += NEUROS_MEMORY_MCP_INSTALL_EXTRA

$(eval $(golang-package))
