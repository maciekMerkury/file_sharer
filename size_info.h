#pragma once
#include <sys/types.h>

typedef struct size_info {
    double size;
    unsigned int unit_idx;
} size_info;

size_info bytes_to_size(size_t size);
const char *const unit(size_info info);

