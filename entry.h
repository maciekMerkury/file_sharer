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
			int fd;
			/* size of this is mmap is `size` */
			void *map;
		} file;

		struct dir_data {
			size_t inner_count;
			struct entry *inners;
		} dir;
	} data;
} entry_t;



char *get_entry_type_name(entry_t *const entry);
/*
 * the path must not end in a /, even if its a dir */
int read_entry(entry_t *entry, char path[PATH_MAX]);

enum load_purpose { lp_read, lp_write };
/*
 * open and mmaps the files (not dirs) */
int load_entries(entry_t *entry, enum load_purpose purpose);
ssize_t total_entry_len(const entry_t *entry);

/* if `dst` is NULL, `len` is disregarded, and new memory is allocated
 *
 * if `dst` is not NULL, `len` >= `calculate_total_size(entry)` must hold
 *
 * returns ptr to the whole structure (`dst` if provided) or NULL on error */
void *deflate_entry(const entry_t *entry, void *dst, size_t len);

/* will allocate memory for `inners` if e->type == mt_dir
 *
 * TODO: think of a way for this function to not have to allocate memory
 */
void const *inflate_entry(entry_t *restrict e, const void *restrict mem);

/* will not deallocate entry
 *
 * assumes:
 * 	a) name was allocated
 * 	b) if type == mt_dir, all inners were allocated
 *
 * if the entry was created using inflate_entry, those hold */
void entry_deallocate(const entry_t *entry);
