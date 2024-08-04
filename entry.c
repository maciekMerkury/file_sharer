// the gnu version of the basename function is needed
#define _GNU_SOURCE

#include <sys/types.h>
#include "core.h"
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

ssize_t total_entry_len(const entry_t *entry)
{
	size_t total_len =
		sizeof(message_type) + sizeof(size_t) * 2 + strlen(entry->name);

	switch (entry->type) {
	case mt_file:
		total_len += sizeof(mode_t);
		break;
	case mt_dir:
		total_len += sizeof(size_t);
		for (size_t i = 0; i < entry->data.dir.inner_count; i++) {
			total_len +=
				total_entry_len(&entry->data.dir.inners[i]);
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
		dst = mempcpy(dst, &entry->data.file.permissions,
			      sizeof(mode_t));
		break;
	case mt_dir:
		dst = mempcpy(dst, &entry->data.dir.inner_count,
			      sizeof(size_t));
		for (size_t i = 0; i < entry->data.dir.inner_count; ++i) {
			dst = populate_mem(dst, &entry->data.dir.inners[i]);
		}
		break;
	default:
		return NULL;
	}

	return dst;
}

void *deflate_entry(const entry_t *entry, void *dst, size_t len)
{
	ssize_t size = total_entry_len(entry);
	if (size < 0)
		return NULL;

	// this is just a safety check, but should never be actually executed if the func is used correctly
	if (dst && len < size)
		return NULL;

	void *data;
	if (dst) {
		data = dst;
	} else {
		data = malloc(size);
		if (!data)
			return NULL;
	}

	if (!populate_mem(data, entry)) {
		if (!dst)
			free(data);
		return NULL;
	}

	return data;
}

void const *inflate_entry(entry_t *restrict e, const void *restrict mem)
{
	mem = memcpyy(&e->type, mem, sizeof(message_type));
	mem = memcpyy(&e->size, mem, sizeof(size_t));

	size_t name_len;
	mem = memcpyy(&name_len, mem, sizeof(size_t));
	e->name = malloc(name_len + 1);

	mem = memcpyy(e->name, mem, name_len);
	e->name[name_len] = '\0';

	switch (e->type) {
	case mt_file:
		mem = memcpyy(&e->data.file.permissions, mem, sizeof(mode_t));
		break;
	case mt_dir: {
		size_t inner_count;
		mem = memcpyy(&inner_count, mem, sizeof(size_t));
		e->data.dir.inner_count = inner_count;
		e->data.dir.inners = malloc(inner_count * sizeof(entry_t));
		for (size_t i = 0; i < inner_count; ++i)
			mem = inflate_entry(e->data.dir.inners + i, mem);
		break;
	}
	default:
		return NULL;
	}

	return mem;
}

void entry_deallocate(const entry_t *entry)
{
	free(entry->name);

	if (entry->type == mt_file)
		return;

	const size_t ic = entry->data.dir.inner_count;
	const entry_t *inners = entry->data.dir.inners;

	for (size_t i = 0; i < ic; ++i) {
		entry_deallocate(inners + i);
	}
	free(entry->data.dir.inners);
}
