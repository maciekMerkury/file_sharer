// the gnu version of the basename function is needed
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
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

ssize_t send_all(const void *const buf, size_t len, int soc)
{
	size_t sent = 0;

	while (sent < len) {
		const ssize_t s = send(soc, (void *)((uintptr_t)buf + sent),
				       len - sent, 0);
		if (s < 0)
			return s;

		sent += s;
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
