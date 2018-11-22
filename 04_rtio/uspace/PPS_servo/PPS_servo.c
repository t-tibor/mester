#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

#include "PPS_servo/PPS_servo.h"
#include "icap_channel.h"
#include "timekeeper.h"
#include "circ_buf.h"
#include "utils.h"


#define ROUND_2_INT(f) ((int)((f) >= 0.0 ? ((f) + 0.5) : ((f) - 0.5)))

#define LOG(level,x, ...) 		{if(verbose_level >= level) fprintf(stderr,"[PPS servo]" x,  ##__VA_ARGS__); }


#define SERVO_STATE_0_OFFSET_LIMIT 		(1000000) // do offset correction until the error is less than 1ms

#define SERVO_STATE_1_PROP_FACTOR 		(0.9)
#define SERVO_STATE_1_OFFSET_LIMIT		100000

#define SERVO_STATE_2_PROP_FACTOR		(0.5)
#define SERVO_STATE_2_AVG_CNT			16
#define SERVO_STATE_2_MAX_OFFSET		20000 // stay in sate 2 until 20 usec error

#define BUF_SIZE 			32
#define BUF_MASK 			(32-1)


struct pps_servo_context
{
	uint64_t base;
	uint64_t ts_g;
	double global_diff;
	double tick_sum;

	int32_t offset;
	double offset_pred;

	uint32_t prev_period;
	uint32_t period_base;
	int32_t period_comp;
	uint32_t new_period;

};



static void calc_offset(uint64_t ts_g, uint64_t period_ns, uint64_t *base, int32_t *offset)
{
	*offset = ts_g % period_ns;
	*base = ts_g - *offset;

	if(*offset > (int32_t)(period_ns>>1))
	{
		*base += period_ns;
		*offset = -(period_ns - *offset);
	}

}


