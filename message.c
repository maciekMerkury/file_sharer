#define _GNU_SOURCE

#include "message.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void get_name(hello_data_t *hello)
{
    if (getlogin_r(hello->username, hello->username_len) < 0)
        strcpy(hello->username, default_user_name);
    hello->username_len = strlen(hello->username);
    printf("%lu\n", hello->username_len);
}

int read_file_data(file_data_t *dst, const char *const path)
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

