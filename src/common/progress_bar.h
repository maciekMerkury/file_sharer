#pragma once
#include <sys/types.h>

#include "core.h"

typedef struct size_info {
	double size;
	unsigned int unit_idx;
} size_info;

size_info bytes_to_size(size_t size);
const char *const unit(size_info info);

typedef struct prog_bar {
	stream_t *entries;
	size_t max_val;
	stream_iter_t it;
	void *curr;

	struct timespec start_ts;
	size_t total_val;

	struct timespec ts;
	size_t val;
} prog_bar_t;

void prog_bar_init(prog_bar_t *bar, stream_t *entries, size_t max_val);
void prog_bar_next(prog_bar_t *bar);
void prog_bar_advance(prog_bar_t *bar, size_t step);
