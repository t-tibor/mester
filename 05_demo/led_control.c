#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>


#include "gpio_uapi.h"



//****************** PERIODIC TASK TO RUN ******************

#define SYSTEM_FREQUENCY    25
#define LED_DUTY            10 // %

#define LED_ON_TIME_US      (1.0/SYSTEM_FREQUENCY*LED_DUTY/100*1e6)

struct rt_task_data_t
{
	struct timespec wake_time;
	int interval_ns;
	int cycle_count;	
	volatile int running;
};

// runs on normal priority
int init_rt_task(struct rt_task_data_t *data)
{
    (void)data;
	return gpio_init();
}

// runs on real time priority
void do_rt_task(struct rt_task_data_t* data)
{
    (void)data;
	gpio_set();
    usleep(LED_ON_TIME_US);
    gpio_clear();
}

// runs on normal priority
int clear_rt_task(struct rt_task_data_t *data)
{
    (void)data;
	return gpio_deinit();
}


//****************** TASK RUNNERS ******************
void *periodic_task_nanosleep(void *arg)
{
	struct rt_task_data_t *data = (struct rt_task_data_t*)arg;


	while(data->running && (data->cycle_count==-1 || (data->cycle_count-- > 0)))
	{
	    if(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &data->wake_time, NULL))
		{
			return NULL;
		}
		do_rt_task(data);

		// calculate next wake time
		data->wake_time.tv_nsec += data->interval_ns;
		while(data->wake_time.tv_nsec >= 1000000000)
		{
			data->wake_time.tv_sec++;
			data->wake_time.tv_nsec -= 1000000000;
		}
	}
	return NULL;
}

struct itimerspec its;
struct sigevent sev;
timer_t timerid;
sigset_t set;

int posix_init(struct rt_task_data_t *data)
{

	sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
    {
        perror("Error: timer_create");
        return -1;
    }

    its.it_value.tv_sec = data->wake_time.tv_sec;
    its.it_value.tv_nsec = data->wake_time.tv_nsec;
    its.it_interval.tv_sec = data->interval_ns / 1000000000;
    its.it_interval.tv_nsec = data->interval_ns % 1000000000;

    sigemptyset(&set);
    if(sigaddset(&set, SIGRTMIN) == -1) 
    {
	    perror("Sigaddset error");                                                                                                     
	    return -1;
	} 
	if(sigprocmask(SIG_BLOCK, &set, NULL))
	{
		perror("Sigmask error");
		return -1;
	}

	if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) == -1)
	{
		perror("timer_settime");
		return -1;
	}
	return 0;
}

void *periodic_task_posix(void *arg)
{
	struct rt_task_data_t *data = (struct rt_task_data_t*)arg;
    int sig;

    while(data->running && (data->cycle_count==-1 || (data->cycle_count-- > 0)))
	{
	    sigwait(&set,&sig);
	  
		do_rt_task(data);
	}
	return NULL;
}





//****************** INPUT PARSER AND INITIALIZERS ******************
int posix;
int interval_ns;
int prio_level;
int cycle_count;
struct timespec start_time;
struct rt_task_data_t rt_task_data;

int parse_argv(int argc, char *argv[])
{
	int arg;
	struct timespec curr_time;
	int interval_ms;
    int length_sec;

	// default values
	posix = 0;
	interval_ms = (1000/SYSTEM_FREQUENCY);
	prio_level = 80;
	length_sec = -1;

	/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "Pi:p:l:")))
		{
			switch(arg)
			{
			case 'P' : posix=1; break;
			case 'i' : interval_ms = atoi(optarg); break;
			case 'p' : prio_level = atoi(optarg); break;
			case 'l' : length_sec = atoi(optarg); break;
			default : printf("Usage: \n\t-P Use POSIX interval timer instead of clock_nanosleep \n\t-i Wake interval [ms] \n\t -p Real time priority [1-99]\n\t-l Test length [sec]\n\n");
						return 0;
			}
		}
	}

	// check input parameter values
	if(interval_ms <= 0)
	{
		fprintf(stderr,"Invalid interval (it must be greater than 0.");
		return -1;
	}
	interval_ns = interval_ms*1000000;


	if(length_sec <= 0)
	{
		// inifinite loop
		cycle_count = -1;
	}
	else
	{
		cycle_count = length_sec*1000/interval_ms;
	}



	if(prio_level <=0 || prio_level > 99)
	{
		fprintf(stderr,"Invalid priority level: %d.\n Valid range: [1-99]",prio_level);
		return -1;
	}

	// calculate first wake time
	clock_gettime(CLOCK_REALTIME, &curr_time);
    start_time.tv_sec = curr_time.tv_sec +2;
    start_time.tv_nsec = 0;

	return 0;
}

void signal_handler(int sig)
{
	printf("Exiting...\n");
	(void)sig;
}


int main(int argc, char* argv[])
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

 	
 	if(parse_argv(argc,argv))
 		return -1;


    /* Initialize pthread attributes (default values) */
    ret = pthread_attr_init(&attr);
    if (ret) 
    {
            printf("init pthread attributes failed\n");
            goto out;
    }

    /* Set a specific stack size  */
    ret = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    if (ret) 
    {
        printf("pthread setstacksize failed\n");
        goto out;
    }

    /* Set scheduler policy and priority of pthread */
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret) 
    {
            printf("pthread setschedpolicy failed\n");
            goto out;
    }
    param.sched_priority = prio_level;
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret) 
    {
            printf("pthread setschedparam failed\n");
            goto out;
    }
    /* Use scheduling parameters of attr */
    ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    if (ret) 
    {
            printf("pthread setinheritsched failed\n");
            goto out;
    }


    rt_task_data.interval_ns = interval_ns;
    rt_task_data.wake_time = start_time;
    rt_task_data.cycle_count = cycle_count;
    rt_task_data.running = 1;
    if(init_rt_task(&rt_task_data))
    {
    	fprintf(stderr,"Cannot init rt task.\n");
    	ret = -1;
    	goto out;
    }

    /* Lock memory */
    if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) 
    {
		clear_rt_task(&rt_task_data);
        fprintf(stderr,"mlockall failed: %m\n");
        ret = -2;
        goto out;
    }

    /* Create a pthread with specified attributes */
    if(posix)
    {
    	ret = posix_init(&rt_task_data);
    	if(!ret)
    		ret = pthread_create(&thread, &attr, periodic_task_posix, (void*)&rt_task_data);
    }
    else
    {
    	ret = pthread_create(&thread, &attr, periodic_task_nanosleep, (void*)&rt_task_data);
    }
    if (ret) 
    {
    		clear_rt_task(&rt_task_data);
            printf("create pthread failed\n");
            goto out;
    }

    signal(SIGINT, signal_handler);
    pause();

    // singal the threads to exit
    rt_task_data.running = 0;

    /* Join the thread and wait until it is done */
    ret = pthread_join(thread, NULL);
    if (ret)
    {
            printf("join pthread failed: %m\n");
            goto out;
    }

    clear_rt_task(&rt_task_data);
 
out:
        return ret;
}