#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "core.h"

void create_vector(vector_t *vector, size_t item_size)
{
	*vector = (vector_t){ .item_size = item_size };
}

void destroy_vector(vector_t *vector)
{
	free(vector->data);
}

void *vector_add_item(vector_t *vector)
{
	if ((vector->len + 1) * vector->item_size > vector->cap) {
		const size_t resize = vector->cap == 0 ? vector->item_size :
							 vector->cap;
		void *new_mem = realloc(vector->data, vector->cap + resize);
		if (LOG_PERROR(new_mem == NULL, "realloc"))
			return NULL;
		vector->data = new_mem;
		vector->cap += resize;
	}

	vector->len++;
	return (void *)((uintptr_t)vector->data +
			(vector->len - 1) * vector->item_size);
}

int vector_copy(vector_t *dest, const vector_t *src)
{
	*dest = (vector_t){ .item_size = src->item_size,
			    .cap = src->len * src->item_size,
			    .len = src->len,
			    .data = malloc(src->len * src->item_size) };

	if (LOG_PERROR(dest->data == NULL, "malloc"))
		return -1;

	memcpy(dest->data, src->data, dest->cap);

	return 0;
}

void create_stream(stream_t *stream)
{
	*stream = (stream_t){ 0 };
	create_vector(&stream->metadata, sizeof(size_t));
}

void destroy_stream(stream_t *stream)
{
	destroy_vector(&stream->metadata);
	free(stream->data);
}

void *stream_add_item(stream_t *stream, size_t size)
{
	const bool resize = stream->size + size > stream->cap;

	if (resize) {
		const size_t resize = stream->size + size > stream->cap * 2 ?
					      size :
					      stream->cap;
		void *new_mem = realloc(stream->data, stream->cap + resize);
		if (LOG_PERROR(new_mem == NULL, "realloc"))
			return NULL;
		stream->data = new_mem;
		stream->cap += resize;
	}

	*(size_t *)vector_add_item(&stream->metadata) = size;

	void *ret = (void *)((uintptr_t)stream->data + stream->size);
	stream->size += size;

	return ret;
}

void stream_iter_init(stream_iter_t *it, const stream_t *stream)
{
	*it = (stream_iter_t){
		.stream = stream,
		.curr = stream->data,
		.i = 0,
	};
}

void *stream_iter_next(stream_iter_t *it)
{
	void *curr = it->curr;

	if (it->i == it->stream->metadata.len)
		return NULL;

	it->curr = (void *)((uintptr_t)it->curr +
			    ((size_t *)it->stream->metadata.data)[it->i]);
	it->i++;

	return curr;
}
