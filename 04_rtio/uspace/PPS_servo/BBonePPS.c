#include "BBonePPS.h"
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

struct BBonePPS_t* BBonePPS_create()
{
	struct BBonePPS_t *ret;
	ret = (struct BBonePPS_t*)malloc(sizeof(struct BBonePPS_t));
	if(ret)
	{
		ret->first_run	= 1;
		ret->count = 0;

		ret->state = SERVO_OFFSET_JUMP;

		ret->error_prev	= 0;
		ret->error_prev2	= 0;
		ret->dev_prev = 0;
		ret->fpDeviation	= 0;

		ret->period = 24000000;
	}
	return ret;
}

void BBonePPS_destroy(struct BBonePPS_t *servo)
{
	free(servo);
}

// servo parameters
#define SLEEP_T	2
int BBonePPS_sample(struct BBonePPS_t *s, int64_t error)
{
		int32_t dev_prop	= 0;
		int32_t dev_int		= 0;
		float fpDevI		= 0;
		float fpDevP		= 0;

		int32_t deviation;
		int32_t deltadev;

		int32_t  error_now = error;
		int32_t  error_prev = s->error_prev;
		int32_t  error_prev2 = s->error_prev2;


		if(s->state == SERVO_OFFSET_JUMP)
		{
			if(abs(error_now) > STEP_MINERROR)
			{
				/// signal step
				return 1;
			}
			else 
			{
				s->state = SERVO_LOCKED1; 
			}
		}


		s->count++;
		if(s->count%SLEEP_T) { return 0; }


		// ************************** PHASE DETERMINATION *************************//
		// Determine the phase of the regulating process (clock servo)
		switch(s->state)
		{
			case SERVO_LOCKED1 : 
					if((abs(error_now)<2500) && (abs(error_prev)<2500) && (abs(error_now-error_prev)<1000)&& !s->first_run)
					{
						s->state = SERVO_LOCKED2;
					}
					break;
			case SERVO_LOCKED2 :
				 	if((abs(error_now)>3000) && (abs(error_prev)>3000))
					{
						s->state = SERVO_LOCKED1;
					}
					else if((abs(error_now)<300) && (abs(error_prev)<300) && (abs(error_now-error_prev)<500)) // TODO sign check?
					{
						s->state = SERVO_LOCKED3;
					}
					break;
			case SERVO_LOCKED3 :  if((abs(error_now)>350) && (abs(error_prev)>350)) // TODO sign check ?
					{
						s->state = SERVO_LOCKED2;
					}
					break;
			default:
				exit(-5);
		}



		if (s->first_run)
		{
			s->first_run = 0;
		}

		/* Clock servo */


			/* Right shift of negative signed value is implementation-defined!!! */
		switch(s->state)
		{
		case SERVO_LOCKED1 :
			dev_int	= (int32_t)((error_now) >> KI);
			dev_prop	= (int32_t)((error_now - error_prev) >> KP);
			break;
		case SERVO_LOCKED2 :
			dev_int	= (int32_t)((error_now) >> (KI+1));
			dev_prop	= (int32_t)((error_now - error_prev) >> (KP+1));
			break;
		case SERVO_LOCKED3 :
			fpDevI		= (error_now / PHASE3_KI);
			fpDevP		= ((error_now - error_prev) / PHASE3_KP);

			if(abs(error_now) <= 80)
			{
				fpDevI = 0;
			}

			s->fpDeviation += (fpDevI + fpDevP);
			// Every deviation component is in dev_int
			dev_int	= (int32_t)(s->fpDeviation);
			dev_prop	= 0;
			s->fpDeviation	-= (float)(dev_int);
			break;
			default:
			exit(-6);
		
		}

		//////////////////////////// deltadev linie ///////////////////////

		deltadev = dev_int + dev_prop;

		if(abs(deltadev) > LIM_DELTADEV)
		{
			deltadev = (deltadev > 0) ? LIM_DELTADEV : -LIM_DELTADEV;
		}
		else if((0 == deltadev) && (s->state < SERVO_LOCKED3) && (abs(error_now) > 80)) // TODO error now?
		{
			 deltadev = (error_now >= 0) ? 1 : -1;
		}

		if((abs(error_now) < 100) && (abs(error_prev) < 100) &&	(abs(error_prev2) < 100))
		{
			deltadev = 0;
		}

		
		deviation = s->dev_prev + deltadev;

		if(0 == deviation && abs(error_now) > 80)
		{
			deviation = (error_now >= 0) ? 1 : -1;
		}
		else if(abs(deviation) > LIM_DEV)
		{
			deviation = (deviation > 0) ? LIM_DEV : -LIM_DEV;
		}



		if(deviation != s->dev_prev)
		{
			s->period = 24000000 - deviation;
		}


		if(1)
		{
			printf("Err:%5d\t dev:%+04d\t ddev:%+04d\t I:%+04d P:%+04d\t "
					"%+.4f %+.4f fDEV:%+.4f\t Phase: %d\n",
					error_now,
					deviation,
					deltadev,
					dev_int,
					dev_prop,
					fpDevI,
					fpDevP,
					s->fpDeviation,
					s->state);
		}

		s->error_prev2	= s->error_prev;
		s->error_prev	= error_now;
		s->dev_prev	= deviation;

		return 1;
}