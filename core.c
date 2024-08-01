// the gnu version of the basename function is needed
#define _GNU_SOURCE
#include "progress_bar.h"

#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pwd.h>
#include <unistd.h>
#include <stdint.h>

#include "core.h"

int read_file_data(file_data_t *dst, const char *path)
{
	const char *name = basename(path);

	const int name_len = strlen(name);
	if (name_len > NAME_MAX)
		return -1;
	memcpy(dst->name, name, name_len);
	dst->name[name_len] = 0;

	struct stat f_stat;
	int ret;
	if ((ret = stat(path, &f_stat)) < 0)
		return ret;
	dst->size = f_stat.st_size;

	return 0;
}

int read_file_data_from_fd(file_data_t *dst, const char *const path, int fd)
{
	const char *name = basename(path);

	const int name_len = strlen(name);
	if (name_len > NAME_MAX)
		return -1;

	memcpy(dst->name, name, name_len);
	dst->name[name_len] = 0;

	struct stat s;
	if (fstat(fd, &s) < 0) {
		perror("fstat");
		return -1;
	}

	dst->size = s.st_size;
	return 0;
}

file_size_t bytes_to_size(size_t byte_size)
{
	double size = byte_size;
	unsigned int idx = 0;

	while (size >= 1024.0) {
		idx++;
		size /= 1024.0;
	}

	return (file_size_t){
		.size = size,
		.unit_idx = idx,
	};
}

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

