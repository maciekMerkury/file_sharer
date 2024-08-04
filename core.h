#pragma once

#include "progress_bar.h"
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define DEFAULT_POLL_TIMEOUT (1000)

typedef enum operation { op_read, op_write } operation;

ssize_t exchange_data_with_socket(int soc, operation op,
				  const void *const restrict buf, size_t len,
				  progress_bar_t *const restrict prog_bar);
/*
 * returns the src ptr advanced by len bytes
 */
void *memcpyy(void *restrict dest, const void *restrict src, size_t len);

#define ERR(source)                                                      \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
	 exit(EXIT_FAILURE))
