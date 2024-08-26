#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gio/gio.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "log.h"
#include "stream.h"

#define MAX_FD 20

const char *get_entry_type_name(entry_type entry_type)
{
	return entry_type == et_reg ? "file" : "directory";
}

entry_data_t *align_entry_data(const char data[])
{
	const size_t reminder = (uintptr_t)data % alignof(entry_data_t);
	return (entry_data_t *)(reminder ? (uintptr_t)data +
						   alignof(entry_data_t) -
						   reminder :
					   (uintptr_t)data);
}

const char *get_entry_rel_path(const char data[])
{
	return align_entry_data(data)->buf;
}

const char *get_entry_content_type(const char data[])
{
	const entry_data_t *entry_data = align_entry_data(data);
	return entry_data->buf + entry_data->path_size;
}

static entries_t *entries;

static int fn(const char *path, const struct stat *s, int flags, struct FTW *f)
{
	static char buf[PATH_MAX];
	assert(entries->parent_path);

	if (flags != FTW_F && flags != FTW_D) {
		fprintf(stderr,
			"file `%s` is an unsupported file type "
			"or an error occurred while reading it\n",
			path);
		return 0;
	}

	if (LOG_IGNORE(LOG_PERROR(realpath(path, buf) == NULL, "realpath")))
		return 0;

	if (strstr(buf, entries->parent_path) != buf) {
		fprintf(stderr,
			"symlinks outside of the root folder "
			"are not supported: %s\n",
			path);
		return 0;
	}

	GFile *file = g_file_new_for_path(path);
	GFileInfo *file_info =
		g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  (GFileQueryInfoFlags)0, NULL, NULL);

	const char *content_type = g_file_info_get_content_type(file_info);
	const size_t content_type_size = strlen(content_type) + 1;

	const char *relative_path = buf + entries->parent_path_len;
	if (entries->parent_path[entries->parent_path_len] != '/')
		++relative_path;
	const size_t rel_path_size = strlen(relative_path) + 1;

	const size_t data_size =
		sizeof(entry_data_t) + rel_path_size + content_type_size;
	const size_t data_reminder = data_size % alignof(entry_data_t);
	const size_t data_alloc_size =
		data_reminder ?
			data_size + alignof(entry_data_t) - data_reminder :
			data_size;

	const size_t struct_size = sizeof(entry_t) + data_alloc_size;
	const size_t reminder = struct_size % alignof(entry_t);
	const size_t alloc_size =
		reminder ? struct_size + alignof(entry_t) - reminder :
			   struct_size;

	entry_t *new_entry;
	if (LOG_IGNORE(LOG_CALL((new_entry = stream_add_item(&entries->entries,
							     alloc_size)) ==
				NULL)))
		goto error;

	*new_entry = (entry_t){
		.type = flags == FTW_F ? et_reg : et_dir,
		.permissions = s->st_mode,
		.size = flags == FTW_F ? s->st_size : 0,
	};

	entry_data_t *data = align_entry_data(new_entry->data);

	*data = (entry_data_t){ .path_size = rel_path_size,
				.content_type_size = content_type_size };

	memcpy(data->buf, relative_path, rel_path_size);
	memcpy(data->buf + rel_path_size, content_type, content_type_size);

	entries->total_file_size += new_entry->size;

error:
	g_object_unref(file_info);
	g_object_unref(file);

	return 0;
}

int create_entries(const char *path, entries_t *e)
{
	*e = (entries_t){ 0 };
	entries = e;
	create_stream(&entries->entries);

	if (LOG_PERROR((entries->parent_path = realpath(path, NULL)) == NULL,
		       "realpath"))
		goto error;

	// dirname modifies path argument
	entries->parent_path = dirname(entries->parent_path);
	entries->parent_path_len = strlen(entries->parent_path);

	if (LOG_PERROR(nftw(path, &fn, MAX_FD, 0) < 0, "nftw"))
		goto error;

	return 0;

error:
	destroy_entries(entries);

	return -1;
}

void destroy_entries(entries_t *entries)
{
	free(entries->parent_path);
	destroy_stream(&entries->entries);
}

int get_entry_handles(int parent_dir_fd, entry_t *entry,
		      entry_handles_t *handles, operation_type operation)
{
	assert(entry->type == et_reg);

	int open_flags = operation == op_read ?
				 O_RDONLY :
				 O_RDWR | O_CREAT | O_APPEND | O_EXCL;
	int map_flags = operation == op_read ? PROT_READ : PROT_WRITE;

	*handles = (entry_handles_t){
		.size = entry->size,
		.fd = openat(parent_dir_fd, get_entry_rel_path(entry->data),
			     open_flags, entry->permissions)
	};

	if (handles->fd < 0) {
		LOG_ERRORF(errno == EEXIST, EEXIST, "File `%s` exists",
			   get_entry_rel_path(entry->data));

		LOG_PERROR(errno != EEXIST, "openat");

		goto error;
	}

	if (handles->size == 0)
		return 0;

	handles->map = mmap(NULL, handles->size, map_flags,
			    MAP_FILE | MAP_SHARED, handles->fd, 0);
	if (LOG_PERROR(handles->map == MAP_FAILED, "mmap"))
		goto error;

	return 0;

error:
	if (handles->fd >= 0)
		close(handles->fd);

	return -1;
}

void close_entry_handles(entry_handles_t *handles)
{
	munmap(handles->map, handles->size);
	close(handles->fd);
}
