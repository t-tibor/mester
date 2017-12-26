#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>	// Defines signal-handling functions (i.e. trap Ctrl-C)
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <time.h>

//#include <linux/ptp_clock.h>
#include "ptp_clock.h"
#include "BBonePPS.h"

//#define DEBUG_1

/* Global variables */
static uint8_t keepgoing = 1; // Set to 0 when Ctrl-c is pressed
static uint8_t sleepT	= 2;
static int8_t step		= 1;
static int8_t first_run	= 1;
static int8_t cPhase	= 1;
int32_t error_now	= 0;
int32_t error_prev	= 0;
int32_t error_prev2	= 0;
int32_t deviation	= 0;
int32_t deltadev	= 0;
float fpDeviation	= 0;


/* Callback called when SIGINT is sent to the process (Ctrl-C) */
void signal_handler(int sig)
{
#ifdef DEBUG_1
	printf( "\nCtrl-C pressed (%d), cleaning up and exiting...\n", sig);
#endif
	keepgoing = 0;
}

void errorCalc(uint32_t nsec);
void adjust_pps_clock(int32_t *dev_int, int32_t *dev_prop, float *fpDevI, float *fpDevP);
int8_t phase_determine();
int32_t deltadev_limit(int32_t dev);
int32_t deviation_limit(int32_t dev);
uint32_t tsDiffCalc(uint32_t tsfirst, uint32_t tssecond);



