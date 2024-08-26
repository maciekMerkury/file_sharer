#pragma once
#include <sys/types.h>

#include "core.h"

typedef enum entry_type { et_reg, et_dir } entry_type;

typedef struct entry_data {
	size_t path_size;
	size_t content_type_size;

	/* buf layout: rel_path nil content_type nil */
	char buf[];
} entry_data_t;

typedef struct entry {
	entry_type type;
	mode_t permissions;
	off_t size;

	/* padding + entry_data_t */
	char data[];
} entry_t;

const char *get_entry_type_name(entry_type entry_type);
entry_data_t *align_entry_data(const char data[]);
const char *get_entry_rel_path(const char entry_data[]);
const char *get_entry_content_type(const char entry_data[]);

typedef struct entries {
	off_t total_file_size;

	/* null-terminated */
	char *parent_path;
	size_t parent_path_len;

	stream_t entries;
} entries_t;

int create_entries(const char *path, entries_t *entries);
void destroy_entries(entries_t *entries);

typedef struct entry_handles {
	int fd;
	void *map;
	size_t size;
} entry_handles_t;

/* will set entry_handles.map to NULL if entry.size is 0 */
int get_entry_handles(int parent_path_fd, entry_t *entry,
		      entry_handles_t *handles, operation_type operation);
void close_entry_handles(entry_handles_t *handles);
