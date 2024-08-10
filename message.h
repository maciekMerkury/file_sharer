#pragma once
#include <linux/limits.h>
#include <stdio.h>
#include <sys/types.h>

#include "entry.h"

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
	entry_type entry_type;

	/* includes the null byte */
	size_t filename_size;
	char filename[];
} request_data_t;

hello_data_t *create_hello_message(header_t *header);

request_data_t *create_request_message(const entries_t *restrict entries,
				       header_t *restrict header);

int send_msg(int soc, header_t *h, void *data);
/* data must be either NULL or ptr to malloced memory */
int receive_msg(int soc, header_t *restrict h, void *restrict *data);