int main(int argc, char *argv[])
{
    volatile void *timer5_addr;
    volatile void *cm__addr;
    volatile void *cpsw_addr;
	volatile void *gpio2_addr;

    volatile uint32_t *cm_per_timer5;
    volatile uint32_t *clksel_timer5;
    //volatile uint32_t *cpts_rft_clksel;
    volatile uint32_t *timer_ctrl;
    volatile uint32_t *timer_counter;
    volatile uint32_t *timer_load;
    volatile uint32_t *timer_match;
	volatile uint32_t *gpio_out_dis;

	struct sched_param param;
	struct ptp_extts_event extts_event;
	struct ptp_extts_request extts_request;

	int fd;
	int fd_ptp;
	int arg, verbose = 0;

	int32_t stepVal		= 0;
	int32_t cnt			= 0;
	uint32_t count		= 1;
	int32_t dev_prev	= 0;
	int32_t dev_prop	= 0;
	int32_t dev_int		= 0;
	float fpDevI		= 0;
	float fpDevP		= 0;

	/* Processing the command line arguments */
	if(argc > 1)
	{
		while(EOF != (arg = getopt(argc, argv, "v")))
		{
			switch(arg)
			{
			case 'v' : verbose=1; break;
			default : printf("Usage: BBonePPS [options]\n-v\t: Verbose, print messages to stdout\n\n");
						return 0;
			}
		}
	}


    /* Set the SCHEDULING POLICY and PRIORITY */
    param.sched_priority = 49;	// 1..99(highest) : sched_get_priority_max(SCHED_FIFO)
    if(0 != sched_setscheduler(0, SCHED_FIFO, &param))
    {
    	perror("SetScheduler");
        return -1;
    }

    /* Set the signal callback for Ctrl-C */
    signal(SIGINT, signal_handler);


    if((fd	= open("/dev/mem", O_RDWR | O_SYNC)) < 0)
	{
		perror("Opening /dev/mem");
		return -1;
	}
	if((fd_ptp = open(PTP_DEVICE, O_RDWR | O_SYNC)) < 0)
	{
	   fprintf(stderr, "Opening %s PTP device: %s\n", PTP_DEVICE, strerror(errno));
	   return -1;
	}


#ifdef DEBUG_1
    printf("Mapping CM: %X - %X (size: %X)\n", CM_START_ADDR, CM_END_ADDR, CM_SIZE);
    printf("Mapping TIMER5: %X - %X (size: %X)\n", DMTIMER5_START_ADDR, DMTIMER5_END_ADDR, DMTIMER5_SIZE);
    printf("Mapping CPTS: %X - %X (size: %X)\n", CPSW_START, CPSW_END, CPSW_SIZE);
    printf("Mapping GPIO2: %X - %X (size: %X)\n", GPIO2_START_ADDR, GPIO2_END_ADDR, GPIO2_SIZE);
#endif

    cm__addr = mmap(0, CM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CM_START_ADDR);
    if(cm__addr == MAP_FAILED)
    {
    	perror("Unable to map CM");
    	return -1;
    }

    timer5_addr = mmap(0, DMTIMER5_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DMTIMER5_START_ADDR);
    if(timer5_addr == MAP_FAILED)
    {
    	perror("Unable to map TIMER5");
    	return -1;
    }

    cpsw_addr = mmap(0, CPSW_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CPSW_START);
    if(cpsw_addr == MAP_FAILED)
    {
    	perror("Unable to map CPSW(CPTS)");
    	return -1;
    }

	gpio2_addr = mmap(0, GPIO2_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO2_START_ADDR);
	if(gpio2_addr == MAP_FAILED)
	{
		perror("Unable to map GPIO2");
		return -1;
	}

	close(fd);

    cm_per_timer5	= cm__addr + CM_PER_TIMER5_CLKCTRL;
    clksel_timer5	= cm__addr + CLKSEL_TIMER5_CLK;
    //cpts_rft_clksel = cm__addr + CM_CPTS_RFT_CLKSEL;
    timer_ctrl		= timer5_addr + DMTIMER_TCLR;
    timer_counter 	= timer5_addr + DMTIMER_TCRR;
    timer_load		= timer5_addr + DMTIMER_TLDR;
    timer_match 	= timer5_addr + DMTIMER_TMAR;
	gpio_out_dis	= gpio2_addr + GPIO_OE;


#ifdef DEBUG_1
    printf("CM mapped to %p\n", cm__addr);
    printf("DMTimer5 mapped to %p\n", timer5_addr);
    printf("CPTS mapped to %p\n", cpsw_addr);
    printf("GPIO2 mapped to %p\n", gpio2_addr)
#endif

/***************************************************************/

	/*
	 * It can be done through the sysfs file system's gpio files with standars
	 * read/write operations or using the libsoc 3rd-party library.
	 * Pinmux settings are determined by a custom DTO file.
	 */
	*gpio_out_dis = GPIO_69 | *gpio_out_dis; // Config. P8_09 as input

	*clksel_timer5 = 0x1; // Select CLK_M_OSC clock for DMTIMER5 (~23.761 MHz (24MHz): 42.085ns)
	*cm_per_timer5 = 0x2; // Enable DMTIMER5's clocks in CM_PER


   /*
    * DMTIMER5 settings
    *
    * bit12: 	1 - Toggle mode on PORTIMERPWM pin
    * bit11-10:	10 - Trigger on overflow and match
    * bit7:		0 - Clear PORTIMERPWM
    * bit6:		1 - Compare mode is enabled (using Match register)
    * bit5:		0 - Prescaler disabled
    * bit1:		1 - Auto reload timer
    * bit0:		0 - Stop timer
    */

	*timer_ctrl		= 0x1842;
   *timer_counter	= DMTIMER_1SEC;		// Initial count value (1s)
   *timer_load		= DMTIMER_1SEC;		// Set the re-load value (after overflow)
   *timer_match		= DMTIMER_DUTY10;	// Set the duty-cycle (~10%)

   *timer_ctrl = 0x1843; // Start the timer (bit0: 1)

#ifdef DEBUG_1
   printf("DMTIMER_TCLR:\t0x%08x\n",*timer_ctrl);
   printf("DMTIMER_TCRR:\t0x%08x\n",*timer_counter);
   printf("DMTIMER_TLDR:\t0x%08x\n",*timer_load);
   printf("DMTIMER_TMAR:\t0x%08x\n",*timer_match);

   /* Determine the DMTIMER5 timer's clock settings */
   printf("CM_PER_TIMER5_CLKCTRL:\t0x%08x \t0x2\n",*cm_per_timer5);
   printf("CLKSEL_TIMER5_CLK:\t0x%08x \t0x1\n",*clksel_timer5);

   /* Determine the CPTS timer's clock source */
   switch(*cpts_rft_clksel)
   {
   	   case 0 : printf("CPTS CLK SRC: CORE_CLKOUTM5 (250MHz)\n"); break;
   	   case 1 : printf("CPTS CLK SRC: CORE_CLKOUTM4 (200MHz)\n"); break;
   	   default: printf("Unable to determine the CPTS clock source\n");
   }
#endif

   /***************************************************************/

   /* Enable the external event timestamping */
   memset(&extts_request, 0, sizeof(extts_request));
   extts_request.index = HWTS_PUSH_INDEX;		// Which pin to get the timestamp from, index is 0 based
   extts_request.flags = PTP_ENABLE_FEATURE;	// PTP_RISING/FALLING_EDGE

   if(ioctl(fd_ptp, PTP_EXTTS_REQUEST, &extts_request))
   {
	   perror("PTP_EXTTS_REQUEST enable");
	   close(fd_ptp);
	   return -1;
   }


   /***************************************************************/
    while(keepgoing)
    {
    	/* Read out the new external TS event */
    	cnt = read(fd_ptp, &extts_event, sizeof(extts_event));
    	if(cnt != sizeof(extts_event))
    	{
    		perror("Ext. timestamp read");
    		break;
    	}

		if(step)
		{
			errorCalc(extts_event.t.nsec); // Can be positive or negative!

			if(abs(error_now) > STEP_MINERROR)
			{
				stepVal = (error_now/42);
				*timer_counter = (uint32_t)(*timer_counter + stepVal);

				if(verbose)
				{
					printf("Event at %lld.%09u\t err:%+03d\t step:%+03d\t\n",
							extts_event.t.sec,
							extts_event.t.nsec,
							error_now,
							stepVal);
			    	fflush(stdout);
				}

		    	continue; // Next while cycle
			}

			else { step = 0; } // < STEP_MINERROR

		} // IF_STEP


		count++;
		if(count%sleepT) { continue; }

		error_prev2	= error_prev;
		error_prev	= error_now;
		dev_prev	= deviation;
		errorCalc(extts_event.t.nsec);

		phase_determine(); // Determine the phase of the regulating process (clock servo)

		if (first_run)
		{
			first_run = 0;
			*gpio_out_dis = (~GPIO_69) & *gpio_out_dis; // Config. P8_09 as output
		}

		/* Clock servo */

		adjust_pps_clock(&dev_int, &dev_prop, &fpDevI, &fpDevP);

		deltadev = deltadev_limit(dev_int + dev_prop);

		deviation = deviation_limit(dev_prev + deltadev);

		if(deviation != dev_prev)
		{
			*timer_load = DMTIMER_1SEC + deviation;
		}


		if(verbose)
		{
			printf("Event at %lld.%09u\t err:%+03d\t dev:%+04d\t ddev:%+04d\t I:%+04d P:%+04d\t "
					"%+.4f %+.4f fDEV:%+.4f\t Phase: %d\n",
					extts_event.t.sec,
					extts_event.t.nsec,
					error_now,
					deviation,
					deltadev,
					dev_int,
					dev_prop,
					fpDevI,
					fpDevP,
					fpDeviation,
					cPhase);

			fflush(stdout);
		}
    } // End of while


    /***************************************************************/

	*gpio_out_dis = GPIO_69 | *gpio_out_dis; // Config. P8_09 as input

   /* Disable the external event timestamping */
   extts_request.flags = 0;
   if(ioctl(fd_ptp, PTP_EXTTS_REQUEST, &extts_request))
   {
	   perror("PTP_EXTTS_REQUEST disable");
   }

    *timer_ctrl = 0x1842; // Stop the timer (bit0: 0)
    *cm_per_timer5 = 0x0; // Disable DMTIMER5's clocks in CM_PER


    munmap((void *)timer5_addr, DMTIMER5_SIZE);
    munmap((void *)cm__addr, CM_SIZE);
	munmap((void *)gpio2_addr, GPIO2_SIZE);

    close(fd_ptp);

    return 0;
}




