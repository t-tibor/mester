
#ifndef BBONEPPS_H_
#define BBONEPPS_H_

/*
 * The mapped memory size must be at least the size of
 * the virtual memory PAGESIZE of the system. (getconf PAGESIZE or PAGE_SIZE)
 * The BBone's page size is 4096 Bytes (0xfff).
 */

#define CALLB_1_NSEC  100000L // 5000L
#define KTHREAD_1_NSEC  64000L // 100000L

 //#define GPIO_PIN 67
#define GPIO_PIN 44

/***************************************************************/
// Clock Module Registers
#define CM_REGS_START       0x44E00000  // Clock Module Peripheral Registers START
#define CM_REGS_END         0x44E00AFF  // Clock Module Efuse Registers END
#define CM_REGS_SIZE        (CM_REGS_END - CM_REGS_START)
// CM Peripheral Registers
#define CM_PER_TIMER5_CLKCTRL   0xEC    // This register manages the TIMER5 clocks
#define CM_PER_TIMER_ENABLE     0x2     // Clock module is explicitly enabled
// CM DPLL Registers
#define CLKSEL_TIMER5_CLK   0x518       // Selects the Mux select line for TIMER5 clock
#define CLK_M_OSC           0x1         // Select CLK_M_OSC clock (25MHz - 40ns)
#define CM_CPTS_RFT_CLKSEL  0x520        // Selects the Mux select line for CPTS RFT clock
/***************************************************************/
// DMTIMER 5 Registers
#define DMTIMER5_START_ADDR 0x48046000
#define DMTIMER5_END_ADDR   0x48046FFF
#define DMTIMER5_SIZE       (DMTIMER5_END_ADDR - DMTIMER5_START_ADDR)

#define DMTIMER7_START_ADDR 0x4804a000
#define DMTIMER7_END_ADDR   0x4804aFFF
#define DMTIMER7_SIZE       (DMTIMER7_END_ADDR - DMTIMER7_START_ADDR)

#define DMTIMER_TCLR	0x38	// Timer Control Register
#define DMTIMER_TCRR	0X3c	// Timer Counter Register
#define DMTIMER_TLDR	0X40	// Timer Load Register
#define DMTIMER_TTGR	0X44	// Timer Trigger Register
#define DMTIMER_TMAR	0X4c	// Timer Match Register
#define DMTIMER_TCAR1	0X50	// Timer Capture_1 Register
#define DMTIMER_IRQSTATUS       0x28    // Timer Status Register (irq)
#define DMTIMER_IRQENABLE_SET   0x2C    // Timer Interrupt Enable Set Register
#define DMTIMER_IRQENABLE_CLR   0x30    // Timer Interrupt Enable Clear Register

#define DMT_OVERF_IRQ_BIT (1<<1)
#define DMT_MATCH_IRQ_BIT (1<<0)

/***************************************************************/
// GPIO Registers
/* P8_12 - GPIO44 - GPIO_1[12] */
#define GPIO1_START_ADDR    0x4804C000
#define GPIO1_END_ADDR      0x4804CFFF
#define GPIO1_SIZE          (GPIO1_END_ADDR - GPIO1_START_ADDR)

#define GPIO_OE             0x134   // GPIO output enable reister
#define GPIO_DATAOUT        0x13C   // Setting the value of the GPIO output pins
#define GPIO_44             (1<<12)



#endif /* BBONEPPS_H_ */
