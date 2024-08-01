#include "entry.h"

char *get_entry_type_name(entry_t *entry)
{
	return entry->type == et_file ? "file" : "directory";
}

char *get_entry_name(entry_t *entry)
{
	return entry->type == et_file ? entry->data.file.name :
					entry->data.dir.name;
}

size_t get_entry_size(entry_t *entry)
{
	return entry->type == et_file ? entry->data.file.size :
					entry->data.dir.total_data_size;
}
