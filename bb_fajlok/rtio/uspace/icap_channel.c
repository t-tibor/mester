#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>

#include <pthread.h>

#include "icap_channel.h"
#include "timekeeper.h"
#include "icap_channel.h"
#include "gpio.h"
#include "./PPS_servo/BBonePPS.h"

//#define assert(x) {if(!(x)) {fprintf(stderr,"Assertion error. File:%s, Line:%d\n",__FILE__,__LINE__); while(1) sleep(1); }}

uint32_t read32(volatile uint8_t *base, int offset)
{
	volatile uint32_t *a = (volatile uint32_t*)(base + offset);
	return *a;
}

void write32(volatile uint8_t *base, int offset, uint32_t val)
{
	volatile uint32_t *a = (volatile uint32_t*)(base + offset);
	*a = val;
}

uint16_t read16(volatile uint8_t *base, int offset)
{
	volatile uint16_t *a = (volatile uint16_t*)(base + offset);
	return *a;
}

void write16(volatile uint8_t *base, int offset, uint16_t val)
{
	volatile uint16_t *a = (volatile uint16_t*)(base + offset);
	*a = val;
}

uint8_t read8(volatile uint8_t *base, int offset)
{
	volatile uint8_t *a = (volatile uint8_t*)(base + offset);
	return *a;
}

void write8(volatile uint8_t *base, int offset, uint8_t val)
{
	volatile uint8_t *a = (volatile uint8_t*)(base + offset);
	*a = val;
}





// input capture channels
#define MAX_CHANNEL_NUM 10
struct icap_channel* channel_slot[MAX_CHANNEL_NUM] = {0};

int register_channel(struct icap_channel *c)
{
	unsigned  idx = c->idx;
	if(idx >= MAX_CHANNEL_NUM)
	{
		fprintf(stderr,"Invalid channel idx: %d.\n",idx);
		return -1;
	}

	if(channel_slot[idx] != NULL)
	{
		fprintf(stderr,"Channel %d is occupied.\n",idx);
		return -1;
	}

	channel_slot[idx] = c;
	return 0;
}
void deregister_channel(struct icap_channel *c)
{
	unsigned  idx = c->idx;
	if(idx >= MAX_CHANNEL_NUM)
	{
		fprintf(stderr,"Invalid channel idx: %d.\n",idx);
		return;
	}

	if(channel_slot[idx] != NULL  &&  channel_slot[idx] != c) 
	{
		fprintf(stderr,"Invalid deregistration request on channel %d.\n",idx);
		return;
	}

	channel_slot[idx] = NULL;
}

int open_channel(struct icap_channel *c, char *dev_path, int idx, int bit_cnt, int mult, int div)
{
	int ret;

	c->dev_path = dev_path;

	c->fdes = open(c->dev_path,O_RDWR | O_SYNC);
	if(c->fdes == -1)
	{
		fprintf(stderr,"Cannot open cdev file: %s\n",c->dev_path);
		return -1;
	}

	ret = ioctl(c->fdes,ICAP_IOCTL_CLEAR_OVF);
	ret = ioctl(c->fdes,ICAP_IOCTL_RESET_TS);
	ret = ioctl(c->fdes,ICAP_IOCTL_STORE_EN);
	ret = ioctl(c->fdes,ICAP_IOCTL_FLUSH);
	(void)ret;

	c->mult = mult;
	c->div = div;
	c->idx = idx;

	if(bit_cnt<=0)
	{
		fprintf(stderr,"Invalid bit count @ %s.\n",c->dev_path);
		close(c->fdes);
		c->fdes = -1;
		return -1;
	}

	fprintf(stderr,"Setting channel bit count to %d.\n",bit_cnt);
	ioctl(c->fdes,ICAP_IOCTL_SET_TS_BITNUM,bit_cnt);

	ret = register_channel(c);
	if(ret)
	{
		fprintf(stderr,"Channel registration failed");
		close(c->fdes);
		c->fdes = -1;
		return -1;
	}


	return 0;
}

void close_channel(struct icap_channel *c)
{
	if(c->fdes > 0)
	{
		deregister_channel(c);
		// wake up readers
		ioctl(c->fdes,ICAP_IOCTL_STORE_DIS);
		close(c->fdes);
		c->fdes = -1;
	}
}

