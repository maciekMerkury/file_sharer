#include "stream.h"
#include "core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void *stream_add_item(stream_t *stream, size_t size)
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
			ERR_GOTO("realloc");
		stream->metadata.sizes = new_mem;
		stream->metadata.cap += resize;
	}

	if (resize) {
		const size_t resize = stream->size + size > stream->cap * 2 ?
					      size :
					      stream->cap;
		void *new_mem = realloc(stream->data, stream->cap + resize);
		if (new_mem == NULL)
			ERR_GOTO("realloc");
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

void destroy_stream(stream_t *stream)
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

struct stream_info {
	size_t len;
	size_t size;
};

int send_stream(int soc, stream_t *restrict stream)
{
	struct stream_info sinfo = {
		.len = stream->metadata.len,
		.size = stream->size,
	};

	if (perf_soc_op(soc, op_write, &sinfo, sizeof(struct stream_info),
			NULL) < 0)
		return -1;

	if (perf_soc_op(soc, op_write, stream->metadata.sizes,
			sinfo.len * sizeof(size_t), NULL) < 0)
		return -1;

	if (perf_soc_op(soc, op_write, stream->data, sinfo.size, NULL) < 0)
		return -1;

	return 0;
}

int recv_stream(int soc, stream_t *restrict stream)
{
	struct stream_info sinfo;

	if (perf_soc_op(soc, op_read, &sinfo, sizeof(struct stream_info),
			NULL) < 0)
		return -1;

	*stream = (stream_t){
		.metadata = {
			.cap = sinfo.len * sizeof(size_t),
			.len = sinfo.len,
			.sizes = malloc(sinfo.len * sizeof(size_t)),
		},
		.cap = sinfo.size,
		.size = sinfo.size,
		.data = malloc(sinfo.size),
	};

	if (stream->metadata.sizes == NULL || stream->data == NULL)
		ERR_GOTO("malloc");

	if (perf_soc_op(soc, op_read, stream->metadata.sizes,
			sinfo.len * sizeof(size_t), NULL) < 0)
		goto error;

	if (perf_soc_op(soc, op_read, stream->data, sinfo.size, NULL) < 0)
		goto error;

	return 0;

error:
	free(stream->metadata.sizes);
	free(stream->data);

	*stream = (stream_t){ 0 };

	return -1;
}
