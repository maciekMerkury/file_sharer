#define _GNU_SOURCE

#include <sys/types.h>
#include "entry.h"
#include "message.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *get_entry_type_name(entry_t *entry)
{
	return entry->type == mt_file ? "file" : "directory";
}

#define ALL_PERMISSIONS (S_IRWXU | S_IRWXG | S_IRWXO)
int read_file_data(entry_t *dst, const char *const path)
{
	dst->type = mt_file;
	const char *name = basename(path);

	const int name_len = strlen(name);
	if (name_len > NAME_MAX)
		return -1;

	dst->name = strdup(name);

	struct stat s;
	if (stat(path, &s) < 0) {
		perror("fstat");
		return -1;
	}

	dst->size = s.st_size;
	dst->data.file.permissions = s.st_mode & ALL_PERMISSIONS;

	return 0;
}

static ssize_t total_entry_len(const entry_t *entry)
{
	size_t total_len = sizeof(message_type) + sizeof(size_t) * 2 + strlen(entry->name);

	switch (entry->type) {
		case mt_file:
			total_len += sizeof(mode_t);
			break;
		case mt_dir:
			total_len += sizeof(size_t);
			for (size_t i = 0; i < entry->data.dir.inner_count; i++) {
				total_len += total_entry_len(&entry->data.dir.inners[i]);
			}
			break;
		default:
			return -1;
	}

	return total_len;
}


/* does not copy the null byte from name, but does prepend name with `size_t name_len`
 *
 * returns the ptr to the byte after the last written byte (ret val of the last mempcpy call) 
 *
 * the caller must guarantee that `dst` is at least `total_entry_len(entry)` long
 *
 * this function uses `mempcpy` which is a part of glibc, but it makes my life easier. */
static void *populate_mem(void *dst, const entry_t *restrict entry)
{
	dst = mempcpy(dst, &entry->type, sizeof(message_type));
	dst = mempcpy(dst, &entry->size, sizeof(size_t));
	size_t name_len = strlen(entry->name);

	dst = mempcpy(dst, &name_len, sizeof(size_t));
	dst = mempcpy(dst, entry->name, name_len);

	switch (entry->type) {
		case mt_file:
			dst = mempcpy(dst, &entry->data.file.permissions, sizeof(mode_t));
			break;
		case mt_dir:
			dst = mempcpy(dst, &entry->data.dir.inner_count, sizeof(size_t));
			for (size_t i = 0; i < entry->data.dir.inner_count; ++i) {
				dst = populate_mem(dst, &entry->data.dir.inners[i]);
			}
			break;
		default:
			return NULL;
	}

	return dst;
}

/* if `dst` is NULL, `len` is disregarded, and new memory is allocated
 *
 * if `dst` is not NULL, `len` >= `calculate_total_size(entry)` must hold
 *
 * returns ptr to the whole structure (`dst` if provided) or NULL on error */
void *flatten_entry(const entry_t *entry, void *dst, size_t len)
{
	ssize_t size = total_entry_len(entry);
	if (size < 0) return NULL;

	// this is just a safety check, but should never be actually executed if the func is used correctly
	if (dst && len < size) return NULL;
	
	void *data;
	if (dst) {
		data = dst;
	} else {
		data = malloc(size);
		if (!data) return NULL;
	}

	if (!populate_mem(data, entry)) {
		if (!dst) free(data);
		return NULL;
	}

	return data;
}