int read_channel_raw(struct icap_channel *c, uint64_t *buf, unsigned len)
{
	int cnt;
	cnt = read(c->fdes, buf, len*sizeof(uint64_t));

	if(cnt % sizeof(uint64_t) != 0)
		fprintf(stderr,"Reading broken time stamp.\n");

	return cnt / sizeof(uint64_t);
}

int channel_do_conversion(struct icap_channel *c, uint64_t *buf, unsigned len)
{
	for(unsigned i=0;i<len;i++)
	{
		buf[i] =  buf[i] + (int64_t)(c->offset);
		buf[i] *= c->mult;
		buf[i] /= c->div;
	}
	return 0;
}
int read_channel(struct icap_channel *c, uint64_t *buf, unsigned len)
{
	int cnt;
	cnt = read(c->fdes, buf, len*sizeof(uint64_t));

	if(cnt % sizeof(uint64_t) != 0)
		fprintf(stderr,"Reading broken time stamp.\n");

	// coversion to nsec
	channel_do_conversion(c,buf,cnt/sizeof(uint64_t));

	return cnt / sizeof(uint64_t);
}

void flush_channel(struct icap_channel *c)
{
	if(c && c->fdes > 0)
		ioctl(c->fdes,ICAP_IOCTL_FLUSH);
}

void disable_channel(struct icap_channel *c)
{
	ioctl(c->fdes,ICAP_IOCTL_STORE_DIS);
}


// // timer character device interface for the timers
// struct dev_if
// {
// 	char *path;
// 	int fdes;
// 	volatile uint8_t *base;
// };

#define PAGE_SIZE 	4096
int dev_if_open(struct dev_if *dev, char *path)
{
	if(!dev) return -1;
	dev->path = path;

	dev->fdes = open(dev->path,O_RDWR | O_SYNC);
	if(dev->fdes == -1)
	{
		fprintf(stderr,"Cannot open cdev file: %s\n",dev->path);
		dev->base = NULL;
		return -1;
	}

	// mmap the register space
	dev->base = (uint8_t*)mmap(NULL,PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fdes, 0);
	if(dev->base == MAP_FAILED)	{
		fprintf(stderr,"Cannot mmap the character device %s\n",dev->path);
		dev->base = NULL;
		close(dev->fdes);
		dev->fdes = -1;
		return -1;
	}


	return 0;
}

void dev_if_close(struct dev_if *dev)
{
	if(!dev) return;

	if(dev->base != NULL)
	{
		if(munmap((void*)dev->base,PAGE_SIZE))
			fprintf(stderr,"Cannot unmap device %s.\n",dev->path);
		dev->base = NULL;
	}


	if(dev->fdes > 0)
	{
		if(close(dev->fdes))
			fprintf(stderr,"Cannot close device %s.\n",dev->path);
		dev->fdes = -1;
	}
}

// // timer interface
// enum timer_clk_source_t {SYSCLK=1, TCLKIN=2};

// struct dmtimer
// {
// 	char 								*name;
// 	char 								*dev_path;
// 	char 								*icap_path;
// 	int 								idx;

// 	uint32_t							load;
// 	uint32_t							match;
// 	int 								enable_oc;
// 	int 								enable_icap;
// 	int 								pin_dir; //1:in, 0:out
// 	// private
// 	struct dev_if						dev;
// 	struct icap_channel 				channel;
// };

// struct ecap_timer
// {
// 	char 								*name;
// 	char 								*dev_path;
// 	char 								*icap_path;
// 	int 								idx;

// 	uint32_t							event_div;
// 	uint8_t 							hw_fifo_size;
// 	// private
// 	struct dev_if						dev;
// 	struct icap_channel 				channel;

// };

#define DMTIMER_CNT 4

struct dmtimer dmt[DMTIMER_CNT] = {
	{	.name = "dmtimer4",
		.dev_path = "/dev/DMTimer4",
		.icap_path = "/dev/dmtimer4_icap",
		.idx = 4,
		.clk_source = TCLKIN,
		.load = 0,
		.match = 10,
		.enable_oc = 1,
		.enable_icap = 0,
		.pin_dir = 0
	},
	{	.name = "dmtimer5",
		.dev_path = "/dev/DMTimer5",
		.icap_path = "/dev/dmtimer5_icap",
		.idx = 5,
		.load = 0xFF800000,
		.match = 0xFFA00000,
		.enable_oc = 1,
		.enable_icap = 1,
		.pin_dir = 1
	},

