#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>


#include "icap_channel.h"



volatile int end;

void signal_handler(int sig)
{
	printf("Exiting...\n");
	(void)sig;
	end = 1;
}



struct timer_setup_t setup = 
{
	.dmtimer4_mode = NONE,
	.dmtimer6_mode = NONE,
	.dmtimer7_mode = PWM,
	
	.ecap0_mode = NONE,
	.ecap2_mode = NONE
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
	printf("\tDMTimer7: P8_08\n\n");


	printf("Recording events on dmtimer 5 channel.\n");
	start_icap_logging(5,1);

	while(!end)
		sleep(2);

	close_rtio();
return 0;
}