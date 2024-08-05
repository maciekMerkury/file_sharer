#pragma once

#include "entry.h"
typedef struct entry_iter {
	entry_t *obj;
	ssize_t dir_idx;
	struct entry_iter *prev;
} entry_iter_t;

/*
 * assumes that `it` must be free'd */
entry_t *next(entry_iter_t *it);

/*
 * just a util fn, mallocs */
entry_iter_t *entry_iter_begin(entry_t *obj);

