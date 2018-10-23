#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "icap_channel.h"



void signal_handler(int sig)
{
	printf("Exiting...\n");
	(void)sig;
}



struct timer_setup_t setup = 
{
	.dmtimer4_mode = NONE,
	.dmtimer6_mode = NONE,
	.dmtimer7_mode = PWM,

	.dmtimer4_cpts_hwts_en = 0,
	.dmtimer6_cpts_hwts_en = 0,
	.dmtimer7_cpts_hwts_en = 1,
	
	.ecap0_mode = ICAP,
	.ecap2_mode = ICAP
};

int main(int argc, char **argv)
{
	int verbose = 0;
	int arg;
	int logger_verbose = 0;
	int use_icap = 0;

		/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "vwli")))
		{
			switch(arg)
			{
			case 'i' : use_icap = 1; break;
			case 'v' : verbose = 1; break;
			case 'w' : verbose = 2; break;
			case 'l' : logger_verbose = 1; break;
			default : printf("Usage: \n\t-i: enable external event timestamping on eCAP0 (P9_42) and eCAP2 (P9_28)\n\t-v: PPS verbose mode (print detected error only)\n\t-w: PPS full verbose mode (print most of the servo parameters)\n\t-l: print timestamps of the captured events to stdout. \n\n");
						return 0;
			}
		}
	}


	 signal(SIGINT, signal_handler);

	if(init_rtio(setup))
	{
		fprintf(stderr,"Cannot start rtio.\n");
		return -1;
	}

	printf("Input pins for the timers:\n");
	printf("\tDMTimer4: P8_07\n");
	printf("\tDMTimer5: P8_09\n");
	printf("\tDMTimer6: P8_10\n");
	printf("\tDMTimer7: P8_08\n");
	printf("\teCAP0: P9_42\n");
	printf("\teCAP2: P9_28\n\n");

	
	for(int i=0;i<5;i++)
	{
		printf("Wait for the timekeeper to stabilize:");
		for(int j=0;j<i;j++)
			printf(".");
		printf("\n");
		sleep(1);
	}
	printf("\n");

	// start pps generation using the CPTS as feedback
	start_pps_generator(-7, 7,verbose);
	// start icap event logger
	if(use_icap)
	{
		start_icap_logging(0, logger_verbose);
		start_icap_logging(2, logger_verbose);
	}

	pause();

	printf("Exiting\n");

	close_rtio();
return 0;
}