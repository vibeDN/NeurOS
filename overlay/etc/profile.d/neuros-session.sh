# Launch the NeurOS compositor when root logs in on tty1 (M2 bring-up).
if [ -z "$WAYLAND_DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
	exec /usr/lib/neuros/neuros-session
fi
