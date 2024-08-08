#pragma once
#include <sys/types.h>

typedef enum file_type { ft_reg, ft_dir } file_type;

typedef struct file {
	file_type type;
	mode_t permissions;
	off_t size;

	/* includes the null byte */
	/* contains alignment padding */
	/* relative to files_t.parent_dir */
	size_t path_size;
	char path[];
} file_t;

char *get_file_type_name(file_type file_type);

typedef struct files {
	off_t total_file_size;

	/* null-terminated */
	char *parent_dir;
	char *root_dir_base;

	size_t files_size;
	file_t *files;
} files_t;

int create_files(const char *path, files_t *files);
void destroy_files(files_t *files);

typedef struct {
	file_t *curr;
	const file_t *end;
} files_iter;

void files_iter_init(files_iter *it, const files_t *files);
file_t *files_iter_next(files_iter *it);

typedef struct file_data {
	int fd;
	void *map;
	size_t size;
} file_data_t;

typedef enum file_operation { fo_read, fo_write } file_operation;

/* chdir to files_t.parent_dir before running */
int open_and_map_file(file_t *file, file_data_t *file_data,
		      file_operation operation);
void destroy_file_data(file_data_t *file_data);
