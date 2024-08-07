// the gnu version of the basename function is needed
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <linux/limits.h>
#include "core.h"
#include "entry.h"
#include "message.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

char *get_entry_type_name(entry_t *entry)
{
	return entry->type == mt_file ? "file" : "directory";
}

#define ALL_PERMISSIONS (S_IRWXU | S_IRWXG | S_IRWXO)
static int read_file(entry_t *dst, struct stat *s, const char path[PATH_MAX])
{
	assert(S_ISREG(s->st_mode));

	dst->type = mt_file;
	dst->size = s->st_size;

	struct file_meta_data data = {
		.permissions = s->st_mode & ALL_PERMISSIONS,
	};

	if ((data.fd = open(path, O_RDONLY)) < 0) {
		perror("open");
		return -1;
	}

	if ((data.map = mmap(NULL, dst->size, PROT_READ, MAP_FILE | MAP_SHARED,
			     data.fd, 0)) == MAP_FAILED) {
		perror("mmap");
		close(data.fd);
		return -1;
	}

	dst->data.file = data;

	return 0;
}

static int write_file(entry_t *src, char path[PATH_MAX])
{
	assert(src->type == mt_file);
	int ret = 0;

	const size_t previous_path_len = strlen(path);
	strcat(path, "/");
	strcat(path, src->name);

	struct file_meta_data data = { 0 };

	if ((data.fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_EXCL,
			    src->data.file.permissions)) < 0)
		CORE_ERR("open");

	if (ftruncate(data.fd, src->size) < 0)
		CORE_ERR("ftruncate");

	if ((data.map = mmap(NULL, src->size, PROT_WRITE, MAP_FILE | MAP_SHARED,
			     data.fd, 0)) == MAP_FAILED)
		CORE_ERR("mmap");

	src->data.file = data;

error:
	path[previous_path_len] = 0;

	if (data.fd >= 0)
		close(data.fd);

	return ret;
}

static int read_dir(entry_t *dst, struct stat *s, char path[PATH_MAX])
{
	dst->type = mt_dir;
	int ret;

	printf("p: %s\t", path);
	const size_t previous_path_len = strlen(path);
//	strcat(path, "/");
//	strcat(path, dst->name);
//	strcat(path, "/");
	const size_t base_path_len = strlen(path);
	printf("p2: %s\n", path);

	DIR *dir = opendir(path);

	if (!dir) {
		perror("opendir");
		ret = -1;
		goto cleanup;
	}

	struct dir_data data = {
		.inner_count = 0,
		.inners = NULL,
	};
	struct dirent *en;
	errno = 0;
	ret = 0;
	while ((en = readdir(dir))) {
		data.inners = realloc(data.inners, ++data.inner_count);

		strcat(path+base_path_len, en->d_name);
		ret = read_entry(data.inners + data.inner_count, path);
		path[base_path_len] = 0;
		if (ret < 0)
			goto cleanup;
	}
	if (errno != 0) {
		perror("readdir");
		ret = -1;
		goto cleanup;
	}

	size_t size = 0;
	for (size_t i = 0; i < data.inner_count; ++i) {
		size += data.inners[i].size;
	}
	dst->size = size;

	ret = 0;
	dst->data.dir = data;
cleanup:
	path[previous_path_len] = 0;

	return ret;
}

static int write_dir(entry_t *src, char path[PATH_MAX])
{
	assert(src->type == mt_dir);
	int ret = 0;

	const size_t previous_path_len = strlen(path);
	strcat(path, "/");
	strcat(path, src->name);
	const size_t base_path_len = strlen(path);

	if (mkdir(path, 0644) < 0)
		CORE_ERR("mkdir");

	for (size_t i = 0; i < src->data.dir.inner_count; i++) {
		if (write_entry(src->data.dir.inners + i, path) < 0)
			CORE_ERR("write_entry");
		path[base_path_len] = 0;
	}

error:
	path[previous_path_len] = 0;

	return ret;
}

int read_entry(entry_t *entry, char path[PATH_MAX])
{
	struct stat s;
	if (stat(path, &s) < 0) {
		perror("stat");
		return -1;
	}

	if (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode))
		return -1;

	const size_t path_len = strlen(path);
	if (path[path_len - 1] == '/' ) path[path_len - 1] = 0;
	printf("p: %s\t", path);
	const char *name = basename(path);
	printf("n: %s\n", name);
	if (S_ISDIR(s.st_mode)) path[path_len - 1] = '/';
	if (strlen(name) > NAME_MAX) return -1;
	entry->name = strdup(name);

	switch (s.st_mode & S_IFMT) {
	case S_IFREG:
		return read_file(entry, &s, path);
	case S_IFDIR:
		return read_dir(entry, &s, path);
	}

	return -1;
}

int write_entry(entry_t *entry, char path[PATH_MAX])
{
	switch (entry->type) {
	case mt_file:
		return write_file(entry, path);
	case mt_dir:
		return write_dir(entry, path);
	default:
		return -1;
	}
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

	if (entry->type == mt_file) {
		munmap(entry->data.file.map, entry->size);
		close(entry->data.file.fd);
		return;
	}

	const size_t ic = entry->data.dir.inner_count;
	const entry_t *inners = entry->data.dir.inners;

	for (size_t i = 0; i < ic; ++i) {
		entry_deallocate(inners + i);
	}
	free(entry->data.dir.inners);
}
