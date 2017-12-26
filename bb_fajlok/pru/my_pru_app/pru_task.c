#include <stdint.h>
//#define TEST_TASK

/* the registers for I/O and interrupts */
//volatile register unsigned int __R31, __R30;	
volatile register uint32_t __R31, __R30;				

int main()
{
	unsigned int prev_pin_value = 0;
	unsigned int value_buff = 0;

#ifndef TEST_TASK
	/* PPS scheduled task */ 
	while(1)
	{
		while( prev_pin_value || (!value_buff) )	// r31.15 (P8_15)
		{
			prev_pin_value = value_buff;	
			value_buff	= (__R31 & 1<<15);
		}				

	  	__R30 = __R30 | 1<<14;		// turn on the LED r30.14 (P8_12)
		__delay_cycles(2000000);	// x*5ns
	  	__R30 = __R30 & 0<<14;		// turn off the LED r30.14

		prev_pin_value = value_buff;
		value_buff	= (__R31 & 1<<15);
	}
#endif
#ifdef TEST_TASK
	/* Test task: square wave, 50Hz */
	while(1)
	{					
      	__R30 = __R30 | 1<<14;		// turn on the LED r30.14 (P8_12)
		__delay_cycles(2000000);	// x*5ns
      	__R30 = __R30 & 0<<14;		// turn off the LED r30.14
		__delay_cycles(2000000);
	}
#endif

}
