#ifndef BBONEPPS_H_
#define BBONEPPS_H_

/*
 * The mapped memory size must be at least the size of
 * the virtual memory PAGESIZE of the system. (getconf PAGESIZE or PAGE_SIZE)
 * The BBone's page size is 4096 Bytes (0xfff).
 */

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
#define OFFSET		-140


/***************************************************************/
// Clock Module
#define CM_START_ADDR		0x44e00000
#define CM_END_ADDR			0x44e03fff
#define CM_SIZE 			(CM_END_ADDR - CM_START_ADDR)
// Peripheral Registers
#define CM_PER_TIMER5_CLKCTRL	0xEC	// This register manages the TIMER5 clocks
// PLL (CM_DPLL) Registers
#define CLKSEL_TIMER5_CLK 		0x518	// Selects the Mux select line for TIMER5 clock
#define CM_CPTS_RFT_CLKSEL		0x520	// Selects the Mux select line for CPTS RFT clock

/***************************************************************/
#define DMTIMER5_START_ADDR		0x48046000
#define DMTIMER5_END_ADDR		0x48046FFF
#define DMTIMER5_SIZE 			(DMTIMER5_END_ADDR - DMTIMER5_START_ADDR)

#define DMTIMER_TCLR	0x38	// Timer Control Register
#define DMTIMER_TCRR	0X3C	// Timer Counter Register
#define DMTIMER_TLDR	0X40	// Timer Load Register
#define DMTIMER_TTGR	0X44	// Timer Trigger Register
#define DMTIMER_TMAR	0X4C	// Timer Match Register
#define DMTIMER_TCAR1	0X50	// Timer Capture_1 Register

/***************************************************************/
// Ethernet Switch Subsystem
#define CPSW_START	0x4a100000
#define CPSW_END	0x4a1010FF // 0x4a107fff
#define CPSW_SIZE 	(CPSW_END - CPSW_START)
// Ethernet Time Sync Module
#define CPTS_CONTROL		0xC04	// Time sync control register
#define CPTS_TS_PUSH		0xC0C	// Time stamp event push register
#define CPTS_INT_ENABLE		0xC28	// Time sync interrupt enable register
#define CPTS_INTSTAT_RAW	0xC20	// Time sync interrupt status raw register
#define CPTS_INTSTAT_MASKED	0xC24	// Time sync interrupt status masked register
#define CPTS_EVENT_POP      0xC30	// Event interrupt pop register
#define CPTS_EVENT_LOW      0xC34	// LOWER 32-BITS OF THE EVENT VALUE
#define CPTS_EVENT_HIGH     0xC38	// UPPER 32-BITS OF THE EVENT VALUE

#define C0_MISC_EN			0x121C	// Subs. Core1 misc. interrupt enable register
#define C0_MISC_STAT		0x124C	// Subs. Core1 misc. masked interrupt status register

#define HW2_TS_PUSH_ENABLE	0x00000201	// DMTIMER5 PORTIMERPWM sign
#define EVNT_PEND_INTR_BIT	0x00000010

/***************************************************************/
// GPIO Registers
#define GPIO2_START_ADDR    0x481AC000
#define GPIO2_END_ADDR      0x481ACFFF
#define GPIO2_SIZE          (GPIO2_END_ADDR - GPIO2_START_ADDR)

#define GPIO_OE             0x134   // GPIO output enable register
#define GPIO_69             (1<<5)	// P8_09

#endif /* BBONEPPS_H_ */
