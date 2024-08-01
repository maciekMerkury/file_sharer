#include "size_info.h"
#include <stddef.h>

#define UNIT_LEN 4
static const char size_units[UNIT_LEN][4] = { "B", "kiB", "MiB", "GiB" };

size_info bytes_to_size(size_t byte_size)
{
	double size = byte_size;
	unsigned int idx = 0;

	while (size >= 1024.0) {
		idx++;
		size /= 1024.0;
	}

	return (size_info){
		.size = size,
		.unit_idx = idx,
	};
}

const char *const unit(size_info info)
{
    return (info.unit_idx < UNIT_LEN) ? size_units[info.unit_idx] : NULL;
}

