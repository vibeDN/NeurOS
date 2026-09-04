################################################################################
#
# claude-code
#
# Ships Anthropic's self-contained Claude Code native binary as the v1 agent
# CLI. No upstream tarball - the binary is copied from the host's installed
# Claude Code (the `local` site just syncs this package dir, which is tiny).
#
################################################################################

CLAUDE_CODE_VERSION = 2.1.260
CLAUDE_CODE_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/claude-code
CLAUDE_CODE_SITE_METHOD = local
CLAUDE_CODE_LICENSE = Commercial (Anthropic)
CLAUDE_CODE_REDISTRIBUTE = NO

# Where to find the native binary on the build host. Override on the CLI if your
# install lives elsewhere:  make CLAUDE_CODE_BIN=/opt/claude/claude
CLAUDE_CODE_BIN ?= $(HOME)/.local/share/claude/versions/$(CLAUDE_CODE_VERSION)

define CLAUDE_CODE_BUILD_CMDS
	test -x "$(CLAUDE_CODE_BIN)" || { \
		echo "claude-code: no binary at $(CLAUDE_CODE_BIN)"; \
		echo "  install Claude Code on the host, or pass CLAUDE_CODE_BIN=/path"; \
		exit 1; }
endef

define CLAUDE_CODE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(CLAUDE_CODE_BIN) $(TARGET_DIR)/opt/claude-code/claude
	mkdir -p $(TARGET_DIR)/usr/bin
	ln -sf /opt/claude-code/claude $(TARGET_DIR)/usr/bin/claude
endef

$(eval $(generic-package))
