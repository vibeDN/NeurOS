################################################################################
#
# claude-code
#
# Ships Anthropic's self-contained Claude Code native binary as the v1 agent
# CLI. No upstream tarball - the binary is copied from the host's installed
# Claude Code (the `local` site just syncs this package dir, which is tiny).
#
################################################################################

# Host installs auto-update, so this is just a label - the binary that actually
# ships is whatever CLAUDE_CODE_BIN resolves to below.
CLAUDE_CODE_VERSION = host
CLAUDE_CODE_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/claude-code
CLAUDE_CODE_SITE_METHOD = local
CLAUDE_CODE_LICENSE = Commercial (Anthropic)
CLAUDE_CODE_REDISTRIBUTE = NO

# The native binary on the build host. Default: the newest version under the
# standard install dir (`~/.local/share/claude/versions/<ver>`, plain files).
# Override on the CLI if your install lives elsewhere:
#   make CLAUDE_CODE_BIN=/opt/claude/claude
CLAUDE_CODE_BIN ?= $(shell ls -1t $(HOME)/.local/share/claude/versions/* 2>/dev/null | head -n1)

define CLAUDE_CODE_BUILD_CMDS
	test -n "$(CLAUDE_CODE_BIN)" && test -x "$(CLAUDE_CODE_BIN)" || { \
		echo "claude-code: no host binary found"; \
		echo "  looked in ~/.local/share/claude/versions/ - install Claude Code"; \
		echo "  on the host, or pass CLAUDE_CODE_BIN=/path"; \
		exit 1; }
	@echo "claude-code: shipping $(CLAUDE_CODE_BIN)"
endef

define CLAUDE_CODE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(CLAUDE_CODE_BIN) $(TARGET_DIR)/opt/claude-code/claude
	mkdir -p $(TARGET_DIR)/usr/bin
	ln -sf /opt/claude-code/claude $(TARGET_DIR)/usr/bin/claude
endef

$(eval $(generic-package))
