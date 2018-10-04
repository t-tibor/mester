
#include <pthread.h>
#include <unistd.h>

#include "utils.h"



int goto_rt_level(int level)
{
	struct sched_param param;
	int policy;


	if(level < 1 || level > 99)
		return -1;

	policy = SCHED_FIFO;
    param.sched_priority = level;

   	return pthread_setschedparam(pthread_self(), policy, &param);
}

int leave_rt_level()
{
	struct sched_param param;
	int policy;

	policy = SCHED_OTHER;
    param.sched_priority = 0;

   	return pthread_setschedparam(pthread_self(), policy, &param);
}