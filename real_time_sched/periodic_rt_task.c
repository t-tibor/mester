#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "gpio_uapi.h"

#define SCHED_REALTIME 	0
#define RT_PRIO			50

timer_t timerid;
struct itimerspec its;


/**************************************************************************************************************

	Function definitions

***************************************************************************************************************/

struct timespec ts_sub(struct timespec after, struct timespec before)
{
	struct timespec res;
	res.tv_sec = after.tv_sec - before.tv_sec;
	res.tv_nsec = after.tv_nsec - before.tv_nsec;

	while(res.tv_nsec<0)
	{
		res.tv_nsec += 1000000000;
		res.tv_sec--;
	}
	return res;
}

static void sig_handler(int sig)
{
	struct timespec time, diff;
	struct tm *date;
	unsigned long tmp;
	int msec, usec, nsec;

	gpio_toggle();

	clock_gettime(CLOCK_REALTIME,&time);
	date = localtime(&time.tv_sec);
	tmp = (time.tv_nsec);
	nsec = tmp % 1000;
	tmp/=1000;
	usec = tmp%1000;
	tmp /= 1000;
	msec = tmp;

   	printf("#Wake time: %d hours, %d mins, %d secs, %d msecs, %d usecs, %d nsecs \n", date->tm_hour, date->tm_min, date->tm_sec, msec, usec, nsec );

   	//calculate scheduling time difference
   	diff = ts_sub(time, its.it_value);
   	printf("%ld;%ld\n",diff.tv_sec, diff.tv_nsec);

   	// restart the timer
   	its.it_value.tv_nsec += 1000000;
   	if(its.it_value.tv_nsec >= 1000000000)
   	{
   		its.it_value.tv_nsec = 0;
   		its.it_value.tv_sec++;
   	}

	timer_settime(timerid, TIMER_ABSTIME, &its, NULL);
}

void sched_setup()
{
	struct sched_param param;
	
	printf("Switching scheduling class to SCHED_FIFO\n");
    param.sched_priority = RT_PRIO;	// 1..99(highest) : sched_get_priority_max(SCHED_FIFO)
    if(0 != sched_setscheduler(0, SCHED_FIFO, &param))
    {
    	perror("Error: SetScheduler");
    }
}

typedef int option_t;

int main(int argc, char**argv)
{
	struct sigevent sev;
	struct timespec current_time;
	option_t rt_mode = 0;
	int arg;
	int rep = 0;

	int i=0;

	gpio_init();

	/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "rn:")))
		{
			switch(arg)
			{
			case 'r' : rt_mode=1; break;
			case 'n' : rep = atoi(optarg); break;
			default : printf("Usage: \n\toptional -r for real time scheduling\n\n");
						return 0;
			}
		}
	}
	// setup real time scheduling	
	if(rt_mode==1) sched_setup();

	// setup signal handling
	signal(SIGRTMIN, sig_handler);

	// setup posix interval timer
		// declare signal event to use at the timeout
	sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
        perror("Error: timer_create");

    // setup timer period
    clock_gettime(CLOCK_REALTIME, &current_time);

    its.it_value.tv_sec = current_time.tv_sec +2;
    its.it_value.tv_nsec = 1000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) == -1)
         perror("timer_settime");

	while(i<rep)
	{
	    sleep(3);
	    i++;
	}

	gpio_deinit();
	return 0;
}