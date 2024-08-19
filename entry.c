#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdalign.h>
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

	const char *relative_path = buf + entries->parent_path_len;
	if (entries->parent_path[entries->parent_path_len] != '/')
		++relative_path;

	const size_t relative_path_size = strlen(relative_path) + 1;
	const size_t path_size = relative_path_size + alignof(entry_t) -
				 relative_path_size % alignof(entry_t);
	const size_t struct_size = sizeof(entry_t) + path_size;

	entry_t *new_entry;
	if (LOG_IGNORE(LOG_CALL((new_entry = stream_add_item(&entries->entries,
							     struct_size)) ==
				NULL)))
		return 0;

	*new_entry = (entry_t){
		.type = flags == FTW_F ? et_reg : et_dir,
		.permissions = s->st_mode,
		.size = flags == FTW_F ? s->st_size : 0,
		.path_size = path_size,
	};
	memcpy(new_entry->rel_path, relative_path, relative_path_size);

	entries->total_file_size += new_entry->size;

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

int get_entry_handles(entry_t *entry, entry_handles_t *handles,
		      operation_type operation)
{
	assert(entry->type == et_reg);

	int open_flags = operation == op_read ?
				 O_RDONLY :
				 O_RDWR | O_CREAT | O_APPEND | O_EXCL;
	int map_flags = operation == op_read ? PROT_READ : PROT_WRITE;

	*handles = (entry_handles_t){
		.size = entry->size,
	};
	handles->fd = open(entry->rel_path, open_flags, entry->permissions);

	if (handles->fd < 0) {
		LOG_ERRORF(errno == EEXIST, EEXIST, "File `%s` exists",
			   entry->rel_path);

		LOG_PERROR(errno != EEXIST, "open");

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
