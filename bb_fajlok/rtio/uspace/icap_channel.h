#ifndef __DMTIMER_H_
#define __DMTIMER_H_

#include <sys/ioctl.h>


// dmtimer registers
#define DMTIMER_TIDR				0x00
#define DMTIMER_TIOCP				0x10
#define DMTIMER_IRQ_EOI				0x20
#define DMTIMER_IRQSTATUS_RAW		0x24
#define DMTIMER_IRQSTATUS			0x28
#define 	TCAR_IT_FLAG			(1<<2)
#define 	OVF_IT_FLAG				(1<<1)
#define 	MAT_IT_FLAG				(1<<0)
#define DMTIMER_IRQENABLE_SET 		0x2C
#define DMTIMER_IRQENABLE_CLR		0x30
#define DMTIMER_TCLR				0x38
#define DMTIMER_TCRR				0x3c
#define 	TCRR_ST					(1<<0)
#define		TCRR_AR					(1<<1)
#define 	TCRR_CE					(1<<6)
#define 	TCRR_TCM_RISING			(1<<8)
#define 	TCRR_TCM_FALL			(1<<9)
#define 	TCRR_TRG_OVF			(1<<10)
#define 	TCRR_TRG_OVF_MAT		(1<<11)
#define 	TCRR_PT 				(1<<12)
#define  	TCRR_CAPT_MODE			(1<<13)
#define 	TCRR_GPO_CFG			(1<<14)
#define DMTIMER_TLDR				0x40
#define DMTIMER_TTGR				0x44
#define DMTIMER_TWPS				0x48
#define DMTIMER_TMAR				0x4c

#define TIMER_TCAR1_OFFSET			0x50
#define DMTIMER_TSICR				0x54
#define 	TSICR_POSTED			(1<<2)
#define TIMER_TCAR2_OFFSET			0x58

// ecap registers
#define ECAP_BASE		0X100

#define ECAP_TSCTR		ECAP_BASE+0x00
#define ECAP_CTRPHS		ECAP_BASE+0x04
#define ECAP_CAP1		ECAP_BASE+0x08
#define ECAP_CAP2		ECAP_BASE+0x0c
#define ECAP_CAP3		ECAP_BASE+0x10
#define ECAP_CAP4		ECAP_BASE+0x14
#define ECAP_ECCTL1		ECAP_BASE+0x28
#define  	ECCTL1_PRESCALE_OFFSET		9
#define 	ECCTL1_CAPLDEN				(1<<8)
#define ECAP_ECCTL2		ECAP_BASE+0x2A
#define		ECCTL2_CONT_ONESHT			(1<<0)
#define 	ECCTL2_STOP_WRAP_OFFSET		1
#define		ECCTL2_RE_ARM				(1<<3)
#define 	ECCTL2_TSCTRSTOP			(1<<4)
#define ECAP_ECEINT		ECAP_BASE+0x2C
#define 	ECEINT_CNTOVF				(1<<5)
#define 	ECEINT_CEVT4				(1<<4)
#define 	ECEINT_CEVT3				(1<<3)
#define 	ECEINT_CEVT2				(1<<2)
#define 	ECEINT_CEVT1				(1<<1)
#define ECAP_ECFLG		ECAP_BASE+0x2E
#define ECAP_ECCLR		ECAP_BASE+0x30
#define ECAP_ECFRC		ECAP_BASE+0x32
#define ECAP_REVID		ECAP_BASE+0x5C


// timer ioctl operations
#define TIMER_IOCTL_MAGIC	'-'
#define TIMER_IOCTL_SET_CLOCK_STATE		_IO(TIMER_IOCTL_MAGIC,1)
	#define TIMER_CLK_DISABLE		0x00
	#define TIMER_CLK_ENABLE		0x01
#define TIMER_IOCTL_SET_CLOCK_SOURCE 		_IO(TIMER_IOCTL_MAGIC,2) // not implented for ecap timers
	#define TIMER_CLOCK_SOURCE_SYSCLK		0x01
	#define	TIMER_CLOCK_SOURCE_TCLKIN		0x02
#define TIMER_IOCTL_SET_ICAP_SOURCE 		_IO(TIMER_IOCTL_MAGIC,3)
#define TIMER_IOCTL_GET_CLK_FREQ			_IO(TIMER_IOCTL_MAGIC,4)


// input capture channel ioctl operations

#define ICAP_IOCTL_MAGIC	'9'
#define ICAP_IOCTL_GET_OVF			_IO(ICAP_IOCTL_MAGIC,1)
#define ICAP_IOCTL_CLEAR_OVF		_IO(ICAP_IOCTL_MAGIC,2)
#define ICAP_IOCTL_RESET_TS			_IO(ICAP_IOCTL_MAGIC,3)
#define ICAP_IOCTL_SET_TS_BITNUM	_IO(ICAP_IOCTL_MAGIC,4)
#define ICAP_IOCTL_STORE_EN			_IO(ICAP_IOCTL_MAGIC,5)
#define ICAP_IOCTL_STORE_DIS		_IO(ICAP_IOCTL_MAGIC,6)
#define ICAP_IOCTL_FLUSH			_IO(ICAP_IOCTL_MAGIC,7)



#endif