#ifndef __BBONE_PPS_H
#define __BBONE_PPS_H

#include <stdint.h>

#define PTP_DEVICE			"/dev/ptp0"
#define HWTS_PUSH_INDEX		1 // HW2_TS_PUSH-1	
#define DMTIMER_1SEC		4270966826 // 0xFE91C82A 1s ; 4271000000 with 42
#define DMTIMER_DUTY10		4273366873 // 10%
#define STEP_MINERROR		1000
#define	LIM_DEV				1000
#define LIM_DELTADEV		200
#define LIM_DELTADEV_LO		3

#define KI	8	// Integrator gain 7
#define KP	7	// Proportional gain 6
#define PHASE3_KI	320.0
#define PHASE3_KP	90.0 // 80.0
#define OFFSET		0 //-140



enum BBonePPS_state {SERVO_OFFSET_JUMP, SERVO_LOCKED1, SERVO_LOCKED2, SERVO_LOCKED3};

struct BBonePPS_t
{
	int8_t first_run;
	unsigned count;

	enum BBonePPS_state state;

	int32_t error_prev;
	int32_t error_prev2;

	int32_t dev_prev;

	float fpDeviation;

	uint32_t period;
};



struct BBonePPS_t* BBonePPS_create();
void BBonePPS_destroy(struct BBonePPS_t *servo);
int BBonePPS_sample(struct BBonePPS_t *s, int64_t error);






#endif