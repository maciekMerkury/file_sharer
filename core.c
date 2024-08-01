// the gnu version of the basename function is needed
#define _GNU_SOURCE
#include "progress_bar.h"

#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pwd.h>
#include <unistd.h>
#include <stdint.h>

#include "core.h"

ssize_t send_all(const void *const buf, size_t len, int soc, progress_bar_t *const prog_bar)
{
	ssize_t sent = 0;
    int old_flags = 0;
	if (prog_bar) {
        old_flags = fcntl(soc, F_GETFL, 0);
        if (fcntl(soc, F_SETFL, old_flags | O_NONBLOCK) < 0) {
            perror("fcntl");
            return -1;
        }
		prog_bar_start(prog_bar);
    }

	ssize_t s;
	while (sent < len) {
		s = send(soc, (void *)((uintptr_t)buf + sent), len - sent, 0);
		if (s < 0) {
			if (errno != EWOULDBLOCK) {
                sent = s;
                break;
            }

			s = 0;
		}

		sent += s;

        if (prog_bar)
            prog_bar_advance(prog_bar, sent);
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

