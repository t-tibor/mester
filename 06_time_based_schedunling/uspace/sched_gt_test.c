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

#define SCHED_GT_IOCTL_MAGIC	'@'
#define IOCTL_SET_OFFSET			_IO(SCHED_GT_IOCTL_MAGIC,1)
#define IOCTL_SET_LENGTH 			_IO(SCHED_GT_IOCTL_MAGIC,2)
#define IOCTL_SET_FREQUENCY 		_IO(SCHED_GT_IOCTL_MAGIC,3)
#define IOCTL_ENTER_SCHED_GT 		_IO(SCHED_GT_IOCTL_MAGIC,4)
#define IOCTL_LEAVE_SCHED_GT 		_IO(SCHED_GT_IOCTL_MAGIC,5)
#define IOCTL_WAIT_NEXT 			_IO(SCHED_GT_IOCTL_MAGIC,6)


void busy_wait(uint32_t delay_us)
{
	struct timespec start,now;
	uint32_t delta_us;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID,&start);

	do
	{
		clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now);
		delta_us = (now.tv_sec - start.tv_sec)*1000000ULL;
		delta_us += now.tv_nsec/1000;
		delta_us -= start.tv_nsec/1000;
	}while(delta_us < delay_us);
}


int main(int argc, char**argv)
{
	int arg;
	unsigned frequency = 1;
	unsigned offset = 3;
	unsigned alloc = 2;
	unsigned comp = 1000;

	int fid;
	int ret;
	int i;
	struct timespec ts;


	/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "o:a:c:f:")))
		{
			switch(arg)
			{
			case 'f' : frequency 	= atoi(optarg); break;
			case 'o' : offset 		= atoi(optarg); break;
			case 'a' : alloc 		= atoi(optarg); break;
			case 'c' : comp 		= atoi(optarg); break;
			default : printf("Usage: \n\t-f: Task cycles per system period.\n\t-o: Set task offset[ms]\n\t-a: Allocated bandwidth.\n\t-c: Computation time [usec].\n\n");
						return 0;
			}
		}
	}

	if(offset >= 20 || alloc >= 20 ||  offset + alloc >= 20)
	{
		fprintf(stderr,"System period is 20 ms. offset + length should be less than that.\n");
		return -1;
	}
	if(comp >= 20000)
	{
		fprintf(stderr,"Too big computation time: %u. Use one bellow 20000 usec.\n",comp);
		return -1;
	}
	if(frequency < 1 || frequency > 20)
	{
		fprintf(stderr,"Frequency should be between 1 and 20.\n");
		return -1;
	}

	printf("Running SCHED GT test.\n");
	fid = open(SCHED_GT_PATH,O_RDWR);
	if(fid < 0)
	{
		perror("Cannot open sched gt");
		return -1;
	}	

	// set offset to 5 msec
	ret = ioctl(fid, IOCTL_SET_FREQUENCY,frequency);
	if(ret)
	{
		fprintf(stderr,"Cannot set frequency.\n");
		return -2;
	}
	printf("Frequency setting OK.\n");
	ret = ioctl(fid, IOCTL_SET_OFFSET,offset);
	if(ret)
	{
		fprintf(stderr,"Cannot set offset.\n");
		return -2;
	}
	printf("Offset setting OK.\n");
	ret = ioctl(fid, IOCTL_SET_LENGTH,alloc);
	if(ret)
	{
		fprintf(stderr,"Cannot set length.\n");
		return -3;
	}
	printf("Length setting OK.\n");

	ret = ioctl(fid, IOCTL_ENTER_SCHED_GT);
	if(ret)
	{
		fprintf(stderr,"Cannot enter sched GT\n");
		return -1;
	}
	printf("Entered to SCHED GT.\n");
	printf("Waiting for the next cycle.\n");
	ret = ioctl(fid, IOCTL_WAIT_NEXT);
	clock_gettime(CLOCK_REALTIME,&ts);
	if(ret)
	{
		fprintf(stderr,"Error during waiting for the next cycle.\n");
		return -1;
	}
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
		busy_wait(comp);
	}

	printf("SCHED GT TEST finished.\nLeavgin SCHED GT.\n");
	ret = ioctl(fid, IOCTL_LEAVE_SCHED_GT);
	if(ret)
	{
		fprintf(stderr,"Cannot leave sched GT\n");
	}
	close(fid);

	return 0;
}