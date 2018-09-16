#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>

#include "../icap_channel.h"
#include "../timekeeper.h"


#define ROUND_2_INT(f) ((int)((f) >= 0.0 ? ((f) + 0.5) : ((f) - 0.5)))

#define LOG(x, ...) 		fprintf(stderr,"[PPS servo]" x,  ##__VA_ARGS__);
#define EXPORT(f,x,...) 	fprintf(f,"[#]" x,  ##__VA_ARGS__);


#define USE_PREDICTOR					1

#define SERVO_STATE_0_OFFSET_LIMIT 		5000 // do offset correction until the error is less than 5us

#define SERVO_STATE_1_PROP_FACTOR 		(0.9)
#define SERVO_STATE_1_OFFSET_LIMIT		2000

#define SERVO_STATE_2_PROP_FACTOR		(0.5)
#define SERVO_STATE_2_AVG_CNT			16

#define BUF_SIZE 			32
#define BUF_MASK 			(32-1)
void *pps_servo_worker(void *arg)
{
	struct PPS_servo_t *data = (struct PPS_servo_t*)arg;
	struct icap_channel *ch = data->feedback_channel;
	struct dmtimer *pwm_gen = data->pwm_gen;
	int rcvCnt;
	uint64_t ts_b[16];
	uint64_t ts_l, ts_g;

	uint64_t g[BUF_SIZE];
	uint32_t p[BUF_SIZE];

	uint32_t period_base;
	int32_t period_comp;
	uint32_t new_period;

	uint32_t idx;
	int servo_state;

	// offset corr
	uint64_t base;
	int32_t offset;
	int32_t offset_pred;

	
	// waiting timekeeper to converge
	LOG("Waiting for the timekeeper to stabilize. ");
	for(int i=5;i>0;i--)
	{
		fprintf(stderr,"%d ",i);
		sleep(1);
	}
	fprintf(stderr,"\n");

	flush_channel(ch);

	while(!quit)
	{
			servo_state = 0;
			dmtimer_pwm_set_period(pwm_gen, 24000000); // set timer period to nominal value

			// offset correction
			LOG("Starting offset correction.\n");
			rcvCnt = read_channel(ch, ts_b,16);
				if(rcvCnt < 1)
					return NULL;
			ts_l = ts_b[rcvCnt-1];
			base = ts_l;
			timekeeper_convert(tk,&base,1);
			offset = base % 1000000000ULL;
			base = base - offset;

			if(offset > 500000000LL)
			{
				base += 1000000000ULL;
				offset = 1000000000ULL - offset;
			}

			// now base contains the closes whole second


			if(abs(offset) < SERVO_STATE_0_OFFSET_LIMIT)
				servo_state = 1;	// skip state 0


			// jump correction
			LOG("Servo state 0 (jump).\n");
			while((servo_state == 0) && (!quit))
			{
				base += 1000000000ULL;
				rcvCnt = read_channel(ch,&ts_l,1);
				if(!rcvCnt)
					return NULL;

				ts_g = ts_l;
				timekeeper_convert(tk,&ts_g,1);
				offset = ts_g - base;

				// offset correction
				if(abs(offset > 300000000))
					offset /= 2;
				
				dmtimer_pwm_apply_offset(pwm_gen,  offset/42); // offset is in ns, dmtimer period is 42 nsec

				LOG("Local ts:%"PRIu64", global ts: %"PRIu64", offset: %"PRId32", correction: %d.\n",ts_l, ts_g, offset,offset/42);

				if(abs(offset) < SERVO_STATE_0_OFFSET_LIMIT)
					servo_state = 1;
			}


			LOG("Servo state 1 (one step drift estimation).\n");
			idx = 0;
			p[0] = 24000000;
			while( (servo_state == 1) && (!quit))
			{
				double global_diff;
				double tick_sum;

				base += 1000000000ULL;
				rcvCnt = read_channel(ch, &ts_l,1);
				if(rcvCnt < 1)
					break;

				ts_g = ts_l;
				timekeeper_convert(tk,&ts_g,1);
				g[idx&BUF_MASK] = ts_g;

				if(idx > 0)
				{		
					// calculating the next period based on the last 2 timestamps
					global_diff = (double)((uint64_t)(g[idx & BUF_MASK] - g[(idx-1) & BUF_MASK]));
					tick_sum = p[(idx-1) & BUF_MASK];

					period_base = (tick_sum / (global_diff*1e-9) + 0.5); //add 0.5 to round instead of truncate

					// calc period offset
					offset = ts_g - base;   // time difference to the closest whole second in ns.

					#if USE_PREDICTOR == 1
						// calculate offset at the next whole second
						offset_pred = (uint32_t)((double)(offset) + ((int32_t)p[idx & BUF_MASK] - (int32_t)period_base)*41.667);
					#else
						offset_pred = offset;
					#endif

					period_comp = ROUND_2_INT(-(offset_pred/41.667)*SERVO_STATE_1_PROP_FACTOR);
					new_period = period_base + period_comp;

					// apply new period
					dmtimer_pwm_set_period(pwm_gen, new_period);
				}
				else
				{
					new_period = 24000000;
				}
			
				idx++;
				p[idx&BUF_MASK] = new_period;

				if(offset > SERVO_STATE_0_OFFSET_LIMIT)
					servo_state = 0;
				else if(offset > SERVO_STATE_1_OFFSET_LIMIT)
				{
					// offset too big, restart data collection
					idx = 0;
				}
				else if(idx > SERVO_STATE_2_AVG_CNT)
				{
					servo_state = 2;
				}

				LOG("Local ts:%"PRIu64", global ts: %"PRIu64".\n",ts_l, ts_g);
			}
			

			// active controlling
			LOG("Servo state 2 (averaging drift estimation).\n");
			while((servo_state == 2) && (!quit))
			{
				double global_diff;
				double tick_sum;

				base += 1000000000ULL;
				rcvCnt = read_channel(ch,&ts_l,1);
				if(rcvCnt != 1)
					continue;

				ts_g = ts_l;
				timekeeper_convert(tk,&ts_g,1);
				g[idx&BUF_MASK] = ts_g;

					// calculating the next period based on the last 4 timestamp
					global_diff = (double)((uint64_t)(g[idx & BUF_MASK] - g[(idx-SERVO_STATE_2_AVG_CNT) & BUF_MASK]));
					tick_sum = 0;
					for(int i=SERVO_STATE_2_AVG_CNT; i > 0; i--)
						tick_sum += p[(idx - i) & BUF_MASK];

					period_base = (tick_sum / (global_diff*1e-9) + 0.5); //add 0.5 to round instead of truncate

					// calc period offset
					offset = ts_g - base;   // time difference to the closest whole second in ns.

					#if USE_PREDICTOR == 1
						// calculate offset at the next whole second
						offset_pred = (uint32_t)((double)(offset) + ((int32_t)p[idx & BUF_MASK] - (int32_t)period_base)*41.667);
					#else
						offset_pred = offset;
					#endif

					period_comp = ROUND_2_INT(-(offset_pred/41.667)*SERVO_STATE_2_PROP_FACTOR);
					new_period = period_base + period_comp;

					// apply new period
					dmtimer_pwm_set_period(pwm_gen, new_period);
				// save new period
				idx++;
				p[idx & BUF_MASK] = new_period;

				LOG("\n\tLocal ts:%"PRIu64", global ts:%"PRIu64"\n\tglob_dif: %lf, tick_sum: %lf\n\toffset: %"PRId32", offset_pred: %"PRId32"\n\tnew period: %"PRIu32" = %"PRIu32" %+"PRId32".\n\n",
					ts_l,
					ts_g,
					global_diff,
					tick_sum,
					offset,
					offset_pred,
					new_period,
					period_base,
					period_comp
					);

				if(offset > SERVO_STATE_1_OFFSET_LIMIT)
				{
					LOG("Restarting servo.\n");
					break;
				}
			}
	}
	LOG("Terminating.\n");
	return NULL;
}