void errorCalc(uint32_t nsec)
{
	if(nsec > (500000000 + OFFSET))
	{
		error_now = nsec - (1000000000 + OFFSET);
	}
	else if(nsec  >= OFFSET)
	{
		error_now = nsec - OFFSET;
	}
	else
	{
		error_now = nsec - OFFSET;
	}
	return;
}


void adjust_pps_clock(int32_t *dev_int, int32_t *dev_prop, float *fpDevI, float *fpDevP)
{
	/* Right shift of negative signed value is implementation-defined!!! */
	switch(cPhase)
	{
	case 1 :
		*dev_int	= (int32_t)((error_now) >> KI);
		*dev_prop	= (int32_t)((error_now - error_prev) >> KP);
		break;
	case 2 :
		*dev_int	= (int32_t)((error_now) >> (KI+1));
		*dev_prop	= (int32_t)((error_now - error_prev) >> (KP+1));
		break;
	case 3 :
		if(3 == cPhase)
		{
			*fpDevI		= (error_now / PHASE3_KI);
			*fpDevP		= ((error_now - error_prev) / PHASE3_KP);

			if(abs(error_now) <= 80)
			{
				*fpDevI = 0;
			}

			fpDeviation += (*fpDevI + *fpDevP);
			// Every deviation component is in dev_int
			*dev_int	= (int32_t)fpDeviation;
			*dev_prop	= 0;
			fpDeviation	-= (float)(*dev_int);
		}
	}
}


