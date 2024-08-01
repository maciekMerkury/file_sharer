#pragma once

#include "progress_bar.h"
#include <linux/limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
/*
 * bash_path has to be null-terminated
 * path is a null-terminated string ending with '/'
 */
void expand_bash_path(char path[PATH_MAX], const char bash_path[PATH_MAX]);

ssize_t send_all(const void *const buf, size_t len, int soc,
		 progress_bar_t *prog_bar);

#define ERR(source)                                                      \
	(perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
	 exit(EXIT_FAILURE))