	{	.name = "dmtimer6",
		.dev_path = "/dev/DMTimer6",
		.icap_path = "/dev/dmtimer6_icap",
		.idx = 6,
		.load = 0,
		.match = 0,
		.enable_oc = 0,
		.enable_icap = 1,
		.pin_dir = 1
	},

	{	.name = "dmtimer7",
		.dev_path = "/dev/DMTimer7",
		.icap_path = "/dev/dmtimer7_icap",
		.idx = 7,
		.load = 0x100000000ULL - 24000000,
		.match = 0x100000000ULL- 18000000,
		.enable_oc = 1,
		.enable_icap = 0,
		.pin_dir = 0
	}
};

struct dmtimer *dmtimer4 = &dmt[0];
struct dmtimer *trigger_timer = &dmt[1];
struct dmtimer *dmtimer6 = &dmt[2];
struct dmtimer *dmtimer7 = &dmt[3];

#define ECAP_CNT 3
struct ecap_timer ecap[ECAP_CNT] = {
	{
		.name="ecap0",
		.dev_path = "/dev/ecap0",
		.icap_path = "/dev/ecap0_icap",
		.idx = 0,
		.event_div = 1,
		.hw_fifo_size = 1
	},
	{
		.name="ecap1",
		.dev_path = "/dev/ecap1",
		.icap_path = "/dev/ecap1_icap",
		.idx = 1,
		.event_div = 1,
		.hw_fifo_size = 1
	},
	{
		.name="ecap2",
		.dev_path = "/dev/ecap2",
		.icap_path = "/dev/ecap2_icap",
		.idx = 2,
		.event_div = 1,
		.hw_fifo_size = 1
	}
};



int timer_enable_clk(struct dev_if *dev)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_STATE, TIMER_CLK_ENABLE);
}

int timer_disable_clk(struct dev_if *dev)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_STATE, TIMER_CLK_DISABLE);
}

int timer_set_clk_source(struct dev_if *dev, enum timer_clk_source_t clk)
{
	int ret;
	assert(dev);
	assert(dev->fdes > 0);

	switch(clk)
	{
		case SYSCLK:
			ret = ioctl(dev->fdes,TIMER_IOCTL_SET_CLOCK_SOURCE,TIMER_CLOCK_SOURCE_SYSCLK);
			break;
		case TCLKIN:
			ret = ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_SOURCE,TIMER_CLOCK_SOURCE_TCLKIN);
			break;

		default:
			fprintf(stderr,"Invalid clocksource: %d\n",clk);
			ret = -1;
	}
	return ret;
}

int timer_get_clk_freq(struct dev_if *dev)
{
	return ioctl(dev->fdes, TIMER_IOCTL_GET_CLK_FREQ);
}



int timer_set_icap_source(struct dev_if *dev, int source)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes,TIMER_IOCTL_SET_ICAP_SOURCE,source);
}

int get_effective_bit_cnt(uint32_t load)
{
	int bit = 0;
	while((load & 0x00000001 )== 0)
	{
		load >>= 1;
		load |= 0x80000000;
		bit ++;
	}
	if(load != 0xFFFFFFFF)
		return 0;

	return bit;
}

