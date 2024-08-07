#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "files.h"
#include "message.h"

int create_hello_message(headers_t *headers, hello_data_t **hello_data)
{
	const char *login = getlogin();
	if (login == NULL)
		login = default_user_name;

	const size_t login_size = strlen(login) + 1;
	const size_t data_size = sizeof(hello_data_t) + login_size;

	hello_data_t *data = malloc(data_size);

	if (data == NULL)
		CORE_ERR("malloc");

	headers->type = mt_hello;
	headers->data_size = data_size;

	data->username_size = login_size;
	memcpy(data->username, login, login_size);

	*hello_data = data;

	return 0;

error:
	free(data);

	return -1;
}

int create_request_message(files_t *files, headers_t *headers,
			   request_data_t **request_data)
{
	const size_t filename_size = strlen(files->root_dir_base) + 1;

	request_data_t *data = malloc(sizeof(request_data_t) + filename_size);

	if (data == NULL)
		CORE_ERR("malloc");

	headers->type = mt_req;
	headers->data_size = sizeof(request_data_t) + filename_size;

	data->total_file_size = files->total_file_size;
	data->filename_size = filename_size;
	memcpy(data->filename, files->root_dir_base, filename_size);

	return 0;

error:
	free(data);

	return -1;
}

int create_metadata_headers(files_t *files, headers_t *headers)
{
	headers->type = mt_meta;
	headers->data_size = files->files_size;

	return 0;
}
