#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "files.h"

#define ERR(source)                                             \
	do {                                                    \
		perror(source);                                 \
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
		goto error;                                     \
	} while (0)

#define MAX_FD 20

char *get_file_type_name(file_t *file)
{
	return file->type == ft_reg ? "file" : "directory";
}

static files_t *files;
static int fn(const char *path, const struct stat *s, int flags, struct FTW *f)
{
	if (files->parent_dir == NULL) {
		if ((files->parent_dir = malloc(f->base + 1)) == NULL)
			ERR("malloc");

		memcpy(files->parent_dir, path, f->base);
		files->parent_dir[f->base] = '\0';
	}

	if (flags != FTW_F && flags != FTW_D) {
		fprintf(stderr,
			"file `%s` is an unsupported file type "
			"or an error occurred while reading it",
			path);
		return 0;
	}

	const size_t parent_dir_len = strlen(files->parent_dir);
	const size_t old_size = files->files_size;
	const size_t path_len = strlen(path) + 1 - parent_dir_len;
	const size_t alignment = alignof(file_t) - path_len % alignof(file_t);
	const size_t path_size = path_len + alignment;
	const size_t struct_size = sizeof(file_t) + path_size;

	files->files_size += struct_size;
	files->files = realloc(files->files, files->files_size);
	file_t *new_file = (file_t *)(((uintptr_t)files->files) + old_size);

	new_file->type = flags == FTW_F ? ft_reg : ft_dir;
	new_file->permissions = s->st_mode;
	new_file->size = flags == FTW_F ? s->st_size : 0;
	new_file->path_size = path_size;
	memcpy(new_file->path, path + parent_dir_len, path_len);

	files->total_file_size += new_file->size;

	return 0;

error:
	return -1;
}

int create_files(const char *path, files_t *f)
{
	*f = (files_t){ 0 };
	files = f;

	if (nftw(path, &fn, MAX_FD, 0) < 0)
		ERR("nftw");

	files->files[0].size = files->total_file_size;

	return 0;

error:
	destroy_files(files);

	return -1;
}

void destroy_files(files_t *files)
{
	free(files->parent_dir);
	free(files->files);
}

file_t *begin_files(const files_t *files)
{
	return files->files;
}

file_t *next_file(const file_t *file)
{
	return (file_t *)((uintptr_t)file + sizeof(file_t) + file->path_size);
}

file_t *end_files(const files_t *files)
{
	return (file_t *)((uintptr_t)files->files + files->files_size);
}

void files_iter_init(files_iter *it, const files_t *files)
{
	it->curr = files->files;
	it->end = (file_t *)((uintptr_t)files->files + files->files_size);
}

file_t *files_iter_next(files_iter *it)
{
	file_t *curr = it->curr;

	if (curr >= it->end)
		return NULL;

	it->curr =
		(file_t *)((uintptr_t)curr + sizeof(file_t) + curr->path_size);

	return curr;
}

int open_and_map_file(file_t *file, file_data_t *file_data,
		      file_operation operation)
{
	assert(file->type == ft_reg);

	int open_flags = operation == fo_read ?
				 O_RDONLY :
				 O_RDWR | O_CREAT | O_APPEND | O_EXCL;
	int map_flags = operation == fo_read ? PROT_READ : PROT_WRITE;

	file_data->size = file->size;

	if ((file_data->fd = open(file->path, open_flags)) < 0)
		ERR("open");
	if ((file_data->map = mmap(NULL, file_data->size, map_flags,
				   MAP_FILE | MAP_SHARED, file_data->fd, 0)) ==
	    MAP_FAILED)
		ERR("mmap");

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