int init_dmtimer(struct dmtimer *t)
{
	int ret;
	uint32_t tmp;
	long rate, mult, div;
	int bit_cnt;


	ret = dev_if_open(&t->dev,t->dev_path);
	if(ret)
	{
		fprintf(stderr,"Cannot open dmtimer device interface.\n");
		return -1;
	}

	if(t->enable_icap)
	{
		rate = timer_get_clk_freq(&t->dev);
		mult = 1000000000;
		div = rate;
		while( (mult%10 == 0) && (div%10==0))
		{
			mult /= 10; div /= 10;
		}

		bit_cnt = get_effective_bit_cnt(t->load);
		if(bit_cnt <= 0)
		{
			fprintf(stderr,"Invalid bitcount @ timer %s.\n",t->dev_path);
			dev_if_close(&t->dev);
			return -1;
		}

		fprintf(stderr,"Opening channel: %s.\n",t->icap_path);
		ret = open_channel(&t->channel,t->icap_path, t->idx,bit_cnt, mult,div);
		if(ret)
		{
			fprintf(stderr,"Cannot open chanel %s.\n",t->icap_path);
			dev_if_close(&t->dev);
		}
		fprintf(stderr,"Clock rate: %lu, mult: %lu, div: %lu\n",rate,mult,div);
	}
	else
		t->channel.fdes = -1;

	// test the mapping
	tmp = read32(t->dev.base,DMTIMER_TIDR); assert(tmp == 0x4FFF1301);

	// setting clock source
	ret = timer_set_clk_source(&t->dev, t->clk_source);
	if(ret)
	{
		fprintf(stderr,"Cannot set timer clock.\n");
		close_channel(&t->channel);
		dev_if_close(&t->dev);
		return -1;
	}
	// enable timers clock
	ret = timer_enable_clk(&t->dev);
	if(ret) 
	{
		fprintf(stderr,"Cannot enable clock for %s.\n",t->dev_path);
		timer_set_clk_source(&t->dev,SYSCLK);
		close_channel(&t->channel);
		dev_if_close(&t->dev);
		return -1;
	}

	// set timer registers
	write32(t->dev.base,DMTIMER_TLDR,t->load);
	write32(t->dev.base,DMTIMER_TMAR, t->match);
	write32(t->dev.base,DMTIMER_TCRR,t->load);
	// config reg
	tmp = 0;
	tmp |= TCRR_AR | TCRR_ST | TCRR_TRG_OVF_MAT;
	if(t->enable_oc) tmp |= TCRR_CE | TCRR_PT;
	if(t->enable_icap) tmp |= TCRR_TCM_RISING;
	if(t->pin_dir) tmp |= TCRR_GPO_CFG;
	write32(t->dev.base,DMTIMER_TCLR,tmp);
	// enable interrupts
	tmp = read32(t->dev.base,DMTIMER_IRQSTATUS);
	write32(t->dev.base,DMTIMER_IRQSTATUS,tmp); // clear pending interrupts
	write32(t->dev.base, DMTIMER_IRQENABLE_SET, OVF_IT_FLAG | TCAR_IT_FLAG);
	return 0;
}

void close_dmtimer(struct dmtimer *t)
{
	// stop the timer and disable interrupts
	uint32_t cntrl;
	if(t->dev.base)
	{
		cntrl = read32(t->dev.base,DMTIMER_TCLR);
		cntrl &= ~(TCRR_ST);
		write32(t->dev.base,DMTIMER_TCLR,cntrl);
		write32(t->dev.base,DMTIMER_IRQENABLE_CLR,0xFF);
	}
	timer_set_clk_source(&t->dev,SYSCLK);
	timer_disable_clk(&t->dev);

	close_channel(&t->channel);
	dev_if_close(&t->dev);
}

int dmtimer_pwm_apply_offset(struct dmtimer *t, int32_t offset)
{
	uint32_t cntr = read32(t->dev.base,DMTIMER_TCRR);
	write32(t->dev.base,DMTIMER_TCRR,(uint32_t)((int32_t)(cntr) + offset));
	return 0;
}


int dmtimer_pwm_set_period(struct dmtimer *t, uint32_t period)
{
	uint32_t ld;
	ld = 0xFFFFFFFF - period;
	ld ++;

	write32(t->dev.base,DMTIMER_TLDR,ld);
	return 0;
}

