#ifndef __DMTIMER_H_
#define __DMTIMER_H_

#include <stdint.h>
#include <sys/ioctl.h>
#include "RTIO_API.h"


// DMTimer5 load value (related to CPTS query period)
#ifdef TI_KERNEL
	//#warning "CPTS poll period: 700msec"
	#define DMTIMER5_LOAD_VALUE			(0xFF000000UL) 	// !! use it for TI kernels !!
#else
	//#warning "CPTS poll period: 350msec"
	#define DMTIMER5_LOAD_VALUE			(0xFF800000UL) 		// !! use it for mainline kernel with CPTS patch !!
#endif



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


// structure definitions
struct ts_channel
{
	void *ch;
	int (*read)(struct ts_channel *ch, uint64_t *buff, int len);
	int (*flush)(struct ts_channel *ch);
};

struct cpts_channel
{
	int idx;
	int fd_read;
	int fd_write;
	struct ts_channel ts_ch;
};

struct icap_channel
{
	char *dev_path;
	unsigned idx;
	int fdes;
	int32_t offset;
	uint64_t mult;
	uint64_t div;

	struct ts_channel ts_ch;
};



// timer character device interface for the timers
struct dev_if
{
	char *path;
	int fdes;
	volatile uint8_t *base;
};

// timer interface
enum timer_clk_source_t {SYSCLK=0, TCLKIN=1};

struct dmtimer
{
	char 								*name;
	char 								*dev_path;
	char 								*icap_path;
	int 								idx;

	int 								enabled;
	enum timer_clk_source_t 			clk_source;
	uint32_t 							clk_rate;
	uint32_t							load;
	uint32_t							match;
	int 								enable_oc;
	int 								enable_icap;
	int 								pin_dir; //1:in, 0:out
	// private
	struct dev_if						dev;
	struct icap_channel 				channel;
};

struct ecap_timer
{
	char 								*name;
	char 								*dev_path;
	char 								*icap_path;
	int 								idx;

	int 								enabled;
	uint32_t							event_div;
	uint8_t 							hw_fifo_size;
	// private
	struct dev_if						dev;
	struct icap_channel 				channel;

};



//adc
struct adc_buffer_t
{
	unsigned buffer_size;
	uint16_t *buffer;
	uint64_t *ts_buffer;

	uint16_t *buffer_wp;
	uint64_t *ts_buffer_wp;

// adc interface file descriptor
	void *fadc;
// timestamp interface
	struct icap_channel *ts_icap;
	int icap_period;
};


//public functions

//icap channel
int open_channel(struct icap_channel *c, char *dev_path, int idx, int bit_cnt, int mult, int div);
void close_channel(struct icap_channel *c);
int read_channel_raw(struct icap_channel *c, uint64_t *buf, unsigned len);
int channel_do_conversion(struct icap_channel *c, uint64_t *buf, unsigned len);
int read_channel(struct icap_channel *c, uint64_t *buf, unsigned len);
void flush_channel(struct icap_channel *c);
void disable_channel(struct icap_channel *c);

// dmtimer
int init_dmtimer(struct dmtimer *t);
void close_dmtimer(struct dmtimer *t);



// structures for the pps generation
extern struct timekeeper *tk;
extern volatile int rtio_quit;


#endif