static void *pps_servo_worker(void *arg)
{
	struct PPS_servo_t *data 		= (struct PPS_servo_t*)arg;
	struct ts_channel *ch 			= data->feedback_channel;
	struct dmtimer *pwm_gen 		= data->pwm_gen;
	uint64_t pps_period_ns			= 1000000000;


	uint32_t clk_rate				= data->pwm_gen->clk_rate;
	double clk_tick_time_ns			= 1e9/(double)clk_rate;
	uint32_t timer_nominal_period 	= clk_rate;
	int verbose_level 				= data->verbose_level;
	struct circ_buf *log_buf 		= (struct circ_buf*)data->priv;
	

	int rcvCnt;
	uint64_t ts_b[16];

	uint32_t idx;
	uint64_t g[BUF_SIZE];
	uint32_t p[BUF_SIZE];

	struct pps_servo_context ctx;

// context
	int servo_state;

	uint64_t base;
	uint64_t ts_g;

	int32_t offset;
	double offset_pred;

	uint32_t prev_period;
	uint32_t period_base;
	int32_t period_comp;
	uint32_t new_period;
	
	int ok_count;

	
	ch->flush(ch);

	dmtimer_pwm_setup(pwm_gen,timer_nominal_period,30);
	dmtimer_set_pin_dir(pwm_gen,0);
	dmtimer_start_timer(pwm_gen);

	// increase own priority to the pre defined level
	if(goto_rt_level(PPS_SERVO_RT_PRIO))
	{
		fprintf(stderr,"[ERROR] Cannot increase PPS servo rt priority.\n");
	}

	while(!rtio_quit)
	{
			servo_state = 0;
			dmtimer_pwm_set_period(pwm_gen, timer_nominal_period); // set timer period to nominal value

			// offset correction
			LOG(1,"Starting offset correction.\n");
			rcvCnt = ch->read(ch,ts_b,16);
				if(rcvCnt < 1)
					return NULL;

			ts_g = ts_b[rcvCnt-1];
			calc_offset(ts_g, pps_period_ns, &base, &offset);

			// now base contains the closes whole second
			if(abs(offset) < SERVO_STATE_0_OFFSET_LIMIT)
				servo_state = 1;	// skip state 0


			// jump correction
			LOG(1,"Servo state 0 (jump).\n");
			while((servo_state == 0) && (!rtio_quit))
			{
				rcvCnt = ch->read(ch,&ts_g,1);
				if(!rcvCnt)
					return NULL;

				calc_offset(ts_g,pps_period_ns,&base,&offset);


				// limiting offset
				if(abs(offset > 300000000))
					offset /= 2;

				if(offset > 500000000)
					offset = 500000000;
	
				if(offset < -500000000)
					offset = -500000000;
				


				dmtimer_pwm_apply_offset(pwm_gen, ((float)offset)*clk_rate/1000000000); // offset is in ns

				LOG(2,"Global ts: %"PRIu64", offset: %"PRId32", correction: %d.\n",ts_g, offset, (int32_t)((double)offset)*clk_rate/1000000000);

				if(abs(offset) < SERVO_STATE_0_OFFSET_LIMIT)
					servo_state = 1;
			}


			LOG(1,"Servo state 1 (one step drift estimation).\n");
			idx = 0;
			p[0] = timer_nominal_period;
			ok_count = 0;
			while( (servo_state == 1) && (!rtio_quit))
			{
				double global_diff;
				double tick_sum;

				base += pps_period_ns;
				rcvCnt = ch->read(ch, &ts_g,1);
				if(rcvCnt < 1)
					break;

				g[idx&BUF_MASK] = ts_g;

				if(idx > 0)
				{		
					// calculating the next period based on the last 2 timestamps
					global_diff = (double)((uint64_t)(g[idx & BUF_MASK] - g[(idx-1) & BUF_MASK]));
					tick_sum = p[(idx-1) & BUF_MASK];

					period_base = (tick_sum / (global_diff*1e-9) + 0.5); //add 0.5 to round instead of truncate

					// calc period offset
					offset = ts_g - base;   // time difference to the closest whole second in ns.

					prev_period = p[idx & BUF_MASK];

					// calculate offset at the next whole second
					offset_pred = (double)(offset) + (((int32_t)prev_period - (int32_t)period_base)*clk_tick_time_ns);
				

					period_comp = ROUND_2_INT(-(offset_pred/clk_tick_time_ns)*SERVO_STATE_1_PROP_FACTOR);
					new_period = period_base + period_comp;

					
					dmtimer_pwm_set_period(pwm_gen, new_period);

				}
				else
				{
					new_period = timer_nominal_period;
				}
			
				idx++;
				p[idx&BUF_MASK] = new_period;

				if(offset > 2*SERVO_STATE_0_OFFSET_LIMIT || offset < -2*SERVO_STATE_0_OFFSET_LIMIT)
					servo_state = 0;
				else if(abs(offset) > SERVO_STATE_1_OFFSET_LIMIT)
				{
					// offset too big, restart data collection
					ok_count = 0;

				}
				else
				{
					ok_count ++;
				}

				if(ok_count > SERVO_STATE_2_AVG_CNT)
				{
					servo_state = 2;
				}


				LOG(2,"Global ts: %"PRIu64", new period: %"PRIu32".\n", ts_g,new_period);

				LOG(2,"\n\t clk_rate: %u, clock_tick_time: %lf\n\tglob_dif: %lf, tick_sum: %lf\n\toffset: %"PRId32", offset_pred: %lf\n\tprev period: %"PRIu32"\n\tnew period: %"PRIu32" = %"PRIu32" %+"PRId32".\n\n",
				clk_rate,
				clk_tick_time_ns,
				global_diff,
				tick_sum,
				offset,
				offset_pred,
				prev_period,
				new_period,
				period_base,
				period_comp
				);
			}
			

			// active controlling
			LOG(1,"Servo state 2 (averaging drift estimation).\n");
			while((servo_state == 2) && (!rtio_quit))
			{
				double global_diff;
				double tick_sum;

				base += pps_period_ns;
				rcvCnt = ch->read(ch,&ts_g,1);
				if(rcvCnt != 1)
					continue;

				g[idx&BUF_MASK] = ts_g;

					// calculating the next period based on the last 4 timestamp
					global_diff = (double)((uint64_t)(g[idx & BUF_MASK] - g[(idx-SERVO_STATE_2_AVG_CNT) & BUF_MASK]));
					tick_sum = 0;
					for(int i=SERVO_STATE_2_AVG_CNT; i > 0; i--)
						tick_sum += p[(idx - i) & BUF_MASK];


					period_base = (tick_sum / (global_diff*1e-9) + 0.5); //add 0.5 to round instead of truncate


					// calc period offset
					offset = ts_g - base;   // time difference to the closest whole second in ns.

					prev_period = p[idx & BUF_MASK];

					// calculate offset at the next whole second
					offset_pred = (double)(offset) + (((int32_t)prev_period - (int32_t)period_base)*clk_tick_time_ns);
				

					period_comp = ROUND_2_INT(-(offset_pred/clk_tick_time_ns)*SERVO_STATE_2_PROP_FACTOR);
					new_period = period_base + period_comp;

					// apply new period
					dmtimer_pwm_set_period(pwm_gen, new_period);
				// save new period
				idx++;
				p[idx & BUF_MASK] = new_period;

				LOG(2,"\n\tglobal_ts:%"PRIu64"\n\toffset:%"PRId32"\n",ts_g, offset);

				LOG(2,"\n\t clk_rate: %u, clock_tick_time: %lf\n\tglob_dif: %lf, tick_sum: %lf\n\toffset_pred: %lf\n\tprev period: %"PRIu32"\n\tnew period: %"PRIu32" = %"PRIu32" %+"PRId32".\n\n",
					clk_rate,
					clk_tick_time_ns,
					global_diff,
					tick_sum,
					offset_pred,
					prev_period,
					new_period,
					period_base,
					period_comp
					);

				// save context
				ctx.base=       base;
				ctx.ts_g=       ts_g;
				ctx.global_diff=global_diff;
				ctx.tick_sum= tick_sum;
				ctx.offset=     offset;
				ctx.offset_pred=offset_pred;
				ctx.prev_period=prev_period;
				ctx.period_base=period_base;
				ctx.period_comp=period_comp;
				ctx.new_period=	new_period;
				circ_buf_push(log_buf,&ctx);


				if(abs(offset) > SERVO_STATE_2_MAX_OFFSET)
				{
					LOG(1,"Restarting servo.\n");
					break;
				}
			}
	}
	LOG(1,"Terminating.\n");

	// stopping timer
	dmtimer_stop_timer(pwm_gen);
	dmtimer_set_pin_dir(pwm_gen,1);

	return NULL;
}


