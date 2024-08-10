#pragma once
#include <sys/types.h>

typedef struct stream {
	struct offsets {
		size_t cap;
		size_t len;
		size_t *sizes;
	} metadata;

	size_t cap;
	size_t size;
	void *data;
} stream_t;

void *stream_add_item(stream_t *stream, size_t size);
void destroy_stream(stream_t *stream);

typedef struct stream_iter {
	const stream_t *stream;
	void *curr;
	size_t i;
} stream_iter_t;

void stream_iter_init(stream_iter_t *it, const stream_t *stream);
void *stream_iter_next(stream_iter_t *it);

int send_stream(int soc, stream_t *restrict stream);
int recv_stream(int soc, stream_t *restrict stream);
