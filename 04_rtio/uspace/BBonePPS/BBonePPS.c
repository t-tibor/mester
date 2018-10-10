#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
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

#include "../icap_channel.h"

#include "../timekeeper.h"

#define DMTIMER_1SEC		4270966826 // 0xFE91C82A 1s ; 4271000000 with 42
#define DMTIMER_DUTY10		4273366873 // 10%
#define STEP_MINERROR		5000
#define	LIM_DEV				1000
#define LIM_DELTADEV		200
#define LIM_DELTADEV_LO		3

#define KI	8	// Integrator gain 7
#define KP	7	// Proportional gain 6
#define PHASE3_KI	320.0
#define PHASE3_KP	90.0 // 80.0
#define OFFSET		0 //-140



//#define DEBUG_1

/* Global variables */
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
int locked_up		= 0;


uint64_t base, tmp;


void errorCalc(int64_t ts);
void adjust_pps_clock(int32_t *dev_int, int32_t *dev_prop, float *fpDevI, float *fpDevP);
int8_t phase_determine();
int32_t deltadev_limit(int32_t dev);
int32_t deviation_limit(int32_t dev);
uint32_t tsDiffCalc(uint32_t tsfirst, uint32_t tssecond);



void* BBonePPS_worker(void *arg)
{
	struct PPS_servo_t *s = (struct PPS_servo_t*)arg;
	struct icap_channel *pps_channel = s->feedback_channel;
	struct dmtimer *pwm_gen = s->pwm_gen;

	int32_t stepVal		= 0;
	int32_t stepVal_accu = 0;
	//int32_t cnt			= 0;
	uint32_t count		= 1;
	int32_t dev_prev	= 0;
	int32_t dev_prop	= 0;
	int32_t dev_int		= 0;
	float fpDevI		= 0;
	float fpDevP		= 0;

	uint64_t buf[5];
	uint64_t pps_timestamp;
	int rcvCnt;

	int verbose = 1;

	// wait for the timekeeper con settle
	printf("BBonePPS initial delay\n");
	sleep(5);

	// to avoid using modulo operation determine the closest whole second and the error relative to it

	rcvCnt = read_channel(pps_channel, buf, 5);
	pps_timestamp = buf[rcvCnt-1];
	timekeeper_convert(tk, &pps_timestamp,1);

	tmp = pps_timestamp % 1000000000ULL; // nanosecond part
	base = pps_timestamp - tmp;

	// round to the closest whole second
	if(tmp > 500000000ULL)
		base += 1000000000ULL; 



   /***************************************************************/
    while(!quit)
    {
    	rcvCnt = read_channel(pps_channel, buf, 5);
    	if(rcvCnt == 0)
    		continue;

    	pps_timestamp = buf[rcvCnt-1];
    	// convert ppt_timestamp to global time
    	timekeeper_convert(tk, &pps_timestamp,1);

    	base += 1000000000ULL;


    
		if(step)
		{
			errorCalc(pps_timestamp); // Can be positive or negative!

			if(abs(error_now) > STEP_MINERROR)
			{
				stepVal = (error_now/42);
				stepVal_accu += stepVal / 20;

				// *timer_counter = (uint32_t)(*timer_counter + stepVal+470);
				dmtimer_pwm_apply_offset(pwm_gen,  stepVal);

				if(verbose)
				{
					printf("xEvent at %"PRIu64"\t err:%+03d\t step:%+03d\t\n",
							pps_timestamp,
							error_now,
							stepVal);
			    	fflush(stdout);
				}

		    	continue; // Next while cycle
			}
			else 
			{
				printf("Starting PI servo\n");
				step = 0; 
			} // < STEP_MINERROR

		} // IF_STEP

		count++;
		if(count%sleepT) { continue; }

		error_prev2	= error_prev;
		error_prev	= error_now;
		dev_prev	= deviation;
		errorCalc(pps_timestamp);

		phase_determine(); // Determine the phase of the regulating process (clock servo)

		if (first_run)
		{
			first_run = 0;
		//	*gpio_out_dis = (~GPIO_69) & *gpio_out_dis; // Config. P8_09 as output
		}

		/* Clock servo */

		adjust_pps_clock(&dev_int, &dev_prop, &fpDevI, &fpDevP);

		deltadev = deltadev_limit(dev_int + dev_prop);

		deviation = deviation_limit(dev_prev + deltadev);

		if(deviation != dev_prev)
		{
			//*timer_load = DMTIMER_1SEC + deviation;
			dmtimer_pwm_set_period(pwm_gen, 24000470 -  deviation);
			printf("New period: 24000470 -%d = %d\n",deviation, 24000470-deviation);

		}

		if(locked_up == 0 && (error_now == error_prev) && error_now > -100 && error_now < 100)
		{
			locked_up = 1; 
			printf("Phase 3 locked.\n");
		}

		if(verbose)
		{
			printf("Event at %15"PRIu64"\t err:%+03d\t dev:%+04d\t ddev:%+04d\t I:%+04d P:%+04d\t "
					"%+.4f %+.4f fDEV:%+.4f\t Phase: %d\n",
					pps_timestamp,
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

    return NULL;
}




void errorCalc(int64_t ts)
{
	int64_t nsec;
	nsec = ts - (int64_t) base;

	if(nsec > (500000000 + OFFSET))
	{
		error_now = nsec - (1000000000 + OFFSET);
		base += 1000000000;
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
		//dev = (error_now >= 0) ? 1 : -1;
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
				else if((abs(error_prev)<400) && (abs(error_prev2)<400) && (abs(error_now-error_prev)<400)) // TODO sign check?
				{
					cPhase = 3;
				}
				break;
		case 3 :  if((abs(error_prev)>400) && (abs(error_prev2)>400)) // TODO sign check ?
				{
					cPhase = 2;
				}
				break;
	}

	return cPhase;
}


