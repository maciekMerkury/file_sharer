#define _GNU_SOURCE

#include <ftw.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

#define MAX_FD 20

char *get_file_type_name(file_t *file)
{
	return file->type == ft_reg ? "file" : "directory";
}

static files_t *files;
static int fn(const char *path, const struct stat *s, int flags, struct FTW *f)
{
	if (flags != FTW_F && flags != FTW_D) {
		fprintf(stderr,
			"file `%s` is an unsupported file type"
			"or an error occurred while reading it",
			path);
		return 0;
	}

	const size_t old_size = files->files_size;
	const size_t path_len = strlen(path) + 1;
	const size_t alignment = alignof(file_t) - path_len % alignof(file_t);
	const size_t path_size = path_len + alignment;
	const size_t struct_size = sizeof(file_t) + path_size;

	files->files_size += struct_size;
	files->files = realloc(files->files, files->files_size);
	file_t *new_file = (file_t *)(((uintptr_t)files->files) + old_size);

	new_file->type = flags == FTW_F ? ft_reg : ft_dir;
	new_file->permissions = s->st_mode;
	new_file->size = s->st_size;
	new_file->path_size = path_size;
	memcpy(new_file->path, path, path_len);

	files->total_file_size += s->st_size;

	return 0;
}

int create_files(const char *path, files_t *f)
{
	*f = (files_t){ 0 };
	files = f;

	if (nftw(path, &fn, MAX_FD, 0) < 0) {
		free(files);
		return -1;
	}

	return 0;
}

void destroy_files(files_t *files)
{
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
