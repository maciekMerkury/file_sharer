#pragma once

#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

typedef struct file_data {
	off_t size;
	char name[NAME_MAX + 1];
} file_data_t;

int read_file_data(file_data_t *dst, const char *const path);
int read_file_data_from_fd(file_data_t *dst, const char *const path, int fd);
/*
 * bash_path has to be null-terminated
 * path is a null-terminated string ending with '/'
 */
void expand_bash_path(char path[PATH_MAX], const char bash_path[PATH_MAX]);

static const char start_transfer_message[] = "start";
static const char file_size_units[4][4] = { "B", "kiB", "MiB", "GiB" };
typedef struct file_size {
    double size;
    unsigned int unit_idx;
} file_size_t;

file_size_t bytes_to_size(size_t size);

ssize_t send_all(const void *const buf, size_t len, int soc, bool disp_prog);
void display_progress(const size_t curr, const size_t max);

#define ERR(source)                                                     \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),    \
	 exit(EXIT_FAILURE))
