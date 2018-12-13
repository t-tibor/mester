#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sched.h>
#include <inttypes.h>
#include <signal.h>

#include "gpio_uapi.h"


#define USE_YIELD	1


 #define gettid() syscall(__NR_gettid)

 #define SCHED_DEADLINE	6

 /* XXX use the proper syscall numbers */
 #ifdef __x86_64__
 #define __NR_sched_setattr		314
 #define __NR_sched_getattr		315
 #endif

 #ifdef __i386__
 #define __NR_sched_setattr		351
 #define __NR_sched_getattr		352
 #endif

 #ifdef __arm__
 #define __NR_sched_setattr		380
 #define __NR_sched_getattr		381
 #endif

 struct sched_attr {
	__u32 size;

	__u32 sched_policy;
	__u64 sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	__s32 sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	__u32 sched_priority;

	/* SCHED_DEADLINE (nsec) */
	__u64 sched_runtime;
	__u64 sched_deadline;
	__u64 sched_period;
 };

 int sched_setattr(pid_t pid,
		  const struct sched_attr *attr,
		  unsigned int flags)
 {
	return syscall(__NR_sched_setattr, pid, attr, flags);
 }

 int sched_getattr(pid_t pid,
		  struct sched_attr *attr,
		  unsigned int size,
		  unsigned int flags)
 {
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
 }



// Timespec helper functions

// return:
// 		1, if a < limit
// 		0, else
int timespec_before(struct timespec a, struct timespec limit)
{
	if(a.tv_sec < limit.tv_sec) return 1;
	if(a.tv_sec > limit.tv_nsec) return 0;
	if(a.tv_nsec < limit.tv_nsec) return 1;
	return 0;
}

struct timespec timespec_add(struct timespec a, uint32_t delta_ns)
{
	a.tv_nsec += delta_ns;
	if(a.tv_nsec >= 1000000000LL) 
	{
		a.tv_sec++;
		a.tv_nsec -= 1000000000ULL;
	}
	return a;
}



static volatile int done;


int period_ms;
int offset_ms;
int runtime_ms;
int computation_time_ms;
#define SCHED_F		0
#define SCHED_R 	1
#define SCHED_D 	2
int sched;
int prio;

 void run_deadline()
 {
	struct sched_attr attr;
	int ret;
	struct timespec now;
	int64_t prev;
	int64_t tmp;
	int64_t delta;



// setup scheduling class
	memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	attr.sched_policy = SCHED_DEADLINE;
	attr.sched_runtime =  ((uint64_t)runtime_ms)*1000*1000;
	attr.sched_period = attr.sched_deadline = ((uint64_t)period_ms) * 1000 *1000;


	ret = sched_setattr(0, &attr, 0);
	if (ret < 0) {
		done = 0;
		perror("Cannot setup scheduling policy");
		exit(-1);
	}

	// wait for the first whole second
	clock_gettime(CLOCK_REALTIME, &now);
	now.tv_sec += 2;
	now.tv_nsec = 0;
	clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &now, NULL);

	prev=0;
	while (!done) 
	{

		// do busy work for computation time parameter length
		clock_gettime(CLOCK_REALTIME, &now);
		tmp = now.tv_sec*1000000000LL + now.tv_nsec;
		delta = tmp - prev;
		
		if(delta > 10000000)
		{
			printf("Wake time: %usec %unsec.\n",now.tv_sec, now.tv_nsec);
		}
		prev = tmp;
	}
 }



void sigint_handler(int sig)
{
	(void)sig;
	printf("Exiting...\n");
	done = 1;
}

void sigxcpu_handler(int sig)
{
	(void)sig;
	printf("Runtime overrun detected.\n");
}

const char *help_message = 	"Usage:\n" 																		\
							"\t-p: period of the task [ms]\n"												\
							"\t-o: runtime offset inside the period [ms]. Must be less than period.\n"		\
							"\t-c: computation time used by the demo program [ms]\n"						\
							"\t-s: scheduler to use (SCHED_FIFO: f , SCHED_RR: r, SCHED_DEADLINE: d).\n"	\
							"\t-P: scheduling priority (for SCHED_FIFO and SCHED_RR)\n"						\
							"\t-r: runtime [ms] (for SCHED_DEADLINE)\n";

 int main (int argc, char **argv)
 {
 	int arg;
 	char *c = NULL;
 	// set default values
 	period_ms = 1000;
 	offset_ms = 10;
 	runtime_ms = 7;
 	computation_time_ms = 5;
 	sched = SCHED_D;
 	prio = 80;

 	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "p:o:r:c:s:P:h")))
		{
			switch(arg)
			{
			case 'p' : period_ms = atoi(optarg); break;
			case 'o' : offset_ms = atoi(optarg); break;
			case 'r' : runtime_ms = atoi(optarg); break;
			case 'c' : computation_time_ms = atoi(optarg); break;
			case 's' : c = optarg; break;
			case 'P' : prio = atoi(optarg); break;
			default  : printf("%s",help_message);
					   return 0;
			}
		}
	}
	// input check
	if(period_ms <=0)
	{
		fprintf(stderr,"Period must be at least 1 msec.\n");
		return -1;
	}
	if(abs(offset_ms) >= period_ms)
	{
		fprintf(stderr,"Offset must be smaller than period.\n");
		return -1;
	}
	if(runtime_ms <=0)
	{
		fprintf(stderr,"Runtime must be at least 1 msec.\n");
		return -1;
	}
	if((computation_time_ms <=0) || (computation_time_ms >= period_ms))
	{
		fprintf(stderr,"Computation time must be at least 1 msec and less than the period.\n");
		return -1;
	}
	if(c)
	{
		switch(*c)
		{
			case 'f' : sched = SCHED_F;
						break;
			case 'r' : sched = SCHED_R;
						break;
			case 'd' : sched = SCHED_D;
						break;
			default  : fprintf(stderr,"Unknown scheduler: %c. (Valid are: f,r,d)\n",*c);
						return -1;
		}
	}
	if(prio <= 0 || prio > 99)
	{
		fprintf(stderr,"Invalid prio number: %d.\n",prio);
		return -1;
	}

	signal(SIGINT,sigint_handler);
	signal(SIGXCPU, sigxcpu_handler);

	printf("SCHED_DEADLINE demo started.\n");

	run_deadline();

	printf("SCHED_DEADLINE demo exited.\n");
	return 0;
 }
