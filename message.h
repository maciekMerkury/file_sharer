#pragma once
#include <linux/limits.h>
#include <sys/types.h>

#include "files.h"

typedef enum __attribute__((__packed__)) message_type {
	mt_hello,
	mt_req,
	mt_ack,
	mt_nack,
} message_type;

static const char default_user_name[] = "(???)";

typedef struct header {
	message_type type;
	size_t data_size;
} header_t;

typedef struct hello_data {
	/* len includes the null byte */
	size_t username_size;
	char username[];
} hello_data_t;

typedef struct request_data {
	off_t total_file_size;
	file_type file_type;

	/* includes the null byte */
	size_t filename_size;
	char filename[];
} request_data_t;

hello_data_t *create_hello_message(header_t *header);

request_data_t *create_request_message(const files_t *restrict files,
				       header_t *restrict header);

void create_metadata_header(header_t *restrict header,
			    const files_t *restrict files);
