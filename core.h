#pragma once

#include <sys/types.h>

#include "progress_bar.h"

#define STRINGIFY(macro) ANOTHERSTRING(macro)
#define ANOTHERSTRING(macro) #macro

#define DEFAULT_POLL_TIMEOUT (10000)
#define DEFAULT_PORT (2137)

typedef struct size_info {
	double size;
	unsigned int unit_idx;
} size_info;

size_info bytes_to_size(size_t size);
const char *const unit(size_info info);

typedef enum operation { op_read, op_write } operation_type;

ssize_t perf_soc_op(int soc, operation_type op, void *restrict buf, size_t len,
		    progress_bar_t *const restrict prog_bar);
