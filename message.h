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

typedef struct file_data {
	off_t size;
	char name[NAME_MAX + 1];
	/*
     * only the perms, not the filetype
     */
	mode_t mode;
} file_data_t;

int read_file_data(file_data_t *dst, const char *const path);
int read_file_data_from_fd(file_data_t *dst, const char *const path, int fd);

typedef struct dir_data {
	char name[NAME_MAX + 1];
	// TODO: rename
	/*
     * the total number of inner files & dirs, recursive
     */
	size_t sub_things;
	/*
     * the total size of the data, recursive
     */
	off_t total_data_size;
} dir_data_t;

/*
union message_data {
    struct hello_data hello;
    struct file_data file;
    struct dir_data dir;
};

typedef struct message {
    message_type type;
    union message_data data;
} message;

*/
