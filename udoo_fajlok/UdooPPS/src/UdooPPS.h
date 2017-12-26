#ifndef UDOOPPS_H_
#define UDOOPPS_H_

/* The mapped memory size must be at least the size of
 * the virtual memory PAGESIZE of the system. (getconf PAGESIZE or PAGE_SIZE)
 * The UDOO Neo's page size is 4096 Bytes (0xfff).
 */

//#define DEBUG_1

/***************************************************************/
#define ENET_1_START_ADDR	0x02188000
#define ENET_1_END_ADDR		0x02188fff
#define ENET_1_SIZE			(ENET_1_END_ADDR - ENET_1_START_ADDR)

#define ENET_2_START_ADDR	0x021B4000
#define ENET_2_END_ADDR		0x021B7FFF
#define ENET_2_SIZE			(ENET_2_END_ADDR - ENET_2_START_ADDR)

/*
 * ENET Registers
 *
 * 0x0000 – 0x01FF: Configuration
 * 0x0200 – 0x03FF: Statistics counters
 * 0x0400 – 0x0430: IEEE 1588 Control
 * 0x0600 – 0x07FC: Capture/Compare block
 */
//0x021B_4024

#define ENET1_ECR	0x24 	// Ethernet Control Register (ENET1_ECR)
#define ENET1_ATCR	0x400	// Adjustable Timer Control Register
#define ENET1_ATVR	0x404	// Timer Value Register
#define ENET1_ATOFF	0x408	// Timer Offset Register
#define ENET1_ATPER	0x40C	// Timer Period Register
#define ENET1_ATCOR	0x410	// Timer Correction Register
#define ENET1_ATINC	0x414	// Time-Stamping Clock Period Register
//#define ENET1_ATSTMP	218_8418 // Timestamp of Last Transmitted Frame

#define ENET2_ECR	0X24	// Ethernet Control Register (ENET2_ECR)

#define ENET1_TGSR	0x604 // Timer Global Status Register
// ENET1_1588_EVENT0
//#define ENET1_TCSR0	0x608 // Timer Control Status Register
//#define ENET1_TCCR0	0x60C // Timer Compare Capture Register
// ENET1_1588_EVENT1
#define ENET1_TCSR1	0x610 // Timer Control Status Register
#define ENET1_TCCR1	0x614 // Timer Compare Capture Register

//#define TCSR0_timer_int_en	0x0000000
#define ENET_TCSR_TMODE			0x0000003C	// TMODE mask
#define ENET_TCSR_TMODE_dis		0xFFFFFFc3	// TMODE disable
#define ENET_TCSR_en_high_pulse	0x0000003C	// Pulse output high on compare for one 1588 clock cycle
#define ENET_TCSR_en_toggle		0x00000014	// Toggle output on compare
#define ENET_TCSR_en_set_clear	0x00000028	// Clear on compare, set on overflow
#define ENET_TCSR_en_sw_only	0x00000010	// Output Compare - software only
#define ENET_TCSR_clear_tf		0x00000080	// Clear the Timer Flag (input capture or output compare occurs)
#define ENET_TCSR_enable_irq	0x00000040	// Timer Interrupt Enable
#define ENET_TCSR_disable_irq	0xFFFFFFBF	// Timer Interrupt Disable


/***************************************************************/
#define IOMUXC_START_ADDR	0x020E0000
#define IOMUXC_END_ADDR		0x020E0FFF
#define IOMUXC_SIZE			(IOMUXC_END_ADDR - IOMUXC_START_ADDR)
#define IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1	0x22C // 1808. oldal Pad Mux Register
#define IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1 0x574 // 2185. oldal Pad Control Register

/***************************************************************/
#define ENET1_1588_IRQ_NUM	151	// ENET1 1588 Timer interrupt [synchronous] request


#endif /* UDOOPPS_H_ */
