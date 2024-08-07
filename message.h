#pragma once
#include <linux/limits.h>
#include <sys/types.h>

#include "files.h"

typedef enum __attribute__((__packed__)) message_type {
	mt_hello,
	mt_req,
	mt_ack,
	mt_nack,
	mt_file,
	mt_dir,
	mt_meta,
} message_type;

static const char *const default_user_name = "(???)";

typedef struct headers {
	message_type type;
	size_t data_size;
} headers_t;

typedef struct hello_data {
	/* len includes the null byte */
	size_t username_size;
	char username[];
} hello_data_t;

typedef struct request_data {
	off_t total_file_size;

	/* includes the null byte */
	size_t filename_size;
	char filename[];
} request_data_t;

int create_hello_message(headers_t *headers, hello_data_t **hello_data);
int create_request_message(files_t *files, headers_t *headers,
			   request_data_t **request_data);
int create_metadata_headers(files_t *files, headers_t *headers);
