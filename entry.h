#include "message.h"

typedef struct entry {
	enum entry_type { et_file, et_dir } type;
	union entry_data {
		file_data_t file;
		dir_data_t dir;
	} data;
} entry_t;

char *get_entry_type_name(entry_t *entry);

char *get_entry_name(entry_t *entry);

size_t get_entry_size(entry_t *entry);


