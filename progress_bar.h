#pragma once
#include <sys/types.h>

typedef struct progress_bar {
	const char *title;
	size_t title_len;
	size_t max_val;
	struct timespec minimum_dt;

	size_t last_val;
	struct timespec prev_ts;
} progress_bar_t;

void prog_bar_init(progress_bar_t *bar, const char *const title,
		   const size_t max, const struct timespec minimum_dt);
int prog_bar_start(progress_bar_t *prog);
int prog_bar_advance(progress_bar_t *prog, const size_t curr_val);
int prog_bar_finish(progress_bar_t *bar);
