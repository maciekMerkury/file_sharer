#pragma once
#include <linux/limits.h>
#include <stdio.h>
#include <sys/types.h>

#include "entry.h"

typedef enum __attribute__((__packed__)) message_type {
	mt_pinfo,
	mt_req,
	mt_ack,
	mt_nack,
} message_type;

static const char default_user_name[] = "(???)";

typedef struct header {
	message_type type;
	size_t data_size;
} header_t;

typedef struct peer_info {
	/* len includes the null byte */
	size_t username_size;
	char username[];
} peer_info_t;

typedef struct request_data {
	off_t total_file_size;
	entry_type entry_type;

	/* padding + entry_data_t */
	char entry_data[];
} request_data_t;

peer_info_t *create_pinfo_message(header_t *header);

request_data_t *create_request_message(const entries_t *restrict entries,
				       header_t *restrict header);

int send_stream(int soc, stream_t *restrict stream);
int recv_stream(int soc, stream_t *restrict stream);

int send_msg(int soc, header_t *h, void *data);
/* data must be either NULL or ptr to malloced memory */
int receive_msg(int soc, header_t *restrict h, void *restrict *data);
