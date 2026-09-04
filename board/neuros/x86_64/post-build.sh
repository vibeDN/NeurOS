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

# Claude Code auth (DEV ONLY): bake the build host's OAuth credentials so the
# agent works out of the box. Secret - never committed; sourced straight from
# $HOME. Skip cleanly if absent (CI / another machine).
if [ -x "$TARGET_DIR/opt/claude-code/claude" ] && [ -f "$HOME/.claude/.credentials.json" ]; then
	install -d -m 0700 "$TARGET_DIR/root/.claude"
	install -m 0600 "$HOME/.claude/.credentials.json" "$TARGET_DIR/root/.claude/.credentials.json"
	# minimal state so the CLI skips first-run onboarding
	cat > "$TARGET_DIR/root/.claude.json" <<-'EOF'
	{
	  "hasCompletedOnboarding": true,
	  "autoUpdates": false,
	  "autoUpdatesProtectedForNative": true,
	  "theme": "dark",
	  "projects": {
	    "/root": {
	      "hasTrustDialogAccepted": true,
	      "hasClaudeMdExternalIncludesApproved": true,
	      "hasClaudeMdExternalIncludesWarningShown": true,
	      "allowedTools": []
	    }
	  }
	}
	EOF
	chmod 0600 "$TARGET_DIR/root/.claude.json"
	# autonomous voice agent: minimise prompts. bypassPermissions / --dangerously-
	# skip-permissions are refused as root, so use acceptEdits until the agent
	# runs as a dedicated non-root user (TODO).
	cat > "$TARGET_DIR/root/.claude/settings.json" <<-'EOF'
	{
	  "permissions": { "defaultMode": "acceptEdits" },
	  "includeCoAuthoredBy": false
	}
	EOF
	chmod 0600 "$TARGET_DIR/root/.claude/settings.json"
fi
