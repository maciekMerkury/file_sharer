// the gnu version of the basename function is needed
#define _GNU_SOURCE
#include "core.h"
#include "progress_bar.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

ssize_t send_all(const void *const restrict buf, size_t len, int soc,
		 progress_bar_t *const restrict prog_bar)
{
	ssize_t sent = 0;
	int old_flags = 0;
    struct pollfd p = {
        .fd = soc,
        .events = POLLIN,
    };

	if (prog_bar) {
		old_flags = fcntl(soc, F_GETFL, 0);
		if (fcntl(soc, F_SETFL, old_flags | O_NONBLOCK) < 0) {
			perror("fcntl");
			return -1;
		}

		assert(!(old_flags & O_NONBLOCK));
		prog_bar_start(prog_bar);
	}

	ssize_t s;
	while (sent < len) {
		s = send(soc, (void *)((uintptr_t)buf + sent), len - sent, 0);
		if (s < 0) {
			if (errno != EWOULDBLOCK) {
				perror("send");
				sent = s;
				break;
			}
		} else {
			sent += s;
		}

		if (prog_bar) {
			prog_bar_advance(prog_bar, sent);
			int ret = poll(&p, 1, DEFAULT_POLL_TIMEOUT);
			if (ret == 0) {
				fprintf(stderr, "sending timed out\n");
				sent = -1;
				break;
			} else if (ret < 0) {
				perror("send_all poll");
				sent = -1;
				break;
			}
			assert(ret == 1);
	}
	}

	if (prog_bar) {
		if (fcntl(soc, F_SETFL, old_flags) < 0) {
			perror("fcntl");
			return -1;
		}
		prog_bar_finish(prog_bar);
	}

	return sent;
}

void expand_bash_path(char path[PATH_MAX], const char bash_path[PATH_MAX])
{
	if (bash_path[0] == '~') {
		struct passwd *pw = getpwuid(getuid());
		path = stpcpy(path, pw->pw_dir);
		bash_path++;
	}

	path = stpcpy(path, bash_path);
	if (*(path - 1) != '/') {
		path[0] = '/';
		path[1] = '\0';
	}
}

void *memcpyy(void *restrict dest, const void *restrict src, size_t len)
{
	memcpy(dest, src, len);
	return (void *)((uintptr_t)src + len);
}
