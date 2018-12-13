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
	
	.ecap0_mode = ICAP,
	.ecap2_mode = ICAP
};

int main()
{
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

	start_pps_generator(5, 7,0);
	start_icap_logging(0,1);
	start_icap_logging(2,1);	


	pause();

	printf("Exiting\n");

	close_rtio();
return 0;
}