static void *pps_logger_worker(void *arg)
{
	struct PPS_servo_t *s = (struct PPS_servo_t*)arg;
	struct circ_buf *b = (struct circ_buf*)s->priv;
	int verbose_level = s->verbose_level;
	struct pps_servo_context c;

	// output file
	FILE *fout;

	if(goto_rt_level(PPS_LOGGER_RT_PRIO))
	{
		fprintf(stderr,"[ERROR] Cannot increase pps logger rt level.\n");
	}

	fout = fopen("./pps.log","w");
	if(!fout)
	{
		LOG(0,"Cannot open the PPS log output file.\n");
		return 0;
	}
	LOG(0,"Starting PPS logger\n");
	// print header
	fprintf(fout,"Local timestamp, global timestamp, global difference, tick sum, offset, predicated offset, period base, period compensation\n");

	while(!circ_buf_pop(b,&c))
	{
		fprintf(fout,
				"%"PRIu64","	// gts
				"%lf,%lf,"		// glob_dif , tick_sum
				"%"PRId32","	// offset
				"%.2lf,"	// offset pred
				"%"PRIu32","	// period base
				"%"PRId32"\n",	// period comp
				c.ts_g, c.global_diff, c.tick_sum, c.offset, c.offset_pred, c.period_base, c.period_comp);
	}

	fclose(fout);
	LOG(0,"PPS logger stopped.\n");
	return NULL;
}


void* pps_worker(void *arg)
{
	pthread_t logger_thread;
	struct PPS_servo_t *s = (struct PPS_servo_t*)arg;
	struct circ_buf *log_buf;
	void *tmp;
	int err;

	log_buf= circ_buf_create(64,sizeof(struct pps_servo_context));
	if(!log_buf)
		return NULL;

	s->priv = log_buf;

	// start logger thread
	err = pthread_create(&logger_thread, NULL, pps_logger_worker, (void*)s);
	if(err)
	{
		fprintf(stderr,"Cannot start the PPS logger thread.\n");
		return NULL;
	}
	

	pps_servo_worker((void*)s);

	circ_buf_wake_consumer(log_buf);
	pthread_join(logger_thread,&tmp);
	circ_buf_destroy(log_buf);

	free(s);

	return NULL;
}