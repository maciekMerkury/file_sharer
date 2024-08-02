#pragma once
#include <linux/limits.h>
#include <sys/types.h>

typedef enum __attribute__((__packed__)) message_type {
	mt_hello,
	mt_ack,
	mt_nack,
	mt_file,
	mt_dir,
} message_type;

static const char *const default_user_name = "(???)";
#define MAX_HELLO_DATA_SIZE \
	(sizeof(message_type) + sizeof(hello_data_t) + NAME_MAX + 1)

typedef struct hello_data {
	/* len includes the null byte */
	size_t username_len;
	char username[];
} hello_data_t;

void get_name(hello_data_t *hello);
