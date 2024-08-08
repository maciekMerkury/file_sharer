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
	const size_t filename_size = strlen(files->root_dir_base) + 1;

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
		.filename_size = filename_size,
	};
	memcpy(data->filename, files->root_dir_base, filename_size);

	return data;
error:
	free(data);

	return NULL;
}

void create_metadata_header(header_t *restrict header,
			    const files_t *restrict files)
{
	*header = (header_t){
		.type = mt_meta,
		.data_size = files->files_size,
	};
}
