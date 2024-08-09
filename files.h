#pragma once
#include <sys/types.h>

#include "core.h"
#include "stream.h"

typedef enum file_type { ft_reg, ft_dir } file_type;

typedef struct file {
	file_type type;
	mode_t permissions;
	off_t size;

	/* includes the null byte */
	/* contains alignment padding */
	/* relative to files_t.parent_dir */
	size_t path_size;
	char rel_path[];
} file_t;

const char *get_file_type_name(file_type file_type);

typedef struct files {
	off_t total_file_size;

	/* null-terminated */
	char *parent_path;
	size_t parent_path_len;

	stream_t filesa;
} files_t;

int create_files(const char *path, files_t *files);
void destroy_files(files_t *files);

typedef struct file_data {
	int fd;
	void *map;
	size_t size;
} file_data_t;

/* chdir to files_t.parent_dir before running */
/* will set file_data.map to NULL if file.size is 0 */
int open_and_map_file(file_t *file, file_data_t *file_data,
		      operation_type operation);
void destroy_file_data(file_data_t *file_data);
