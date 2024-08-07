#include <sys/types.h>

typedef enum file_type { ft_reg, ft_dir } file_type;

typedef struct file {
	file_type type;
	mode_t permissions;
	off_t size;

	/* includes the null byte */
	size_t path_size;
	char path[];
} file_t;

char *get_file_type_name(file_t *file);

typedef struct files {
	off_t total_file_size;

	size_t files_size;
	file_t *files;
} files_t;

typedef struct {
	file_t *curr;
	const file_t *end;
} files_iter;

int create_files(const char *path, files_t *files);
void destroy_files(files_t *files);

void files_iter_init(files_iter *it, const files_t *files);
file_t *files_iter_next(files_iter *it);

file_t *begin_files(const files_t *files);
file_t *next_file(const file_t *file);
file_t *end_files(const files_t *files);
