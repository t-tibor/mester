#include "timekeeper.h"

struct timekeeper* timekeeper_create()
{
	struct timekeeper *ret;
	ret = (struct timekeeper*)malloc(sizeof(struct timekeeper));
	if(!ret)
		return NULL;
	ret->head = 0;
	ret->tail = 0;
	ret->sync_pint_buffer_size = TIMEKEEPER_SYNC_POINT_COUNT;
}

int timekeeper_add_sync_point(uint64_t local_ts, uint64_t global_ts)
{
	
}

int timekeeper_convert(uint64_t *local_ts, int ts_count);