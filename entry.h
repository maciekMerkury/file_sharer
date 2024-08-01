#include "message.h"

typedef enum entry_type { et_file, et_directory } entry_type;

typedef struct entry {
	entry_type type;
	union message_data data;
} entry_t;

char *get_entry_type_name(entry_t *entry);

char *get_entry_name(entry_t *entry);

size_t get_entry_size(entry_t *entry);


