#define _GNU_SOURCE
#include <linux/limits.h>
#include <libgen.h>
#include <assert.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core.h"
#include "files.h"

#define MAX_FD 20

const char *get_file_type_name(file_type file_type)
{
	return file_type == ft_reg ? "file" : "directory";
}

static files_t *files;

static int fn(const char *path, const struct stat *s, int flags, struct FTW *f)
{
	static char buf[PATH_MAX];
	assert(files->parent_path);

	if (flags != FTW_F && flags != FTW_D) {
		fprintf(stderr,
			"file `%s` is an unsupported file type "
			"or an error occurred while reading it",
			path);
		return 0;
	}

	if (!realpath(path, buf))
		CORE_ERR("realpath");

	if (strstr(buf, files->parent_path) != buf) {
		fprintf(stderr, "symlinks are not supported: %s\n", path);
		return 0;
	}

	char *relative_path = buf + files->parent_path_len;
	if (files->parent_path[files->parent_path_len] != '/')
		++relative_path;

	const size_t relative_path_size = strlen(relative_path) + 1;
	const size_t path_size = relative_path_size + alignof(file_t) -
				 relative_path_size % alignof(file_t);
	const size_t struct_size = sizeof(file_t) + path_size;

	file_t *new_file = stream_allocate(&files->filesa, struct_size);

	*new_file = (file_t){
		.type = flags == FTW_F ? ft_reg : ft_dir,
		.permissions = s->st_mode,
		.size = flags == FTW_F ? s->st_size : 0,
		.path_size = path_size,
	};
	memcpy(new_file->rel_path, relative_path, relative_path_size);

	files->total_file_size += new_file->size;

	return 0;

error:
	return -1;
}

int create_files(const char *path, files_t *f)
{
	*f = (files_t){ 0 };
	files = f;

	if (!(files->parent_path = realpath(path, NULL)))
		CORE_ERR("realpath");

	// dirname modifies path argument
	files->parent_path = dirname(files->parent_path);
	files->parent_path_len = strlen(files->parent_path);

	if (nftw(path, &fn, MAX_FD, 0) < 0)
		CORE_ERR("nftw");

	return 0;

error:
	destroy_files(files);

	return -1;
}

void destroy_files(files_t *files)
{
	free(files->parent_path);
	stream_destroy(&files->filesa);
}

int open_and_map_file(file_t *file, file_data_t *file_data,
		      operation_type operation)
{
	assert(file->type == ft_reg);

	int open_flags = operation == op_read ?
				 O_RDONLY :
				 O_RDWR | O_CREAT | O_APPEND | O_EXCL;
	int map_flags = operation == op_read ? PROT_READ : PROT_WRITE;

	*file_data = (file_data_t){
		.size = file->size,
	};

	if ((file_data->fd =
		     open(file->rel_path, open_flags, file->permissions)) < 0) {
		fprintf(stderr, "rp: %s\n", file->rel_path);
		CORE_ERR("open");
	}

	if (file_data->size == 0)
		return 0;

	if ((file_data->map = mmap(NULL, file_data->size, map_flags,
				   MAP_FILE | MAP_SHARED, file_data->fd, 0)) ==
	    MAP_FAILED)
		CORE_ERR("mmap");

	return 0;

error:
	if (file_data->fd >= 0)
		close(file_data->fd);

	return -1;
}

void destroy_file_data(file_data_t *file_data)
{
	munmap(file_data->map, file_data->size);
	close(file_data->fd);
}