uint32_t tsDiffCalc(uint32_t tsfirst, uint32_t tssecond)
{
	return (tssecond > tsfirst) ? (tssecond-tsfirst) : (tsfirst-tssecond);
}


int32_t deltadev_limit(int32_t dev)
{
	if(abs(dev) > LIM_DELTADEV)
	{
		dev = (dev > 0) ? LIM_DELTADEV : -LIM_DELTADEV;
	}
	else if((0 == dev) && (cPhase < 3) && (abs(error_now) > 80)) // TODO error now?
	{
		 dev = (error_now >= 0) ? 1 : -1;
	}

	if((abs(error_now) < 100) && (abs(error_prev) < 100) &&	(abs(error_prev2) < 100))
	{
		dev = 0;
	}

	return dev;
}


int32_t deviation_limit(int32_t dev)
{
	if(0 == dev && abs(error_now) > 80)
	{
		dev = (error_now >= 0) ? 1 : -1;
	}
	else if(abs(dev) > LIM_DEV)
	{
		dev = (dev > 0) ? LIM_DEV : -LIM_DEV;
	}

	return dev;
}


int8_t phase_determine()
{
	// TODO error_now ?
	switch(cPhase)
	{
		case 1 : if((abs(error_prev)<2500) && (abs(error_prev2)<2500) && (abs(error_prev-error_prev2)<1000)&& !first_run)
				{
					cPhase = 2;
				}
				break;
		case 2 : if((abs(error_prev)>3000) && (abs(error_prev2)>3000))
				{
					cPhase = 1;
				}
				else if((abs(error_prev)<300) && (abs(error_prev2)<300) && (abs(error_now-error_prev)<500)) // TODO sign check?
				{
					cPhase = 3;
				}
				break;
		case 3 :  if((abs(error_prev)>350) && (abs(error_prev2)>350)) // TODO sign check ?
				{
					cPhase = 2;
				}
				break;
	}

	return cPhase;
}


