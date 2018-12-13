#ifndef __PPS_SERVO_H_
#define __PPS_SERVO_H_

#define PPS_SERVO_RT_PRIO	10
#define PPS_LOGGER_RT_PRIO  1

#include "../icap_channel.h"
// pps signal 
struct PPS_servo_t
{
	struct ts_channel *feedback_channel;
	struct dmtimer *pwm_gen;
	int verbose_level;

	void *priv;
};

void* pps_worker(void *arg);

struct nPPS_servo_t
{
	struct icap_channel *feedback_channel;
	struct dmtimer *pwm_gen;
	int verbose_level;

	void *priv;

	double period_ms;
	unsigned hw_prescaler;
};

void* npps_worker(void *arg);

#endif