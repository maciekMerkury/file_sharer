#pragma once

#include <sys/types.h>

#define STRINGIFY(macro) ANOTHERSTRING(macro)
#define ANOTHERSTRING(macro) #macro

#define DEFAULT_POLL_TIMEOUT (10000)
#define DEFAULT_PORT (2137)

typedef enum operation { op_read, op_write } operation_type;

typedef struct vector {
	size_t item_size;

	size_t cap;
	size_t len;
	void *data;
} vector_t;

void create_vector(vector_t *vector, size_t item_size);
void destroy_vector(vector_t *vector);

void *vector_add_item(vector_t *vector);
int vector_copy(vector_t *dest, const vector_t *src);

typedef struct stream {
	vector_t metadata;

	size_t cap;
	size_t size;
	void *data;
} stream_t;

void create_stream(stream_t *stream);
void destroy_stream(stream_t *stream);

void *stream_add_item(stream_t *stream, size_t size);

typedef struct stream_iter {
	const stream_t *stream;
	void *curr;
	size_t i;
} stream_iter_t;

void stream_iter_init(stream_iter_t *it, const stream_t *stream);
void *stream_iter_next(stream_iter_t *it);
