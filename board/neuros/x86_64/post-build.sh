#!/bin/sh
# Runs after the target rootfs is assembled, before image creation.
set -e

BOARD_DIR="$(dirname "$0")"

# Install the GRUB menu and stage the BIOS 1st-stage image for genimage.
install -D -m 0644 "$BOARD_DIR/grub.cfg" "$TARGET_DIR/boot/grub/grub.cfg"
cp -f "$TARGET_DIR/lib/grub/i386-pc/boot.img" "$BINARIES_DIR/boot.img"

# openssh's sample sshd_config has no Include line, so /etc/ssh/sshd_config.d/*
# is ignored. Add one (drop-in carries our dev PermitRootLogin/PasswordAuth).
SSHD="$TARGET_DIR/etc/ssh/sshd_config"
if [ -f "$SSHD" ] && ! grep -q '^Include /etc/ssh/sshd_config.d/' "$SSHD"; then
	sed -i '1i Include /etc/ssh/sshd_config.d/*.conf' "$SSHD"
fi

# NeurOS owns tty1 (compositor) - the text-console font/keymap setup only fails
mkdir -p "$TARGET_DIR/etc/systemd/system"
ln -sf /dev/null "$TARGET_DIR/etc/systemd/system/systemd-vconsole-setup.service"

# fish as root's interactive login shell (serial console + ssh). System scripts
# and the agent CLI keep their explicit /bin/sh shebangs.
if [ -x "$TARGET_DIR/usr/bin/fish" ]; then
	grep -q '^/usr/bin/fish$' "$TARGET_DIR/etc/shells" 2>/dev/null || \
		echo /usr/bin/fish >> "$TARGET_DIR/etc/shells"
	sed -i 's|^\(root:[^:]*:[^:]*:[^:]*:[^:]*:[^:]*:\).*|\1/usr/bin/fish|' \
		"$TARGET_DIR/etc/passwd"
fi

# --- agent user (claude) home + Claude Code provisioning --------------------
# The agent CLI runs as the unprivileged `claude` user (uid 1000, users.table).
# mkusers runs AFTER this script and chown -R's /home/claude to 1000:1000, so we
# just lay the files down here.
AGENT_HOME="$TARGET_DIR/home/claude"
NEUROS_DIR="${BR2_EXTERNAL_NEUROS_PATH:-$BOARD_DIR/../../..}"

# mandatory per-agent memory prompt -> ~/.claude/CLAUDE.md (user-global memory);
# <agent> is substituted for this user.
install -d -m 0755 "$AGENT_HOME/.claude"
install -d -m 0755 "$AGENT_HOME/memory/you" "$AGENT_HOME/memory/topics" "$AGENT_HOME/memory/area"
if [ -f "$NEUROS_DIR/docs/agent-memory-prompt.md" ]; then
	sed -n '/^## Memory$/,/^## Implementation notes/p' "$NEUROS_DIR/docs/agent-memory-prompt.md" \
		| sed '/^## Implementation notes/d; s#/home/<agent>/#/home/claude/#g' \
		> "$AGENT_HOME/.claude/CLAUDE.md"
fi

# Claude Code auth (DEV ONLY): bake the build host's OAuth credentials so the
# agent works out of the box. Secret - never committed; sourced straight from
# $HOME. Skip cleanly if absent (CI / another machine).
if [ -x "$TARGET_DIR/opt/claude-code/claude" ] && [ -f "$HOME/.claude/.credentials.json" ]; then
	for h in "$AGENT_HOME" "$TARGET_DIR/root"; do
		if [ "$h" = "$TARGET_DIR/root" ]; then dir=/root; bypass=false; else dir=/home/claude; bypass=true; fi
		install -d -m 0700 "$h/.claude"
		install -m 0600 "$HOME/.claude/.credentials.json" "$h/.claude/.credentials.json"
		cat > "$h/.claude.json" <<-EOF
		{
		  "hasCompletedOnboarding": true,
		  "autoUpdates": false,
		  "autoUpdatesProtectedForNative": true,
		  "bypassPermissionsModeAccepted": $bypass,
		  "theme": "dark",
		  "projects": {
		    "$dir": {
		      "hasTrustDialogAccepted": true,
		      "hasClaudeMdExternalIncludesApproved": true,
		      "hasClaudeMdExternalIncludesWarningShown": true,
		      "allowedTools": []
		    }
		  }
		}
		EOF
		chmod 0600 "$h/.claude.json"
	done
	# agent (non-root): bypassPermissions is fine -> fully autonomous voice flow.
	# Stop hook feeds the reply to NeurOS TTS (neuros-agent-hook -> reply.txt).
	cat > "$AGENT_HOME/.claude/settings.json" <<-'EOF'
	{
	  "permissions": { "defaultMode": "bypassPermissions" },
	  "includeCoAuthoredBy": false,
	  "hooks": {
	    "Stop": [
	      { "hooks": [ { "type": "command", "command": "/usr/lib/neuros/neuros-agent-hook" } ] }
	    ]
	  }
	}
	EOF
	# root (for manual `claude` over ssh): bypass is refused as root -> acceptEdits
	cat > "$TARGET_DIR/root/.claude/settings.json" <<-'EOF'
	{
	  "permissions": { "defaultMode": "acceptEdits" },
	  "includeCoAuthoredBy": false
	}
	EOF
	chmod 0600 "$AGENT_HOME/.claude/settings.json" "$TARGET_DIR/root/.claude/settings.json"
fi
