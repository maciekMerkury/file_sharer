// the gnu version of the basename function is needed
#define _GNU_SOURCE

#include <string.h>
#include <sys/stat.h>

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
