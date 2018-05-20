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
#include <signal.h>
#include <sys/wait.h>

#include "gpio_uapi.h"

#define SCHED_REALTIME 	0
#define RT_PRIO			81
#define STACK_EXTRA_PAGES_TO_LOCK	2

timer_t timerid;
struct itimerspec its;


/**************************************************************************************************************

	Function definitions

***************************************************************************************************************/

static void heartbeat()
{
	struct timespec time;
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
   	printf("%ld\n",time.tv_nsec - its.it_value.tv_nsec);
   	fflush(stdout);
}

void sched_setup()
{
	// dummy array for stack expanding
	volatile uint8_t dummy_array[STACK_EXTRA_PAGES_TO_LOCK * 4096];
	struct sched_param param;
	int i;

	for(i=0;i<sizeof(dummy_array);i++)
		dummy_array[i] = i;
	
	printf("Switching scheduling class to SCHED_FIFO\n");
    param.sched_priority = RT_PRIO;	// 1..99(highest) : sched_get_priority_max(SCHED_FIFO)
    if(0 != sched_setscheduler(0, SCHED_FIFO, &param))
    {
    	perror("Error: SetScheduler");
    }

    printf("Pinning memory pages.");
	if(mlockall(MCL_CURRENT | MCL_FUTURE)){
		perror("Cannot pin memory pages to frames.\n");
		exit(1);
	} 
}

void dummy_work()
{
	volatile double n1,n2;
	int i;
	n1 = 0.15684;
	n2 = 0.9993322;
	for(i=0;i<100;i++)
	{
		n1 *= n2;
		n2 *= n1;
	}
}
typedef int option_t;

void run_test_rest(int proc_num, int rep_num, int offset, int spread)
{
	timer_t timerid;
	struct itimerspec its;
	struct sigevent sev;
	struct timespec current_time;
	sigset_t set;
	long unsigned wake_ns;
	int i=0;
	int sig;

	if(proc_num %2 == 0)
   		wake_ns = offset*1000000 - (spread==1 ? 100000 : 0);
   	else 
   		wake_ns = offset*1000000 + (spread==1 ? 100000 : 0);


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
   	its.it_value.tv_nsec = wake_ns;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;


    sigemptyset(&set);
    if(sigaddset(&set, SIGRTMIN) == -1) {                                           
	    perror("Sigaddset error");                                                                                                     
	 } 

 	if(sigprocmask(SIG_BLOCK, &set, NULL))
		perror("Sigmask error");

    while(i<rep_num)
	{
		 if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) == -1)
         perror("timer_settime");

	    sigwait(&set,&sig);
		 //printf("Dummy waken\n");
	    // we are wakened by the signal
	    // perform some computation for 10 ms
	    do
	    {
	    	dummy_work();
	    	clock_gettime(CLOCK_REALTIME, &current_time);
	    }while(current_time.tv_nsec < wake_ns+10000000UL);

	    // forward the timer
		its.it_value.tv_sec++;
	    i++;
	}
}

/**
* Function for the main process whose execution is tracked by the GPIO signal and the logging.
*/
void run_test_main(int rt_mode, int rep_num, int offset)
{
	struct sigevent sev;
	struct timespec current_time;
	long unsigned wake_ns = offset*1000000;
	int i=0;
	int sig;
	sigset_t set;

	gpio_init();
	//signal(SIGRTMIN, sig_handler);
	// setup real time scheduling	
	if(rt_mode==1) sched_setup();


	// setup posix interval timer
	// declare signal event to use at the timeout
	sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
        perror("Error: timer_create");

    //printf("Start main process with %d repetition.\n",rep_num);
    // setup timer period
    clock_gettime(CLOCK_REALTIME, &current_time);
    its.it_value.tv_sec = current_time.tv_sec +2;
    its.it_value.tv_nsec = wake_ns;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    sigemptyset(&set);
    if(sigaddset(&set, SIGRTMIN) == -1) {                                           
	    perror("Sigaddset error");                                                                                                     
	 } 
	if(sigprocmask(SIG_BLOCK, &set, NULL))
		perror("Sigmask error");


	 i=0;
    while(i<rep_num)
	{
		 if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) == -1)
         perror("timer_settime");

	    sigwait(&set,&sig);
	    // we are wakened by the signal, handle it
		heartbeat();

	    // perform some computation for 10 ms
	    do
	    {
	    	dummy_work();
	    	clock_gettime(CLOCK_REALTIME, &current_time);
	    }while(current_time.tv_nsec < wake_ns+10000000UL);

	    // forward the timer
		its.it_value.tv_sec++;
	    i++;
	}
		gpio_deinit();
}


int main(int argc, char**argv)
{

	option_t rt_mode = 0;
	int arg;
	int rep = 0;
	int i;
	int child_num = 0;
	int offset = 0;
	int spreading = 0;
	int stat;
	int ch_pid;
	/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "rt:N:O:s")))
		{
			switch(arg)
			{
			case 'r' : rt_mode=1; break;
			case 't' : rep = atoi(optarg); break;
			case 'N' : child_num = atoi(optarg); break;
			case 'O' : offset = atoi(optarg); break;
			case 's' : spreading =1; break;
			default : printf("Usage: \n\t-t timeout \n\t-r real time scheduling \n\t -N process number\n\t-O offset in ms\n\t-s spread the wake times\n\n");
						return 0;
			}
		}
	}

	// start the test processes
	for(i = 0;i<child_num;i++)
	{
		ch_pid = fork();
		if(ch_pid == 0)
		{
			//printf ("Running child %d.\n",i);
			// i am a child
			if(i == 0)
				run_test_main(rt_mode, rep, offset);
			else
				run_test_rest(i,rep, offset, spreading);
			_exit(0);
		}
		else if(ch_pid < 0)
		{
			fprintf(stderr,"Forking error.\n");
			exit(-1);
		}
	}

	//wait until all children exit
	for(i = 0;i<child_num;i++)
		waitpid(-1,&stat,0);
	
	return 0;
}
