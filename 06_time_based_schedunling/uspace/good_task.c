#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>



#define SCHED_GT_PATH 	"/dev/sched_gt"

#define TIMER_IOCTL_MAGIC	'@'
#define IOCTL_SET_OFFSET			_IO(TIMER_IOCTL_MAGIC,1)
#define IOCTL_SET_LENGTH 			_IO(TIMER_IOCTL_MAGIC,2)
#define IOCTL_WAIT_NEXT 			_IO(TIMER_IOCTL_MAGIC,3)

void busy_wait(uint32_t delay)
{
	struct timespec start,now;
	uint32_t delta;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID,&start);

	do
	{
		clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now);
		delta = (now.tv_sec - start.tv_sec)*1000000ULL;
		delta += now.tv_nsec/1000;
		delta -= start.tv_nsec/1000;
	}while(delta < delay);
}

int main(int argc, char**argv)
{
	(void)argc;
	(void)argv;
	int fid;
	int ret;
	int i;
	struct timespec ts;

	printf("Running SCHED GT test.\n");
	fid = open(SCHED_GT_PATH,O_RDWR);
	if(fid < 0)
	{
		perror("Cannot open sched gt");
		return -1;
	}	

	// set offset to 5 msec
	ret = ioctl(fid, IOCTL_SET_OFFSET,7);
	if(ret)
	{
		fprintf(stderr,"Cannot set offset.\n");
		return -2;
	}
	printf("Offset setting OK.\n");
	ret = ioctl(fid, IOCTL_SET_LENGTH,2);
	if(ret)
	{
		fprintf(stderr,"Cannot set length.\n");
		return -3;
	}
	printf("Length setting OK.\n");
	ioctl(fid, IOCTL_WAIT_NEXT);
	clock_gettime(CLOCK_REALTIME,&ts);
	printf("Waiting survived. :) \n");
	printf("First wake: %ldsec,%ldnsec\n",ts.tv_sec, ts.tv_nsec);


	for(i=0;i<500;i++)
	{
		(void)ioctl(fid, IOCTL_WAIT_NEXT);
		clock_gettime(CLOCK_REALTIME,&ts);
		if(i%50 == 0)
		{
			printf("32 waiting is OK.\n");
			printf("Wake: %ldsec,%ldnsec\n",ts.tv_sec, ts.tv_nsec);
		}

		busy_wait(1000);
	}

	close(fid);

	printf("Good task finished.\n");

	return 0;
}