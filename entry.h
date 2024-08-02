#pragma once
#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>
/*
typedef struct entry {
	enum entry_type { et_file, et_dir } type;
	union entry_data {
		file_data_t file;
		dir_data_t dir;
	} data;
} entry_t;
*/

typedef struct entry {
	enum entry_type { et_file, et_dir } type;
	/*
     * total size.
     * for files, its just the size
     * for directories, its the total size of all the data (sort of like du -d0 <name>)
     */
	size_t size;

	/* consider whether this should be a dynamic array 
     * pros:
     *      lower mem usage
     *      less data to send
     * cons:
     *      the dir_data's inners array would not be of constant length, and would probably be have to be a linked list
     */
	char name[NAME_MAX + 1];

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
