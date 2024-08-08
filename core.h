#pragma once

#include "progress_bar.h"
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define DEFAULT_POLL_TIMEOUT (10000)

typedef enum operation { op_read, op_write } operation;

ssize_t exchange_data_with_socket(int soc, operation op, void *restrict buf,
				  size_t len,
				  progress_bar_t *const restrict prog_bar);

#define ERR(source)                                                      \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
	 exit(EXIT_FAILURE))

#define CORE_ERR(source)                                        \
	do {                                                    \
		perror(source);                                 \
		fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
		goto error;                                     \
	} while (0)

typedef struct size_info {
	double size;
	unsigned int unit_idx;
} size_info;

size_info bytes_to_size(size_t size);
const char *const unit(size_info info);
