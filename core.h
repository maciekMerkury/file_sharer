#pragma once

#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#include "progress_bar.h"

#define STRINGIFY(macro) ANOTHERSTRING(macro)
#define ANOTHERSTRING(macro) #macro

#define DEFAULT_POLL_TIMEOUT (10000)
#define DEFAULT_PORT (2137)

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

typedef enum operation { op_read, op_write } operation_type;

ssize_t perf_soc_op(int soc, operation_type op, void *restrict buf, size_t len,
		    progress_bar_t *const restrict prog_bar);
