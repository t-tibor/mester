#ifndef __RTIO_API_H_
#define __RTIO_API_H_

#include <stdint.h>


enum timer_mode_t {NONE, ICAP, PWM, CLKDIV};

struct timer_setup_t
{
	enum timer_mode_t dmtimer4_mode;
	enum timer_mode_t dmtimer6_mode;
	enum timer_mode_t dmtimer7_mode;

	int dmtimer4_cpts_hwts_en;
	int dmtimer6_cpts_hwts_en;
	int dmtimer7_cpts_hwts_en;
	
	enum timer_mode_t ecap0_mode;
	enum timer_mode_t ecap2_mode;
};


int init_rtio(struct timer_setup_t setup);
void close_rtio();


int start_pps_generator(int ts_channel_idx, int pwm_timer_idx, int verbose_level);
int start_npps_generator(int ts_channel_idx, int pwm_timer_idx, uint32_t pps_period_ms, uint32_t hw_prescaler, int verbose_level);

int start_icap_logging(int timer_idx, int verbose);


struct ts_channel;
#define TS_CHANNEL_CPTS_1			-4
#define TS_CHANNEL_CPTS_2			-5
#define TS_CHANNEL_CPTS_3 			-6
#define TS_CHANNEL_CPTS_4 			-7
#define TS_CHANNEL_ECAP0 			 0
#define TS_CHANNEL_ECAP2 			 2
#define TS_CHANNEL_DMTIMER4 		 4
#define TS_CHANNEL_DMTIMER5 		 5
#define TS_CHANNEL_DMTIMER6 		 6
#define TS_CHANNEL_DMTIMER7 		 7
struct ts_channel *get_ts_channel(int channel_idx);
int ts_channel_read(struct ts_channel *ts, uint64_t *buffer, int size);

struct dmtimer;
struct dmtimer *get_dmtimer(int dmtimer_idx);
int dmtimer_pwm_apply_offset(struct dmtimer *t, int32_t offset);
int dmtimer_pwm_set_period(struct dmtimer *t, uint32_t period);
int dmtimer_pwm_set_duty(struct dmtimer *t, uint32_t period, uint32_t duty);
int dmtimer_pwm_setup(struct dmtimer *t, uint32_t period, uint32_t duty);
int dmtimer_set_pin_dir(struct dmtimer *t, uint8_t dir);
void dmtimer_start_timer(struct dmtimer *t);
void dmtimer_stop_timer(struct dmtimer *t);



#endif