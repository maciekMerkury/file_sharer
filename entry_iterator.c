#include "entry_iterator.h"
#include "message.h"
#include <stdlib.h>
#include <string.h>

#define get_prev(it) do { entry_iter_t *tmp = it; *it = *it->prev; free(tmp); } while (0);

entry_t *next(entry_iter_t *it)
{
	if (!it->obj) return NULL;

	if (it->obj->type == mt_file) {
		entry_t *tmp = it->obj;
		if (it->prev)
			get_prev(it)
		else 
			it->obj = NULL;
			
		return tmp;
	}
	// obj is a dir

	if (it->dir_idx == -1) {
		it->dir_idx = 0;
		return it->obj;
	}

	if (it->dir_idx >= it->obj->data.dir.inner_count) {
		if (it->prev) {
			get_prev(it);
			return next(it);
		} else
			return NULL;
	}

	entry_t *tmp = &it->obj->data.dir.inners[it->dir_idx++];

	entry_iter_t *curr = malloc(sizeof(entry_iter_t));
	memcpy(curr, it, sizeof(entry_iter_t));
	*it = (entry_iter_t) {
		.obj = tmp,
		.dir_idx = -1,
		.prev = curr,
	};

	return next(it);
}

entry_iter_t *entry_iter_begin(entry_t *obj)
{
	entry_iter_t *it = malloc(sizeof(entry_iter_t));
	*it = (entry_iter_t) {
		.obj = obj,
		.dir_idx = -1,
		.prev = NULL,
	};

	return it;
}
