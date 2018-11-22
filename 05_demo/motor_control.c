#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include "RTIO_API.h"


#define TARGET_FREQUENCY	25	// 25 ratation per sec
#define P 					2
#define I 					0.8
#define PI_OFFSET 			30
#define PI_ANTI_WINDUP_LIMIT 70

#define TARGET_PERIOD		(1.0/TARGET_FREQUENCY)
#define TARGET_PERIOD_US 	(1000000ULL/TARGET_FREQUENCY)

struct timer_setup_t setup = 
{
	.dmtimer4_mode = PWM,
	.ecap0_mode = ICAP,
};

struct dmtimer *pwm;
struct ts_channel *ch;

volatile int goon;
void signal_handler(int sig)
{
	fprintf(stderr,"Exiting...\n");
	(void)sig;
}


float calc_offset(uint64_t ts_ns)
{
	uint32_t offset;
	offset = (uint32_t)((ts_ns/1000ULL) % TARGET_PERIOD_US);
	
	float ratio;
	ratio = (float)offset/(float)TARGET_PERIOD_US;
	if(ratio >= 0.5) ratio -= 1;

	//if((ratio > -0.05) && (ratio < 0.05)) ratio = 0;
	return ratio;
}

// input: rotation period in sec
// output: pwm duty to apply to 
double error;
double prop;
double integral = 0;
int freq_control(float f_request, double period)
{
	int duty;

	error = (f_request)-1.0/period;
	prop =  error * P;
	integral += error * I * period;

	// anti windup
	if(integral > PI_ANTI_WINDUP_LIMIT) integral = PI_ANTI_WINDUP_LIMIT;
	if(integral < -PI_ANTI_WINDUP_LIMIT) integral = -PI_ANTI_WINDUP_LIMIT;

	duty = (int)(PI_OFFSET + prop + integral);

	if(duty < 0) duty = 0;
	if(duty > 99) duty = 99;
	return duty;
}


void *motor_controller(void *arg)
{	
	(void)arg;
	uint64_t prev_ts = 0;
	double period;

	double rot;
	uint64_t buffer[8];
	int rcvCnt;
	uint64_t next_report = 0;
	double min, max;
	int duty;
	int motor_state = 0; 		// 0 - frequency controlling
								// 1 - position controlling
	float f_request;
	float offset_ratio=0;
	float period_avg = 0;


	fprintf(stderr,"Starting motor control.\nPress Ctrl+C to exit.\n");
	
	// start dmtimer
	dmtimer_pwm_setup(pwm,24000,90);	// PWM: period: 1kHz,  duty: 30%
	dmtimer_set_pin_dir(pwm,0);			// dir to output
	dmtimer_start_timer(pwm);

	if(goon && !prev_ts)
	{
		rcvCnt = ts_channel_read(ch,buffer,1);
		if(rcvCnt > 0)
			prev_ts = buffer[0];
	}

	while(goon)
	{
		rcvCnt = ts_channel_read(ch,buffer,8);

		for(int i=0;i<rcvCnt;i++)
		{
			period = (double)((int64_t)(buffer[i] - prev_ts))*1e-9;

			// sanity check to filter EMC inducated pulses out
			if(period < TARGET_PERIOD*0.75)
			{
				fprintf(stderr,"EMC pulse detected.\n");
				continue;
			}

			f_request = TARGET_FREQUENCY;
			if(motor_state == 0)
			{
				// controlling for rotation
				period_avg = 0.99*period_avg + 0.01*period;
				if((period_avg > TARGET_PERIOD*0.98) && (period_avg < TARGET_PERIOD*1.02))
				{
					motor_state = 1;
					fprintf(stderr,"---------- Switching to position controlling.-----------\n");
				}
				
			}
			else
			{
				offset_ratio = calc_offset(buffer[i]);
				f_request*= (1+0.2*offset_ratio);
			}

			duty = freq_control(f_request, period);
			dmtimer_pwm_set_duty(pwm,24000,duty);
			printf("%" PRIu64 ", %lf, %lf, %lf, %lf, %d\r\n",buffer[i],period, error, prop, integral, duty);


			// debug messages
			if(period < min) min = period;
			if(period > max) max = period;
			if(next_report < buffer[i])
			{
				next_report = buffer[i] + 1000000000;
				rot = 1.0/ period;
				fprintf(stderr,"\nRotation speed= %lf 1/sec.\nPeriod Min:%.6lf\tMax:%.6lf\nPeriod_avg: %f\tOffset_ratio:%f\n",rot,min,max,period_avg,offset_ratio);
				min = 10000;
				max = 0; 

			}

			prev_ts = buffer[i];
		}
	}
	fprintf(stderr,"Motor control stopped.\n");


	return NULL;
}


int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int ret;
	pthread_t worker;
	void *tmp;

	signal(SIGINT, signal_handler);

	ret = init_rtio(setup);
	if(ret)
	{
		fprintf(stderr,"Cannot initialize RTIO.\n");
		return -1;
	}

	// get pointer to the input channel used by the motor control
	ch = get_ts_channel(TS_CHANNEL_ECAP0);
	if(!ch)
	{
		fprintf(stderr,"Cannot get timestamp channel\n");
		return -1;
	}

	pwm = get_dmtimer(4);
	if(!pwm)
	{
		fprintf(stderr,"Cannot get pwm generator timer.\n");
		return -1;
	}

	for(int i=0;i<5;i++)
	{
		fprintf(stderr,"Wait for the timekeeper to stabilize:");
		for(int j=0;j<i;j++)
			fprintf(stderr,".");
		fprintf(stderr,"\n");
		sleep(1);
	}
	fprintf(stderr,"\n");

	//
	goon = 1;
	ret = pthread_create(&worker, NULL, motor_controller, (void*)ch);
	if(ret)
		fprintf(stderr,"Cannot start the PPS servo thread.\n");

	// wait until signal arrives
	pause();
	goon = 0;

	pthread_join(worker,&tmp);
	(void)tmp;

	close_rtio();


	return 0;
}

