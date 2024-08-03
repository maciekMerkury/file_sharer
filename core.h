#pragma once

#include "progress_bar.h"
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#define DEFAULT_POLL_TIMEOUT (1000)
ssize_t send_all(const void *const buf, size_t len, int soc,
		 progress_bar_t *prog_bar);

/*
 * returns the src ptr advanced by len bytes
 */
void *memcpyy(void *restrict dest, const void *restrict src, size_t len);

#define ERR(source)                                                      \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
	 exit(EXIT_FAILURE))
