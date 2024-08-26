#include <bits/posix1_lim.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "log.h"
#include "message.h"

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
