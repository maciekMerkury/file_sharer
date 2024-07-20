#pragma once

#include <linux/limits.h>
#include <sys/types.h>

typedef struct file_data {
	off_t size;
	char name[(long)NAME_MAX + 1];
} file_data_t;

int read_file_data(file_data_t *dst, const char *const path);

#define ERR(source)                                                      \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
	 exit(EXIT_FAILURE))