int init_ecap(struct ecap_timer *e)
{
	uint32_t tmp;
	int ret;
	long rate, mult, div;

	ret = dev_if_open(&e->dev,e->dev_path);
	if(ret) 
	{
		fprintf(stderr,"Cannot open ecap device interface.");
		return -1;
	}

	rate = timer_get_clk_freq(&e->dev);
	mult = 1000000000;
	div = rate;
	while( (mult%10 == 0) && (div%10==0))
	{
		mult /= 10; div /= 10;
	}


	fprintf(stderr,"Opening channel: %s.\n",e->icap_path);
	ret = open_channel(&e->channel,e->icap_path, e->idx,32, mult,div);
	if(ret)
	{
		fprintf(stderr,"Cannot open chanel %s.\n",e->icap_path);
		dev_if_close(&e->dev);
	}
	fprintf(stderr,"Clock rate: %lu, mult: %lu, div: %lu\n",rate,mult,div);


	// test the mapping
	tmp = read32(e->dev.base,ECAP_REVID); assert(tmp == 0x44D22100);
	// enable timers clock
	ret = timer_enable_clk(&e->dev); 
	if(ret)
	{
		fprintf(stderr,"Cannot enable ecap clk.\n");
		close_channel(&e->channel);
		dev_if_close(&e->dev);
		return -1;
	}

	// clear interrupts
	write16(e->dev.base,ECAP_ECCLR,0xFFFF);
	

	write32(e->dev.base,ECAP_TSCTR,0x00);
	assert((e->event_div == 1) || (e->event_div % 2) == 0);
	assert(e->event_div-1 < 62);
	write16(e->dev.base,ECAP_ECCTL1,ECCTL1_CAPLDEN |( (uint16_t)(e->event_div/2) << ECCTL1_PRESCALE_OFFSET));

	assert((e->hw_fifo_size >0) && (e->hw_fifo_size <=4));
	write16(e->dev.base,ECAP_ECCTL2, ECCTL2_TSCTRSTOP | ((e->hw_fifo_size-1)<<ECCTL2_STOP_WRAP_OFFSET)); //use all the 4 registers


	write16(e->dev.base,ECAP_ECEINT,ECEINT_CNTOVF | (ECEINT_CEVT1 << (e->hw_fifo_size-1))); // enable interrupt corresponding to fifo size
	return 0;
}

void close_ecap(struct ecap_timer *e)
{
	// stop the timer and disable interrupts
	write32(e->dev.base,ECAP_ECCTL1,0);
	write32(e->dev.base, ECAP_ECCTL2,6);
	write16(e->dev.base,ECAP_ECEINT,0x0000);

	close_channel(&e->channel);
	dev_if_close(&e->dev);
}


void close_timers()
{
	for(int i=0;i<DMTIMER_CNT;i++)
		close_dmtimer(&dmt[i]);

	for(int i=0;i<ECAP_CNT;i++)
		close_ecap(&ecap[i]);
}
int init_timers()
{
	int err1=0, err2=0;

	// init dmtimers
	for(int i=0;i<DMTIMER_CNT && !err1;i++)
		err1 = init_dmtimer(&dmt[i]);

	// init ecap timers
	for(int i=0;i<ECAP_CNT && !err2;i++)
		err2 = init_ecap(&ecap[i]);

	if(err1 || err2)
	{
		close_timers();
		return -1;
	}

	return 0;
}


// <------------------------------------------------------------------------------>
// <------------------------------- CPTS interface ------------------------------->
// <------------------------------------------------------------------------------>

#include "ptp_clock.h"

#define PTP_DEVICE					"/dev/ptp0"
#define TIMER5_HWTS_PUSH_INDEX		1 // HW2_TS_PUSH-1	

int ptp_fdes = 0;

int ptp_open()
{
	if(ptp_fdes <= 0)
	{
		if((ptp_fdes = open(PTP_DEVICE, O_RDWR | O_SYNC)) < 0)
		{
			fprintf(stderr, "Opening %s PTP device: %s\n", PTP_DEVICE, strerror(errno));
			return -1;
		}
	}
	return 0;
}

int ptp_enable_hwts()
{
	struct ptp_extts_request extts_request;

	// enable hardware timestamp generation for timer5
	memset(&extts_request, 0, sizeof(extts_request));
	extts_request.index = TIMER5_HWTS_PUSH_INDEX;
	extts_request.flags = PTP_ENABLE_FEATURE;

	if(ioctl(ptp_fdes, PTP_EXTTS_REQUEST, &extts_request))
	{
	   fprintf(stderr,"Cannot enable timer5 hardware timestamp requests.\n");
	   close(ptp_fdes);
	   return -1;
	}
	return 0;
}

int ptp_disable_hwts()
{
	struct ptp_extts_request extts_request;

	// enable hardware timestamp generation for timer5
	memset(&extts_request, 0, sizeof(extts_request));
	extts_request.index = TIMER5_HWTS_PUSH_INDEX;
	extts_request.flags = 0;

	if(ioctl(ptp_fdes, PTP_EXTTS_REQUEST, &extts_request))
	{
	   fprintf(stderr,"Cannot disable Timer 5 HWTS generation.\n");
	   return -1;
	}
	return 0;
}

