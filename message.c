#include <bits/posix1_lim.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "files.h"
#include "message.h"

hello_data_t *create_hello_message(header_t *header)
{
	char username[LOGIN_NAME_MAX];
	if (getlogin_r(username, sizeof(username)) != 0) {
		memcpy(username, default_user_name, sizeof(default_user_name));
	}
	const size_t username_size = strlen(username) + 1;
	const size_t data_size = sizeof(hello_data_t) + username_size;

	hello_data_t *data = malloc(data_size);
	if (data == NULL)
		CORE_ERR("malloc");

	*header = (header_t){
		.type = mt_hello,
		.data_size = data_size,
	};

	data->username_size = username_size;
	memcpy(data->username, username, username_size);

	return data;

error:
	free(data);

	return NULL;
}

request_data_t *create_request_message(const files_t *restrict files,
				       header_t *restrict header)
{
	const char *root_dir_basename =
		((file_t *)files->filesa.data)[0].rel_path;
	const size_t filename_size = strlen(root_dir_basename) + 1;

	const size_t req_size = sizeof(request_data_t) + filename_size;
	request_data_t *data = malloc(req_size);

	if (data == NULL)
		CORE_ERR("malloc");

	*header = (header_t){
		.type = mt_req,
		.data_size = req_size,
	};

	*data = (request_data_t){
		.total_file_size = files->total_file_size,
		.file_type = ((file_t *)files->filesa.data)[0].type,
		.filename_size = filename_size,
	};
	memcpy(data->filename, root_dir_basename, filename_size);

	return data;
error:
	free(data);

	return NULL;
}

int send_msg(int soc, header_t *h, void *data)
{
	if (perf_soc_op(soc, op_write, h, sizeof(header_t), NULL) < 0)
		return -1;

	if (perf_soc_op(soc, op_write, data, h->data_size, NULL) < 0)
		return -1;

	return 0;
}

int receive_msg(int soc, header_t *restrict h, void *restrict *data)
{
	if (perf_soc_op(soc, op_read, h, sizeof(header_t), NULL) < 0)
		return -1;

	*data = realloc(*data, h->data_size);

	if (!*data) {
		perror("realloc");
		return -1;
	}

	if (perf_soc_op(soc, op_read, *data, h->data_size, NULL) < 0) {
		free(*data);
		return -1;
	}

	return 0;
}
