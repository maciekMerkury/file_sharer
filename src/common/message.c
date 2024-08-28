#include <assert.h>
#include <bits/posix1_lim.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "entry.h"
#include "log.h"
#include "message.h"
#include "progress_bar.h"

peer_info_t *create_pinfo_message(header_t *header)
{
	char username[LOGIN_NAME_MAX];
	if (getlogin_r(username, sizeof(username)) != 0)
		memcpy(username, default_user_name, sizeof(default_user_name));

	const size_t username_size = strlen(username) + 1;
	const size_t data_size = sizeof(peer_info_t) + username_size;

	peer_info_t *data = malloc(data_size);
	if (LOG_PERROR(data == NULL, "malloc"))
		return NULL;

	*header = (header_t){
		.type = mt_pinfo,
		.data_size = data_size,
	};

	data->username_size = username_size;
	memcpy(data->username, username, username_size);

	return data;
}

request_data_t *create_request_message(const entries_t *restrict entries,
				       header_t *restrict header)
{
	const entry_t *main_entry = (entry_t *)entries->entries.data;
	const entry_data_t *main_entry_data =
		align_entry_data(main_entry->data);
	const size_t main_entry_data_size = sizeof(entry_data_t) +
					    main_entry_data->path_size +
					    main_entry_data->content_type_size;

	const size_t req_size = sizeof(request_data_t) + main_entry_data_size;

	request_data_t *data = malloc(req_size);
	if (LOG_PERROR(data == NULL, "malloc"))
		return NULL;

	*header = (header_t){
		.type = mt_req,
		.data_size = req_size,
	};

	*data = (request_data_t){
		.total_file_size = entries->total_file_size,
		.entry_type = main_entry->type,
	};

	entry_data_t *entry_data = align_entry_data(data->entry_data);
	*entry_data =
		(entry_data_t){ .path_size = main_entry_data->path_size,
				.content_type_size =
					main_entry_data->content_type_size };

	memcpy(entry_data->buf, main_entry_data->buf,
	       main_entry_data->path_size + main_entry_data->content_type_size);

	return data;
}

#define SOCKET_OPERATION(op, ...) \
	op == op_read ? recv(__VA_ARGS__) : send(__VA_ARGS__)

ssize_t perf_soc_op(int soc, operation_type op, void *restrict buf, size_t len,
		    prog_bar_t *const restrict prog_bar)
{
	int event = op_read ? POLLIN : POLLOUT;

	ssize_t sent = 0;
	int old_flags = 0;
	struct pollfd p = {
		.fd = soc,
		.events = event,
	};

	if (prog_bar) {
		old_flags = fcntl(soc, F_GETFL, 0);
		if (LOG_PERROR(fcntl(soc, F_SETFL, old_flags | O_NONBLOCK) < 0,
			       "fcntl"))
			return -1;
	}

	ssize_t s;
	while (sent < len) {
		if (prog_bar) {
			int ret = poll(&p, 1, DEFAULT_POLL_TIMEOUT);
			if (LOG_ERROR(ret == 0, ETIMEDOUT, "sending timed out"))
				goto error;
			if (LOG_PERROR(ret < 0, "poll"))
				goto error;
		}
		s = SOCKET_OPERATION(op, soc, (void *)((uintptr_t)buf + sent),
				     len - sent, 0);

		if (s < 0 && errno == EWOULDBLOCK)
			continue;

		if (LOG_PERROR(s < 0 && errno != EWOULDBLOCK, "soc op"))
			goto error;

		sent += s;

		if (prog_bar)
			prog_bar_advance(prog_bar, s);
	}

	if (prog_bar)
		if (LOG_PERROR(fcntl(soc, F_SETFL, old_flags) < 0, "fcntl"))
			return -1;

	return sent;

error:
	if (prog_bar)
		if (LOG_PERROR(fcntl(soc, F_SETFL, old_flags) < 0, "fcntl"))
			return -1;
	return -1;
}

int send_msg(int soc, header_t *h, void *data)
{
	if (LOG_CALL(perf_soc_op(soc, op_write, h, sizeof(header_t), NULL) < 0))
		return -1;

	if (LOG_CALL(perf_soc_op(soc, op_write, data, h->data_size, NULL) < 0))
		return -1;

	return 0;
}

int receive_msg(int soc, header_t *restrict h, void *restrict *data)
{
	if (LOG_CALL(perf_soc_op(soc, op_read, h, sizeof(header_t), NULL) < 0))
		return -1;

	*data = realloc(*data, h->data_size);
	if (LOG_PERROR(*data == NULL, "realloc"))
		goto error;

	if (LOG_CALL(perf_soc_op(soc, op_read, *data, h->data_size, NULL) < 0))
		goto error;

	return 0;

error:
	free(*data);

	return -1;
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

	if (LOG_CALL(perf_soc_op(soc, op_write, &sinfo,
				 sizeof(struct stream_info), NULL) < 0))
		return -1;

	if (LOG_CALL(perf_soc_op(soc, op_write, stream->metadata.data,
				 sinfo.len * sizeof(size_t), NULL) < 0))
		return -1;

	if (LOG_CALL(perf_soc_op(soc, op_write, stream->data, sinfo.size,
				 NULL) < 0))
		return -1;

	return 0;
}

int recv_stream(int soc, stream_t *restrict stream)
{
	struct stream_info sinfo;

	if (LOG_CALL(perf_soc_op(soc, op_read, &sinfo,
				 sizeof(struct stream_info), NULL) < 0))
		return -1;

	*stream = (stream_t){
		.metadata = {
			.item_size = sizeof(size_t),
			.cap = sinfo.len * sizeof(size_t),
			.len = sinfo.len,
			.data = malloc(sinfo.len * sizeof(size_t)),
		},
		.cap = sinfo.size,
		.size = sinfo.size,
		.data = malloc(sinfo.size),
	};

	if (LOG_PERROR(stream->metadata.data == NULL || stream->data == NULL,
		       "malloc"))
		goto error;

	if (LOG_CALL(perf_soc_op(soc, op_read, stream->metadata.data,
				 sinfo.len * sizeof(size_t), NULL) < 0))
		goto error;

	if (LOG_CALL(perf_soc_op(soc, op_read, stream->data, sinfo.size, NULL) <
		     0))
		goto error;

	return 0;

error:
	free(stream->metadata.data);
	free(stream->data);

	*stream = (stream_t){ 0 };

	return -1;
}