void ptp_close()
{
	ptp_disable_hwts();
	close(ptp_fdes);
}


int ptp_get_ts(struct timespec *ts)
{
	struct ptp_extts_event extts_event[10];
	int cnt;

	cnt = read(ptp_fdes, extts_event, 2*sizeof(struct ptp_extts_event));
	if(cnt < 0)
	{
		fprintf(stderr,"PTP read error: %s\n",strerror(errno));
		return -1;
	}
	if(cnt == 0)
	{
		return 0;
	}
	else if(cnt > (int)sizeof(struct ptp_extts_event))
	{
		fprintf(stderr,"PTP timestamp accumulation.\n");
	}
	cnt /= sizeof(struct ptp_extts_event);
	cnt--;

	if(cnt < 0 ) return -1;

	ts->tv_sec = extts_event[cnt].t.sec;
	ts->tv_nsec = extts_event[cnt].t.nsec;

	return 1;
}


int quit = 0;

struct timekeeper *tk;
// workers

void *timekeeper_worker(void *arg)
{
	struct timekeeper *tk = (struct timekeeper*)arg;
	uint64_t next_ts, ts_period;
	struct timespec tspec;
	uint64_t ptp_ts;
	uint64_t hwts;
	// int pres = 0;


	if(ptp_open())
	{
		fprintf(stderr,"Cannot open PTP device.\n");
		return NULL;
	}

	next_ts = ts_period = (0xFFFFFFFF - trigger_timer->load)+1;


	// enable timer HW_TS push events
	ptp_enable_hwts();

	while(!quit)
	{
		// calc hwts times from timer 5 settings
		ptp_get_ts(&tspec);
		// if(pres == 0)
		// 	printf("PTP time: %ld sec, %lunsec\n",tspec.tv_sec, tspec.tv_nsec);
		// pres = (pres+1) & 0x3;

		ptp_ts = ((uint64_t)tspec.tv_sec*1000000000ULL)+tspec.tv_nsec;

		hwts = next_ts;
		channel_do_conversion(&trigger_timer->channel,&hwts,1);
		// send the new timestamp to the timekeeper
		timekeeper_add_sync_point(tk, hwts, ptp_ts);

		next_ts += ts_period;
	}

	ptp_close();

	return NULL;
}


void *channel_logger(void *arg)
{
	struct icap_channel *ch = (struct icap_channel*)arg;
	int channel_idx = ch->idx;
	FILE *log;
	char fname[256];
	uint64_t ts[16], ts2[16];
	int rcvCnt;

	sprintf(fname,"./icap_channel_%d.log",channel_idx);
	log = fopen(fname,"w");
	if(!log)
	{
		fprintf(stderr,"Cannot open channel log: %s\n",fname);
		return NULL;
	}

	while(!quit)
	{
		rcvCnt = read_channel(ch,ts,16);
		memcpy(ts2,ts,sizeof(uint64_t) *16);
		timekeeper_convert(tk,ts,rcvCnt);
		for(int i=0;i<rcvCnt;i++)
		{
			fprintf(log,"Local:%" PRIu64 ", Global:%" PRIu64 "\n",ts2[i],ts[i]);
			fprintf(stdout,"CH%d - Local:%" PRIu64 ", Global:%lf sec, %llunsec\n",ch->idx,ts2[i],(ts[i]/1e9),ts[i]%1000000000);
		}
	}

	
	fclose(log);
	return NULL;	
}


