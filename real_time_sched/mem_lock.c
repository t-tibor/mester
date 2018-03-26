#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h> //lock functions
#include <pthread.h>	// pthread functions
#include <unistd.h>		// sleep
#include <limits.h>

/*
mlockall(...) : lock all memory pages of the proces
mlock(addr,length): lock specific memory regions
*/


pthread_t rt_thread;


void *rt_func(void *data)
{
	sleep(2);
	printf("Hello from rt thread\n");
	return NULL;
}


static void create_rt_thread(void)
{
	pthread_attr_t attr;
	struct sched_param param;
	
	if(pthread_attr_init(&attr))
	{
		perror("Cannot initialize attr structure");
		exit(1);
	}

	printf("Minimum stack size: %d bytes\n",PTHREAD_STACK_MIN);

	if(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 1024))
		exit(2);

	if(pthread_attr_setschedpolicy(&attr,SCHED_FIFO))
		exit(3);

	param.sched_priority = 80;

	if(pthread_attr_setschedparam(&attr, &param))
		exit(4);

	pthread_create(&rt_thread, &attr, rt_func, NULL);
}


int main()
{
	void *retval;

	create_rt_thread();
	// Lock all current and future memory pages
	if(mlockall(MCL_CURRENT | MCL_FUTURE)){
		perror("mlockall failed\n");
	}

	pthread_join(rt_thread,&retval);
	return 0;
}