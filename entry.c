#define _GNU_SOURCE

#include "entry.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

char *get_entry_type_name(entry_t *entry)
{
	return entry->type == et_file ? "file" : "directory";
}

#define ALL_PERMISSIONS (S_IRWXU | S_IRWXG | S_IRWXO)
int read_file_data(entry_t *dst, const char *const path)
{
	const char *name = basename(path);

	const int name_len = strlen(name);
	if (name_len > NAME_MAX)
		return -1;

	memcpy(dst->name, name, name_len);
	dst->name[name_len] = 0;

	struct stat s;
	if (stat(path, &s) < 0) {
		perror("fstat");
		return -1;
	}

	dst->size = s.st_size;
	dst->data.file.permissions = s.st_mode & ALL_PERMISSIONS;

	return 0;
}
