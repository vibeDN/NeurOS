/*
 * NeurOS compositor control socket. See ipc.h. MIT.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "ipc.h"
#include "server.h"
#include "shell.h"

#define NG_IPC_SOCK "neuros-comp.sock"
#define NG_IPC_MAXLINE 512

struct ng_ipc {
	struct cg_server *server;
	int fd;
	char path[256];
	struct wl_event_source *source;
};

static int
parse_hex_color(const char *s, float out[4])
{
	if (*s == '#')
		s++;
	if (strlen(s) < 6)
		return -1;
	char c[7] = {s[0], s[1], s[2], s[3], s[4], s[5], 0};
	long v = strtol(c, NULL, 16);
	out[0] = ((v >> 16) & 0xff) / 255.0f;
	out[1] = ((v >> 8) & 0xff) / 255.0f;
	out[2] = (v & 0xff) / 255.0f;
	out[3] = 1.0f;
	return 0;
}

static void
handle_line(struct ng_ipc *ipc, char *line)
{
	/* trim trailing whitespace/newline */
	size_t n = strlen(line);
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
		line[--n] = '\0';
	if (n == 0)
		return;

	struct ng_shell *shell = ipc->server->shell;
	if (!shell)
		return;

	char *arg = strchr(line, ' ');
	if (arg)
		*arg++ = '\0';

	if (strcmp(line, "agent") == 0 && arg) {
		ng_shell_set_agent(shell, arg);
	} else if (strcmp(line, "model") == 0) {
		ng_shell_set_model(shell, arg ? arg : "");
	} else if (strcmp(line, "status") == 0 && arg) {
		ng_shell_set_status(shell, arg);
	} else if (strcmp(line, "strip") == 0) {
		ng_shell_set_strip(shell, arg ? arg : "");
	} else if (strcmp(line, "strip_right") == 0) {
		ng_shell_set_strip_right(shell, arg ? arg : "");
	} else if (strcmp(line, "activity") == 0) {
		ng_shell_set_activity(shell, arg ? arg : "");
	} else if (strcmp(line, "mic") == 0) {
		if (arg && strcmp(arg, "toggle") == 0)
			ng_shell_set_mic(shell, !shell->mic_on);
		else
			ng_shell_set_mic(shell, arg && strcmp(arg, "on") == 0);
	} else if (strcmp(line, "lock") == 0) {
		/* optional "HH:MM|Weekday DD Month" */
		char *bar = arg ? strchr(arg, '|') : NULL;
		if (bar)
			*bar++ = '\0';
		ng_shell_set_locked(shell, 1, (arg && arg[0]) ? arg : NULL, bar);
	} else if (strcmp(line, "unlock") == 0) {
		ng_shell_set_locked(shell, 0, NULL, NULL);
	} else if (strcmp(line, "colors") == 0 && arg) {
		char *sp = strchr(arg, ' ');
		float top[4], bot[4];
		if (sp) {
			*sp++ = '\0';
			if (parse_hex_color(arg, top) == 0 && parse_hex_color(sp, bot) == 0)
				ng_shell_set_colors(shell, top, bot);
		}
	} else {
		wlr_log(WLR_DEBUG, "ng_ipc: unknown command '%s'", line);
	}
}

static int
handle_readable(int fd, uint32_t mask, void *data)
{
	struct ng_ipc *ipc = data;
	(void) mask;

	char buf[NG_IPC_MAXLINE];
	ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
	if (r <= 0)
		return 0;
	buf[r] = '\0';

	/* one or more newline-separated commands in this datagram */
	char *save = NULL;
	for (char *ln = strtok_r(buf, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save))
		handle_line(ipc, ln);
	return 0;
}

struct ng_ipc *
ng_ipc_create(struct cg_server *server)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (!rt)
		rt = "/tmp";

	struct ng_ipc *ipc = calloc(1, sizeof(*ipc));
	if (!ipc)
		return NULL;
	ipc->server = server;
	snprintf(ipc->path, sizeof(ipc->path), "%s/" NG_IPC_SOCK, rt);

	ipc->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (ipc->fd < 0) {
		wlr_log_errno(WLR_ERROR, "ng_ipc: socket");
		free(ipc);
		return NULL;
	}

	unlink(ipc->path);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	strncpy(addr.sun_path, ipc->path, sizeof(addr.sun_path) - 1);
	if (bind(ipc->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wlr_log_errno(WLR_ERROR, "ng_ipc: bind %s", ipc->path);
		close(ipc->fd);
		free(ipc);
		return NULL;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	ipc->source = wl_event_loop_add_fd(loop, ipc->fd, WL_EVENT_READABLE, handle_readable, ipc);

	setenv("NEUROS_COMP_SOCK", ipc->path, 1);
	wlr_log(WLR_INFO, "ng_ipc: listening on %s", ipc->path);
	return ipc;
}

void
ng_ipc_destroy(struct ng_ipc *ipc)
{
	if (!ipc)
		return;
	if (ipc->source)
		wl_event_source_remove(ipc->source);
	if (ipc->fd >= 0)
		close(ipc->fd);
	unlink(ipc->path);
	free(ipc);
}
