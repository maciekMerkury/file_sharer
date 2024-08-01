#pragma once

#include "progress_bar.h"
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

static const char start_transfer_message[] = "start";
static const char file_size_units[4][4] = { "B", "kiB", "MiB", "GiB" };
typedef struct file_size {
    double size;
    unsigned int unit_idx;
} file_size_t;

file_size_t bytes_to_size(size_t size);

ssize_t send_all(const void *const buf, size_t len, int soc, progress_bar_t *prog_bar);

#define ERR(source)                                                     \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__),    \
	 exit(EXIT_FAILURE))
