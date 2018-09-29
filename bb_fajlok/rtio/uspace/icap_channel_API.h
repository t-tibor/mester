#ifndef __ICAP_CHANNEL_API_H_
#define __ICAP_CHANNEL_API_H_


enum timer_mode_t {NONE, ICAP, PWM, CLKDIV};

struct timer_setup_t
{
	enum timer_mode_t dmtimer4_mode;
	enum timer_mode_t dmtimer6_mode;
	enum timer_mode_t dmtimer7_mode;
	
	enum timer_mode_t ecap0_mode;
	enum timer_mode_t ecap2_mode;
};



int init_rtio(struct timer_setup_t setup);
void close_rtio();
int start_icap_logging(int timer_idx);
int start_pps_generator(int icap_timer_idx, int pwm_timer_idx);




#endif