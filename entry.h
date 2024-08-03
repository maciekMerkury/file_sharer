#pragma once
#include "message.h"
#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct entry {
        /*
         * can only be mt_file or mt_dir
         */
        message_type type;
	/*
        * total size, sans this metadata
        * for files, its just the size
        * for directories, its the total size of all the data (sort of like du -d0 <name>)
        */
	size_t size;

	char *name;

	union {
		struct file_data {
			mode_t permissions;
		} file;

		struct dir_data {
			size_t inner_count;
			struct entry *inners;
		} dir;
	} data;
} entry_t;

char *get_entry_type_name(entry_t *const entry);
int read_file_data(entry_t *entry, const char *const path);

/* if `dst` is NULL, `len` is disregarded, and new memory is allocated
 *
 * if `dst` is not NULL, `len` >= `calculate_total_size(entry)` must hold
 *
 * returns ptr to the whole structure (`dst` if provided) or NULL on error */
void *flatten_entry(const entry_t *entry, void *dst, size_t len);
