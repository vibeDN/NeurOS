/*
 * neuros-ctl - send one command line to the NeurOS compositor control socket.
 *
 *   neuros-ctl status Working
 *   neuros-ctl agent Claude
 *   neuros-ctl colors '#D97757' '#5C2B1C'
 *
 * MIT.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <command> [args...]\n", argv[0]);
		return 2;
	}

	const char *rt = getenv("XDG_RUNTIME_DIR");
	const char *sock = getenv("NEUROS_COMP_SOCK");
	char path[256];
	if (sock) {
		snprintf(path, sizeof(path), "%s", sock);
	} else {
		snprintf(path, sizeof(path), "%s/neuros-comp.sock", rt ? rt : "/tmp");
	}

	char line[512];
	size_t off = 0;
	for (int i = 1; i < argc && off < sizeof(line) - 2; i++) {
		int w = snprintf(line + off, sizeof(line) - off, "%s%s", i > 1 ? " " : "", argv[i]);
		if (w < 0)
			break;
		off += (size_t) w;
	}
	line[off++] = '\n';

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (sendto(fd, line, off, 0, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("sendto");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}