// Base on LinuxPTP servo
void *pps_worker(void *arg)
{
	struct PPS_servo_t *data = (struct PPS_servo_t*)arg;
	struct icap_channel *ch = data->feedback_channel;
	struct servo *s;
	enum servo_state state;
	int rcvCnt;
	uint64_t ts[16];

	uint32_t timer_period = 24000000;
	uint32_t timer_period_prev = 24000000;

	uint64_t base;
	int64_t fraction;
	int64_t offset;
	double adj;
	double period_delta;


	s = servo_create(CLOCK_SERVO_PI, 0, MAX_FREQUENCY,0);
	if(!s)
	{
		fprintf(stderr,"Cannot create PPS servo\n");
		return NULL;
	}
	
	// init servo
	state = SERVO_UNLOCKED;
	servo_sync_interval(s, 1); //set interval for 1 sec
	servo_reset(s);


	flush_channel(ch);
	// get the first ts, this will be the starting time

do
{
	rcvCnt = read_channel(ch, ts,16);
	if(rcvCnt <= 0)
		break; // channel closed, exit

	base = ts[rcvCnt-1]; 
	
	// rouding base to the closest whole second
	// offset: distance from the closest whole second
	fraction = base % 1000000000ULL;
	if(fraction > 500000000LL)
	{
		base = base + 1000000000 - fraction;
		offset = 1000000000 - fraction;
	}
	else
	{
		base -= fraction;
		offset = -fraction;
	}

// LINUXPTP servo
	while(!quit)
	{
		fprintf(stderr,"PPS base: %"PRIu64", PPS offset: %"PRId64"\n",base,offset);
		// feed data into the servo
		adj = servo_sample(s,offset,base,1,&state);

		fprintf(stderr,"\tServo state: %d\n",state);

		switch (state) {
		case SERVO_UNLOCKED:
			break;
		case SERVO_JUMP:
			dmtimer_pwm_apply_offset(data->pwm_gen, -offset/42);
			fprintf(stderr,"\tApplying pwm offset: %"PRId64"\n",-offset);

			period_delta = 24000000*adj*1e-9;
			fprintf(stderr,"\tPWM period delta: %lf\n",period_delta);			
			timer_period = 24000000 + period_delta;
			if(timer_period != timer_period_prev)
			{
				dmtimer_pwm_set_period(data->pwm_gen, timer_period);
				timer_period_prev = timer_period;
			}
			break;
		case SERVO_LOCKED:
			period_delta = 24000000*adj*1e-9;
			fprintf(stderr,"\tadj:%lf PWM period delta: %lf\n",adj,period_delta);			
			timer_period = 24000000 / (1-adj*1e-9);
			if(timer_period != timer_period_prev)
			{
				dmtimer_pwm_set_period(data->pwm_gen, timer_period);
				timer_period_prev = timer_period;
			}
			break;
		}
		


		base += 1000000000;

		rcvCnt = read_channel(ch, ts,1);
		if(rcvCnt <= 0)
			break; // channel closed, exit


		fprintf(stderr,"\nPTP :%"PRIu64",sec %"PRIu64"nsec\n",ts[0]/1000000000, ts[0]%1000000000);	

		offset = base - ts[0];
	}

}while(0);

	servo_destroy(s);
	return NULL;
}

/*
void *MyBBonePPS_worker(void *arg)
{
	struct PPS_servo_t *data = (struct PPS_servo_t*)arg;
	struct icap_channel *ch = data->feedback_channel;
	struct BBonePPS_t *s;
	int rcvCnt;
	uint64_t ts[16];


	uint64_t base;
	int64_t fraction;
	int64_t offset;

	s = BBonePPS_create();
	if(!s)
	{
		fprintf(stderr,"Cannot create PPS servo\n");
		return NULL;
	}
	

	flush_channel(ch);
	// get the first ts, this will be the starting time

do
{
	rcvCnt = read_channel(ch, ts,16);
	if(rcvCnt <= 0)
		break; // channel closed, exit

	base = ts[rcvCnt-1]; 
	
	// rouding base to the closest whole second
	// offset: distance from the closest whole second
	fraction = base % 1000000000ULL;
	if(fraction > 500000000LL)
	{
		base = base + 1000000000 - fraction;
		offset = fraction-1000000000;
	}
	else
	{
		base -= fraction;
		offset = fraction;
	}

	while(!quit)
	{
		// feed data into the servo
		BBonePPS_sample(s, offset);


		switch (s->state) {
		case SERVO_OFFSET_JUMP:

			dmtimer_pwm_apply_offset(data->pwm_gen, offset/42);
			fprintf(stderr,"\tApplying pwm offset: %"PRId64"\n",-offset);
			
			break;
		default:
			dmtimer_pwm_set_period(data->pwm_gen, s->period);
			break;
		}
		


		base += 1000000000;

		rcvCnt = read_channel(ch, ts,1);
		if(rcvCnt <= 0)
			break; // channel closed, exit


		//fprintf(stderr,"\nPTP :%"PRIu64",sec %"PRIu64"nsec\n",ts[0]/1000000000, ts[0]%1000000000);	

		offset = ts[0] - base;
	}
}while(0);

	BBonePPS_destroy(s);
	return NULL;
}
*/


