#include "stream.h"
#include "core.h"

#include <stdint.h>
#include <stdio.h>

void *stream_allocate(stream_t *stream, size_t size)
{
	const bool resize_metadata =
		(stream->metadata.len + 1) * sizeof(size_t) >
		stream->metadata.cap;
	const bool resize = stream->size + size > stream->cap;

	if (resize_metadata) {
		const size_t resize = stream->metadata.cap == 0 ?
					      sizeof(size_t) :
					      stream->metadata.cap;
		void *new_mem = realloc(stream->metadata.sizes,
					stream->metadata.cap + resize);
		if (new_mem == NULL)
			CORE_ERR("realloc");
		stream->metadata.sizes = new_mem;
		stream->metadata.cap += resize;
	}

	if (resize) {
		const size_t resize = stream->size + size > stream->cap * 2 ?
					      size :
					      stream->cap;
		void *new_mem = realloc(stream->data, stream->cap + resize);
		if (new_mem == NULL)
			CORE_ERR("realloc");
		stream->data = new_mem;
		stream->cap += resize;
	}

	stream->metadata.sizes[stream->metadata.len] = size;
	stream->metadata.len++;

	void *ret = (void *)((uintptr_t)stream->data + stream->size);
	stream->size += size;

	return ret;

error:
	return NULL;
}

void stream_destroy(stream_t *stream)
{
	free(stream->metadata.sizes);
	free(stream->data);
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
			    it->stream->metadata.sizes[it->i]);
	it->i++;

	return curr;
}