void *pps_servo_worker(void *arg);



void signal_handler(int sig)
{
	printf("Exiting...\n");
	(void)sig;
	quit = 1;
}

#define CHANNEL_NUM 10
#define SYNC_OFFSET 1000000 // 1ms
#define TIMEKEEPER_LOG_NAME	"./timekeeper.log"


int main()
{
	int err = 0;
	//pthread_t timkeeper_thread_id;
	//pthread_t channel3_logger;
	// pthread_t channel4_logger;
	pthread_t pps_servo;

	uint64_t ts[CHANNEL_NUM];
	double trigger_period;
	struct PPS_servo_t pps;


	// install signal handler for exit handling
	signal(SIGINT, signal_handler);


	// create timekeeper
	trigger_period = (double)((0xFFFFFFFF - trigger_timer->load)+1) * 1000 / 24 / 1e9 ;
	tk = timekeeper_create(0,trigger_period, SYNC_OFFSET, TIMEKEEPER_LOG_NAME);
	if(!tk)
	{
		fprintf(stderr,"Cannot create timekeeper.\n");
		return -1;
	}


	// init timers
	err = init_timers();
	if(err)
	{
		timekeeper_destroy(tk);
		fprintf(stderr,"Cannot open hw timers. Exiting...");
		return -1;
	}

	// Initial offset cancelation
	printf("Offset cancellation.\n");

	init_sync_gpio();
	//change timer event source to gpio interrupt
	for(int i=0;i<DMTIMER_CNT;i++)
		timer_set_icap_source(&dmt[i].dev,17);
	for(int i=0;i<ECAP_CNT;i++)
		timer_set_icap_source(&ecap[i].dev,17);


	for(int i=0;i<CHANNEL_NUM;i++)
		if(channel_slot[i])
			flush_channel(channel_slot[i]);

	trigger_sync_gpio();
	close_sync_gpio();

	
	printf("Sync point timestamps:\n");
	for(int i=0;i<CHANNEL_NUM;i++)
		if(channel_slot[i])
		{
			read_channel_raw(channel_slot[i],&ts[i],1);
			printf("%s - 0x%016llx\n",channel_slot[i]->dev_path, ts[i]);
			channel_slot[i]->offset = -ts[i];
		}
	printf("Offset cancellation ready.\n");


	//change timer event source to input pins
	for(int i=0;i<DMTIMER_CNT;i++)
		timer_set_icap_source(&dmt[i].dev,0);
	for(int i=0;i<ECAP_CNT;i++)
		timer_set_icap_source(&ecap[i].dev,0);
	// timer_set_icap_source(&dmt[0].dev,17);


	//start channel loggers
	//err = pthread_create(&channel3_logger, NULL, channel_logger, (void*)&dmt[0].channel);
	//if(err)
//		fprintf(stderr,"Cannot start the dmtimer5 logger.\n");

	// err = pthread_create(&channel4_logger, NULL, channel_logger, (void*)&dmt[2].channel);
	// if(err)
	// 	fprintf(stderr,"Cannot start the dmtimer6 logger.\n");

	// start pps servo
	pps.feedback_channel = &dmt[1].channel;	// timer5
	pps.pwm_gen = &dmt[3];							// timer7

	err = pthread_create(&pps_servo, NULL, pps_servo_worker, (void*)&pps);
	if(err)
		fprintf(stderr,"Cannot start the PPS servo thread.\n");



	// do the timekeeper work
	timekeeper_worker((void*)tk);

	// signal receivd to stop
	// close channels to wake up thre reader threads
	for(int i=0;i<CHANNEL_NUM;i++)
		if(channel_slot[i])
			disable_channel(channel_slot[i]);


	fprintf(stderr,"Waiting for the reader threads.\n");
	void *ret;
	(void)ret;
	//pthread_join(channel3_logger,&ret);
	// pthread_join(channel4_logger,&ret);


	fprintf(stderr,"Closing timers.\n");	
	close_timers();

return 0;
}