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

#include "icap_channel.h"
#include "timekeeper.h"
#include "gpio.h"
#include "PPS_servo/PPS_servo.h"
#include "adc.h"
#include "circ_buf.h"
#include "utils.h"

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



// cpts timestamp channels
struct cpts_channel cpts_channels[4];

int ts_channel_cpts_read(struct ts_channel *ts_ch, uint64_t *buf, int len);
int ts_channel_cpts_flush(struct ts_channel *ts_ch);
int ts_channel_icap_read(struct ts_channel *ts_ch, uint64_t *buf, int len);
int ts_channel_icap_flush(struct ts_channel *ts_ch);


int create_cpts_channels()
{
	int pipefd[2];

	for(int i=0;i<4;i++)
	{
		cpts_channels[i].idx = i+4;
	    if (pipe(pipefd) == -1) 
	    {
	        fprintf(stderr,"Cannot create pipe for CPTS channel %d.\n",i+4);
	        return -1;
	    }
	    cpts_channels[i].fd_read = pipefd[0];
	    cpts_channels[i].fd_write = pipefd[1];
	    // make write side nonblocking
	    if(fcntl(pipefd[1],F_SETFL, O_NONBLOCK))
	    	fprintf(stderr,"Cannot set O_NONBLOCK flag on the pipe.\n");

	    cpts_channels[i].ts_ch.ch = &cpts_channels[i];
	    cpts_channels[i].ts_ch.read = ts_channel_cpts_read;
	    cpts_channels[i].ts_ch.flush = ts_channel_cpts_flush;

	}
	return 0;
}

int close_cpts_channels()
{
	for(int i=0;i<4;i++)
	{
		if(cpts_channels[i].fd_read)
			close(cpts_channels[i].fd_read);

		if(cpts_channels[i].fd_write)
			close(cpts_channels[i].fd_write);

		cpts_channels[i].fd_read = 0;
		cpts_channels[i].fd_write = 0;
	}
	return 0;
}

int cpts_channel_write(struct cpts_channel *ch, uint64_t buf)
{
	write(ch->fd_write, &buf, sizeof(uint64_t));
	return 0;
}

int cpts_channel_read(struct cpts_channel *ch, uint64_t *buf, int len)
{
	int count;

	count = read(ch->fd_read, buf, len * sizeof(uint64_t));
	if( (count % sizeof(uint64_t)) != 0 )
		fprintf(stderr,"Invalid timestamp length from the pipe %d.\n",ch->idx);

	return count / sizeof(uint64_t);
}

int cpts_disable_channels()
{
	for(int i=0;i<4;i++)
	{
		struct cpts_channel *ch = &cpts_channels[i];
		if(ch->fd_write)
		{
			close(ch->fd_write);
			ch->fd_write = 0;
		}
	}
	return 0;
}

// input capture channels
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

	c->ts_ch.ch = c;
	c->ts_ch.read = ts_channel_icap_read;
	c->ts_ch.flush = ts_channel_icap_flush;

	return 0;
}

void close_channel(struct icap_channel *c)
{
	if(c->fdes > 0)
	{
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


// generic timestamp channel
int ts_channel_icap_read(struct ts_channel *ts_ch, uint64_t *buf, int len)
{
	int cnt;
	struct icap_channel *icap = (struct icap_channel*)ts_ch->ch;

	cnt = read_channel(icap, buf,len);
	timekeeper_convert(tk, buf, cnt);
	return cnt;
}

int ts_channel_icap_flush(struct ts_channel *ts_ch)
{
	struct icap_channel *icap = (struct icap_channel*)ts_ch->ch;

	flush_channel(icap);
	return 0;
}

int ts_channel_cpts_read(struct ts_channel *ts_ch, uint64_t *buf, int len)
{
	int cnt;
	struct cpts_channel *cch = (struct cpts_channel*)ts_ch->ch;

	cnt =  cpts_channel_read(cch, buf,len);
	return cnt;
}

int ts_channel_cpts_flush(struct ts_channel *ts_ch)
{
	(void)ts_ch;
	return 0;
}

int ts_channel_read(struct ts_channel *ts, uint64_t *buffer, int size)
{
	if(!ts) return -1;
	return ts->read(ts,buffer,size);
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


#define MAX_TIMER_NUM 10
struct icap_channel *icap_channels[MAX_TIMER_NUM] = {0};
struct dev_if *icap_dev_ifs[MAX_TIMER_NUM] = {0};
int icap_count = 0;

struct dmtimer dmts[4]= {
	{
		.name = "dmtimer4",
		.dev_path = "/dev/DMTimer4",
		.idx = 4
	},
	{ 
		.name = "dmtimer5",
		.dev_path = "/dev/DMTimer5",
		.icap_path = "/dev/dmtimer5_icap",
		.idx = 5,
		.load = DMTIMER5_LOAD_VALUE,
		.match = (DMTIMER5_LOAD_VALUE + 0x200000),
		.enabled = 1,
		.enable_oc = 1,
		.enable_icap = 1,
		.pin_dir = 1
	},
	{
		.name = "dmtimer6",
		.dev_path = "/dev/DMTimer6",
		.icap_path = "/dev/dmtimer6_icap",
		.idx = 6
	},
	{
		.name = "dmtimer7",
		.dev_path = "/dev/DMTimer7",
		.icap_path = "/dev/dmtimer7_icap",
		.idx = 7
	}
};

struct dmtimer *trigger_timer = &dmts[1];

struct ecap_timer ecaps[3] = {
	{
		.name="ecap0",
		.dev_path = "/dev/ecap0",
		.icap_path = "/dev/ecap0_icap",
		.idx = 0,
	},
		{
		.name="ecap1",
		.dev_path = "/dev/ecap1",
		.icap_path = "/dev/ecap1_icap",
		.idx = 0,
		.enabled=0,
	},

	{
		.name="ecap2",
		.dev_path = "/dev/ecap2",
		.icap_path = "/dev/ecap2_icap",
		.idx = 2,
	}
};





int timer_enable_clk(struct dev_if *dev)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_STATE, TIMER_CLK_ENABLE);
}
int dmtimer_enable_clk(struct dmtimer *timer) { return timer_enable_clk(&timer->dev);}
int ecap_enable_clk(struct ecap_timer*timer) { return timer_enable_clk(&timer->dev);}

int timer_disable_clk(struct dev_if *dev)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_STATE, TIMER_CLK_DISABLE);
}
int dmtimer_disable_clk(struct dmtimer *timer) { return timer_disable_clk(&timer->dev);}
int ecap_disable_clk(struct ecap_timer*timer) { return timer_disable_clk(&timer->dev);}

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
int dmtimer_set_clk_source(struct dmtimer *timer, enum timer_clk_source_t clk) { return timer_set_clk_source(&timer->dev,clk);}
int ecap_set_clk_source(struct ecap_timer*timer, enum timer_clk_source_t clk) { return timer_set_clk_source(&timer->dev,clk);}

int timer_get_clk_freq(struct dev_if *dev)
{
	return ioctl(dev->fdes, TIMER_IOCTL_GET_CLK_FREQ);
}
int dmtimer_get_clk_freq(struct dmtimer *timer) { return timer_get_clk_freq(&timer->dev);}
int ecap_get_clk_freq(struct ecap_timer*timer) { return timer_get_clk_freq(&timer->dev);}


int timer_set_icap_source(struct dev_if *dev, int source)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes,TIMER_IOCTL_SET_ICAP_SOURCE,source);
}
int dmtimer_set_icap_source(struct dmtimer *timer, int source) { return timer_set_icap_source(&timer->dev,source);}
int ecap_set_icap_source(struct ecap_timer*timer, int source) { return timer_set_icap_source(&timer->dev,source);}

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
	long mult, div;
	int bit_cnt;

	if(t->enabled == 0)
	{
		fprintf(stderr,"%s is not used.\n",t->name);
		return 0;
	}

	fprintf(stderr,"Initializing %s.\n",t->name);

	ret = dev_if_open(&t->dev,t->dev_path);
	if(ret)
	{
		fprintf(stderr,"Cannot open dmtimer device interface.\n");
		return -1;
	}

	t->clk_rate = timer_get_clk_freq(&t->dev);

	if(t->enable_icap)
	{
		mult = 1000000000;
		div = t->clk_rate;
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
		fprintf(stderr,"Clock rate: %u, mult: %lu, div: %lu\n",t->clk_rate,mult,div);
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
	if(t->enabled==0)
		return;

	fprintf(stderr,"Closing %s.\n",t->name);

	if(t->dev.base)
	{
		cntrl = read32(t->dev.base,DMTIMER_TCLR);
		cntrl &= ~(TCRR_ST);
		write32(t->dev.base,DMTIMER_TCLR,cntrl);
		write32(t->dev.base,DMTIMER_IRQENABLE_CLR,0xFF);
	
		timer_set_clk_source(&t->dev,SYSCLK);
		timer_disable_clk(&t->dev);
	}

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

int dmtimer_pwm_set_duty(struct dmtimer *t, uint32_t period, uint32_t duty)
{
	uint32_t ld, match;
	ld = 0xFFFFFFFF - period;
	ld ++;
	match = ld + (period*duty/100);

	write32(t->dev.base, DMTIMER_TMAR, match);
	return 0;
}

int dmtimer_pwm_setup(struct dmtimer *t, uint32_t period, uint32_t duty)
{
	uint32_t ld, match;
	ld = 0xFFFFFFFF - period;
	ld ++;

	match = ld + (period*duty/100);

	write32(t->dev.base, DMTIMER_TLDR, ld);
	write32(t->dev.base, DMTIMER_TMAR, match);
	write32(t->dev.base, DMTIMER_TCRR, ld);
	return 0;
}

int dmtimer_set_cntr(struct dmtimer *t, uint32_t val)
{
	write32(t->dev.base, DMTIMER_TCRR,val);
	return 0;
}

int dmtimer_set_pin_dir(struct dmtimer *t, uint8_t dir)
{
	uint32_t ctrl;

	ctrl = read32(t->dev.base, DMTIMER_TCLR);
	if(1 == dir)
		ctrl |= ((uint32_t)TCRR_GPO_CFG);
	else if(0 == dir)
		ctrl &= ~((uint32_t)TCRR_GPO_CFG);
	else
		return -1;

	write32(t->dev.base, DMTIMER_TCLR, ctrl);

	return 0;
}

void dmtimer_start_timer(struct dmtimer *t)
{
	uint32_t reg;
	reg = read32(t->dev.base, DMTIMER_TCLR);
	write32(t->dev.base, DMTIMER_TCLR, reg | (uint32_t)TCRR_ST);
}

void dmtimer_stop_timer(struct dmtimer *t)
{
	uint32_t reg;
	reg = read32(t->dev.base, DMTIMER_TCLR);
	write32(t->dev.base, DMTIMER_TCLR, reg & (~(uint32_t)TCRR_ST));
}

void dmtimer_set_oc(struct dmtimer *t, int on)
{
	uint32_t reg;
	reg = read32(t->dev.base, DMTIMER_TCLR);
	if(on)
		reg |= TCRR_CE | TCRR_PT;
	else
		reg &= ~((uint32_t)(TCRR_CE | TCRR_PT));

	write32(t->dev.base, DMTIMER_TCLR, reg);
}

void dmtimer_set_icap(struct dmtimer *t, int on)
{
	uint32_t reg;
	reg = read32(t->dev.base, DMTIMER_TCLR);
	if(on)
		reg |= TCRR_TCM_RISING;
	else
		reg &= ~((uint32_t)(TCRR_TCM_RISING | TCRR_TCM_FALL));

	write32(t->dev.base, DMTIMER_TCLR, reg);
}


int init_ecap(struct ecap_timer *e)
{
	uint32_t tmp;
	int ret;
	long rate, mult, div;

	if(e->enabled == 0)
	{
		fprintf(stderr,"%s is not used.\n",e->name);
		return 0;
	}

	fprintf(stderr,"Initializing %s.\n",e->name);

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
	if(e->enabled==0)
		return;

	fprintf(stderr,"Closing %s.\n",e->name);

	// stop the timer and disable interrupts
	if(e->dev.base)
	{
		write32(e->dev.base,ECAP_ECCTL1,0);
		write32(e->dev.base, ECAP_ECCTL2,6);
		write16(e->dev.base,ECAP_ECEINT,0x0000);
	}

	close_channel(&e->channel);
	dev_if_close(&e->dev);
}

int ecap_set_prescaler(struct ecap_timer *e, unsigned char presc)
{
	uint32_t reg;

	if(!e || !e->enabled || presc==0 || presc>62 || (presc%2==1 && presc!=1))
		return -1;

	reg = read32(e->dev.base, ECAP_ECCTL1);
	reg &= ~((uint32_t) 0x3e00);
	reg |= ((uint16_t)(presc/2)) << ECCTL1_PRESCALE_OFFSET;
	write32(e->dev.base,ECAP_ECCTL1,reg);
	return 0;
}


// <------------------------------------------------------------------------------>
// <------------------------------- CPTS interface ------------------------------->
// <------------------------------------------------------------------------------>

#include "PPS_servo/ptp_clock.h"

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

int ptp_enable_hwts(int ch)
{
	struct ptp_extts_request extts_request;

	// enable hardware timestamp generation
	memset(&extts_request, 0, sizeof(extts_request));
	extts_request.index = ch;
	extts_request.flags = PTP_ENABLE_FEATURE;

	if(ioctl(ptp_fdes, PTP_EXTTS_REQUEST, &extts_request))
	{
	   fprintf(stderr,"Cannot enable HWTS_CHANNEL%d hardware timestamp requests. %m\n",ch);
	   close(ptp_fdes);
	   return -1;
	}
	return 0;
}

int ptp_disable_hwts(int ch)
{
	struct ptp_extts_request extts_request;

	// enable hardware timestamp generation for timer5
	memset(&extts_request, 0, sizeof(extts_request));
	extts_request.index = ch;
	extts_request.flags = 0;

	if(ioctl(ptp_fdes, PTP_EXTTS_REQUEST, &extts_request))
	{
	   fprintf(stderr,"Cannot disable Timer %d HWTS generation.\n",ch);
	   return -1;
	}
	return 0;
}

void ptp_close()
{
	close(ptp_fdes);
	ptp_fdes = 0;
}


int ptp_get_ts(struct timespec *ts, int *channel, int len)
{
	struct ptp_extts_event extts_event[16];
	int cnt;
	int req_cnt = len < 16 ? len : 16;

	cnt = read(ptp_fdes, extts_event, req_cnt * sizeof(struct ptp_extts_event));
	if(cnt < 0)
	{
		fprintf(stderr,"PTP read error: %s\n",strerror(errno));
		return -1;
	}
	else if(cnt == 0)
	{
		return 0;
	}
	else if( (cnt % sizeof(struct ptp_extts_event)) != 0 )
	{
		fprintf(stderr,"Broken PTP timestamp.\n");
		return -1;
	}
	cnt /= sizeof(struct ptp_extts_event);

	for(int i=0; i<cnt;i++)
	{
		ts[i].tv_sec  = extts_event[i].t.sec;
		ts[i].tv_nsec = extts_event[i].t.nsec;
		channel[i]    = extts_event[i].index;
	}

	return cnt;
}


volatile int rtio_quit = 0;

pthread_t tk_thread;
struct timekeeper *tk;
// workers

void *timekeeper_worker(void *arg)
{
	struct timekeeper *tk = (struct timekeeper*)arg;
	uint64_t next_ts, ts_period;
	struct timespec tspec[16];
	int channel[16];
	uint64_t ptp_ts;
	int ptp_ch;
	uint64_t hwts;
	int rcvCnt;

	// int pres = 0;

	next_ts = ts_period = (0xFFFFFFFF - trigger_timer->load)+1;

	// increate thread priority
	if(goto_rt_level(TIMEKEEPER_RT_PRIO))
	{
		fprintf(stderr,"[ERROR] Cannot increase timekeeper rt priority.\n");
	}

	while(!rtio_quit)
	{
		// calc hwts times from timer 5 settings
		rcvCnt = ptp_get_ts(tspec,channel,16);
		if(rcvCnt <=0 )
		{
			fprintf(stderr,"Cannot read CPTS timestamp.\n");
			break;
		}

		// handle timestamps
		for(int i=0;i<rcvCnt;i++)
		{
			ptp_ts = ((uint64_t)tspec[i].tv_sec*1000000000ULL)+tspec[i].tv_nsec;
			ptp_ch = channel[i];

			if(ptp_ch == TIMER5_HWTS_PUSH_INDEX)
			{
				// DMTimer5 HWTS are consumed by the timekeeper core
				hwts = next_ts;
				channel_do_conversion(&trigger_timer->channel,&hwts,1);
				// send the new timestamp to the timekeeper
				timekeeper_add_sync_point(tk, hwts, ptp_ts);

				next_ts += ts_period;
			}
			else // write timestamps to the pipes
			{
				if(ptp_ch < 0 || ptp_ch > 3)
				{
					fprintf(stderr,"Invalid cpts channel id: %d\n",ptp_ch);
					continue;
				}

				cpts_channel_write(&cpts_channels[ptp_ch],ptp_ts);
			}
		}
	}
	// close write side of the cpts pipes to wake readers
	cpts_disable_channels();
	return NULL;
}



// WORKER THREADS
#define MAX_WORKER_COUNT 10
pthread_t workers[MAX_WORKER_COUNT];
int worker_cnt = 0;


// print events to stdout too
int logger_verbose = 0;
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

	fprintf(log,"Local, Global\n");

	while(!rtio_quit)
	{
		rcvCnt = read_channel(ch,ts,16);
		memcpy(ts2,ts,sizeof(uint64_t) *16);
		timekeeper_convert(tk,ts,rcvCnt);
		for(int i=0;i<rcvCnt;i++)
		{
			fprintf(log,"%" PRIu64 ", %" PRIu64 "\n",ts2[i],ts[i]);
			if(logger_verbose)
				fprintf(stdout,"[icap logger CH%d] %llusec, %llunsec\n",ch->idx,(ts[i]/1000000000),ts[i]%1000000000);
		}
	}

	
	fclose(log);
	return NULL;	
}

/*
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
	while(!rtio_quit)
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
*/
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

	while(!rtio_quit)
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

//******************************************************************************//
//******************************** ADC HANDLERS ********************************//
//******************************************************************************//

void *adc_ts_reader(void*arg)
{
	struct adc_buffer_t *adc_buffer = (struct adc_buffer_t*)arg;
	struct icap_channel *ch = adc_buffer->ts_icap;
	uint64_t *wp = adc_buffer->ts_buffer;
	int ts_period = adc_buffer->icap_period;
	uint64_t ts;


	while(read_channel(ch,&ts,1) > 0)
	{
		fprintf(stderr,"[ADC TS] Incoming local ts: %"PRIu64",\tts count: %d\n",ts, wp - adc_buffer->ts_buffer);
		*wp =ts;
		wp += ts_period;
	}

	adc_buffer->ts_buffer_wp = wp;
	fprintf(stderr,"[ADC TS] Ready.\n");

	return NULL;
}

// adc_exit signal handler	
volatile int adc_exit;
static void adc_reader_signal_handler(int signum)
{
	(void)signum;
	adc_exit = 1;
}

void *adc_reader(void *arg)
{
	struct adc_buffer_t *adc_buffer = (struct adc_buffer_t*)arg;
	FILE *fadc = (FILE*)(adc_buffer->fadc);
	int rcvCnt, emptyCnt;
	uint16_t *wp;
	struct sigaction action;

	adc_exit = 0;
	// install signal handler
	memset(&action, 0, sizeof(action));  // SA_RESTART bit not set
    action.sa_handler = adc_reader_signal_handler;
    sigaction(SIGUSR1, &action, NULL);


	wp = adc_buffer->buffer;
	emptyCnt = adc_buffer->buffer_size;
	while((!adc_exit) && (emptyCnt > 10000)  && ((rcvCnt = adc_read_buffer(fadc, wp,50000)) > 0) )
	{
		wp += rcvCnt;
		emptyCnt -= rcvCnt;
		fprintf(stderr,"Received 50000 samples");
	}

	fprintf(stderr,"[ADC READER] Receive finished. Collected data count: %d\n", wp - adc_buffer->buffer);
	return adc_buffer;
}


#define BUFFER_SIZE (10*100*1000)	// 10 sec + 100kS/s -> 1M sample
void *adc_worker(void *arg)
{
	// trigger timer: timer7
	// trigger divider: timer4
	// icap reader: timer5
	struct dmtimer *trigger_timer 		= &dmts[3];
	struct dmtimer *trigger_divider 	= &dmts[0];
	struct dmtimer *capture_timer 		= &dmts[1];

	pthread_t adc_reader_thread, adc_ts_reader_thread;
	struct adc_buffer_t *adc_buffer;
	void * thread_ret;
	int err;

	(void)arg;

	// alloc buffer to contain 10 sec of data
	adc_buffer = (struct adc_buffer_t*)malloc(sizeof(struct adc_buffer_t));
	adc_buffer->buffer_size = BUFFER_SIZE;
	adc_buffer->buffer = (uint16_t *)calloc(BUFFER_SIZE, sizeof(uint16_t));
	adc_buffer->ts_buffer = (uint64_t*)calloc(BUFFER_SIZE, sizeof(uint64_t));
	adc_buffer->buffer_wp = NULL;
	adc_buffer->ts_buffer_wp = NULL;

	if(!adc_buffer->buffer || !adc_buffer->ts_buffer)
	{
		fprintf(stderr,"[ADC] Cannot alloc buffer for adc data or timestamp.");
		return NULL;
	}

	adc_buffer->fadc = adc_open_buffer();
	if(!adc_buffer->fadc)
	{
		free(adc_buffer->buffer);
		free(adc_buffer->ts_buffer);
		free(adc_buffer);
		return NULL;
	}

	adc_buffer->ts_icap = &dmts[1].channel; // dmtimer 5
	adc_buffer->icap_period = 100000;


	// setup trigger timer period
	dmtimer_stop_timer(trigger_timer);
	dmtimer_set_pin_dir(trigger_timer,1); // input
	dmtimer_pwm_setup(trigger_timer, 240,30);	// PWM: 100kHz with 30% duty

	// setup trigger divider
	dmtimer_set_clk_source(trigger_divider, TCLKIN);
	dmtimer_pwm_setup(trigger_divider, 100000,30); // PWM: 1Hz (100kHz divided by 100000)
	dmtimer_set_cntr(trigger_divider,0xFFFFFFFF);
	dmtimer_set_pin_dir(trigger_timer, 0);

	// capture timer -> ok

	// setup adc and enable buffering
	adc_set_hw_event_source(ADC_TRIGGER_TIMER7);
	adc_enable_channel_buffering(0);
	adc_set_buffer_size(100*1024);	
	adc_start_buffering();

	// start ts reader thead
	err = pthread_create(&adc_ts_reader_thread, NULL, adc_ts_reader, (void*)adc_buffer);
	if(err)
		fprintf(stderr,"Cannot start adc ts reader.\n");

	// start adc sample reader thread
	err = pthread_create(&adc_reader_thread, NULL, adc_reader, (void*)adc_buffer);
	if(err)
		fprintf(stderr,"Cannot start adc reader.\n");


	// start trigger timer
	fprintf(stderr,"[ADC] Starting ADC trigger timer.\n");
	dmtimer_set_pin_dir(trigger_timer,0);
	dmtimer_start_timer(trigger_timer);

	sleep(5);
	

	// stop trigger timer
	fprintf(stderr,"[ADC] Stopping ADC trigger timer.\n");
	dmtimer_stop_timer(trigger_timer);
	dmtimer_set_pin_dir(trigger_timer,1);
	
	// send stop signal to the adc reader thread
	pthread_kill(adc_reader_thread, SIGUSR1);
	// send stop signal to the adc ts reader
	disable_channel(&capture_timer->channel);

	fprintf(stderr,"[ADC] Waiting for the reader threads.\n");
	// wait for the reader threads
	pthread_join(adc_reader_thread,&thread_ret);
	pthread_join(adc_ts_reader_thread,&thread_ret);
	// cleanup

	// save collected data
	adc_close_buffer(adc_buffer->fadc);


	// export read data
	fprintf(stderr,"[ADC] Exporting adc data.\n");
	FILE *adc_export;
	adc_export = fopen("adc.log","w");
	if(adc_export)
	{
		int i;
		int dataCnt = adc_buffer->buffer_wp - adc_buffer->buffer;
		for(i=0;i<dataCnt;i++)
			fprintf(adc_export,"%"PRIu64", %"PRIu16"\n",adc_buffer->ts_buffer[i],adc_buffer->buffer[i]);

		fclose(adc_export);
		fprintf(stderr,"[ADC] Export ready.\n");
	}
	else
	{
		fprintf(stderr,"[ADC] Cannot open adc.log.\n");
	}

	free(adc_buffer->buffer);
	free(adc_buffer->ts_buffer);
	free(adc_buffer);
	return NULL;
}



void close_timers()
{
	for(int i=0;i<4;i++)
		close_dmtimer(&dmts[i]);

	for(int i=0;i<3;i++)
		close_ecap(&ecaps[i]);
}

int init_timers()
{
	int err1=0, err2=0;

	// init dmtimers
	for(int i=0;i<4 && !err1;i++)
		err1 = init_dmtimer(&dmts[i]);

	// init ecap timers
	for(int i=0;i<3 && !err1 && !err2;i++)
		err2 = init_ecap(&ecaps[i]);

	if(err1 || err2)
	{
		close_timers();
		return -1;
	}

	return 0;
}

int dmtimer_setup(struct dmtimer *t, enum timer_mode_t mode)
{
	switch(mode)
	{
		case NONE:
			t->enabled = 0; t->enable_oc = 0; t->enable_icap = 0;
			break;
		case ICAP:
			t->enabled=1; t->enable_oc=0; t->enable_icap=1;
			t->clk_source = SYSCLK; t->load=0; t->match = 100;
			t->pin_dir=1; 
			break;
		case PWM:
			t->enabled=1; t->enable_oc=1; t->enable_icap=0;
			t->clk_source = SYSCLK; t->load=0; t->match = 100;
			t->pin_dir=0;
			break;
		case CLKDIV:
			t->enabled=1; t->enable_oc=1; t->enable_icap=0;
			t->clk_source = TCLKIN; t->load=0; t->match = 100;
			t->pin_dir=0;
		break;
		default:
			fprintf(stderr,"Unknown dmtimer mode\n");
			return -1;
	}
	return 0;
}

int ecap_setup(struct ecap_timer *t, enum timer_mode_t mode)
{
	switch(mode)
	{
		case NONE:
			t->enabled = 0;
			break;
		case ICAP:
			t->enabled=1;
			t->event_div = 1;
			t->hw_fifo_size = 1;
			break;
		default:
			fprintf(stderr,"Unknown eCAP mode\n");
			return -1;
	}
	return 0;

}

int parse_setup(struct timer_setup_t setup)
{
	if(setup.dmtimer4_mode!=NONE && setup.dmtimer4_mode!=CLKDIV && setup.dmtimer4_mode!=PWM )
	{
		fprintf(stderr,"Timer 4 can only be used on NONE, CLKDIv or PWM mode.\n");
		return -1;
	}

	if(dmtimer_setup(&dmts[0],setup.dmtimer4_mode))
	{
		fprintf(stderr,"Cannot initialize dmtimer%d\n",4);
		return -1;
	}
	if(dmtimer_setup(&dmts[2],setup.dmtimer6_mode))
	{
		fprintf(stderr,"Cannot initialize dmtimer%d\n",6);
		return -1;
	}
	if(dmtimer_setup(&dmts[3],setup.dmtimer7_mode))
	{
		fprintf(stderr,"Cannot initialize dmtimer%d\n",7);
		return -1;
	}
	if(ecap_setup(&ecaps[0],setup.ecap0_mode))
	{
		fprintf(stderr,"Cannot initialize eCAP%d\n",0);
		return -1;
	}
	if(ecap_setup(&ecaps[2],setup.ecap2_mode))
	{
		fprintf(stderr,"Cannot initialize eCAP%d\n",2);
		return -1;
	}

	// save the timers with input capture channels enabled
	for(int i=0;i<4;i++)
	{
		if(dmts[i].enable_icap)
		{
			icap_channels[icap_count] = &dmts[i].channel;
			icap_dev_ifs[icap_count++] = &dmts[i].dev;
		}
	}

	for(int i=0;i<3;i++)
	{
		if(ecaps[i].enabled)
		{
			icap_channels[icap_count] = &ecaps[i].channel;
			icap_dev_ifs[icap_count++] = &ecaps[i].dev;
		}
	}


	return 0;
}

// main init function
#define CHANNEL_NUM 10
#define SYNC_OFFSET 210000000 // 210ms
#define TIMEKEEPER_LOG_NAME	"./timekeeper.log"

int init_rtio(struct timer_setup_t setup)
{
	int err = 0;
	double trigger_period;

	// create pipes for cpts timestamp forwarding
	if(create_cpts_channels())
	{
		fprintf(stderr,"CPTS channel error.\n");
		return -1;
	}

	// parse hw settings
	if(parse_setup(setup))
		return -1;

	// init /dev/ptp0 clock
	if(ptp_open())
	{
		fprintf(stderr,"Cannot open PTP device.\n");
		return -1;
	}
	if(setup.dmtimer4_cpts_hwts_en)
	{
		ptp_disable_hwts(0);
		ptp_enable_hwts(0);
	}
	ptp_disable_hwts(TIMER5_HWTS_PUSH_INDEX);

	if(ptp_enable_hwts(TIMER5_HWTS_PUSH_INDEX))
	{
		fprintf(stderr,"Cannot enable DMTimer 5 HWTS generation.\n");
		return -1;
	}

	if(setup.dmtimer6_cpts_hwts_en)
	{
		ptp_disable_hwts(2);
		ptp_enable_hwts(2);
	}
	if(setup.dmtimer7_cpts_hwts_en)
	{
		ptp_disable_hwts(3);
		ptp_enable_hwts(3);
	}



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
		fprintf(stderr,"Cannot open hw timers. Exiting...\n");
		return -1;
	}

	fprintf(stderr, "Offset cancellation.\n");
	if(init_sync_gpio())
	{
		timekeeper_destroy(tk);
		fprintf(stderr,"Exiting.\n");
		return -1;
	}
	//change timer event source to gpio interrupt
	for(int i=0;i<icap_count;i++)
	{
		timer_set_icap_source(icap_dev_ifs[i],17);
		flush_channel(icap_channels[i]);
	}
	trigger_sync_gpio();
	close_sync_gpio();

	fprintf(stderr,"Sync point timestamps:\n");
	for(int i=0;i<icap_count;i++)
	{
		uint64_t ts;
		struct icap_channel *ch = icap_channels[i];

		read_channel_raw(ch,&ts,1);
		printf("%s - 0x%016llx\n",ch->dev_path, ts);
		ch->offset = -ts;
	}
	fprintf(stderr,"Offset cancellation ready.\n");


	//change timer event source to input pins
	for(int i=0;i<icap_count;i++)
	{
		timer_set_icap_source(icap_dev_ifs[i],0);
	}

	// start timekeeper thread
	err = pthread_create(&tk_thread, NULL, timekeeper_worker, (void*)tk);
	if(err)
	{
		fprintf(stderr,"Cannot start the timekeeper.\n");
		close_timers();
		timekeeper_destroy(tk);
	}



	return 0;
}

void close_rtio()
{
	void *ret;

	// signal exiting
	rtio_quit = 1;
	// close icap channels to wake readers
	for(int i=0;i<icap_count;i++)
		if(icap_channels[i])
			disable_channel(icap_channels[i]);

	// wait for the workers 
	for(int i=0; i<worker_cnt;i++)
	{
		pthread_join(workers[i],&ret);
	}

	pthread_join(tk_thread,&ret);


	close_timers();
	timekeeper_destroy(tk);


	ptp_disable_hwts(0);
	ptp_disable_hwts(1);
	ptp_disable_hwts(2);
	ptp_disable_hwts(3);
	ptp_close();
	close_cpts_channels();
}

struct ts_channel *get_ts_channel(int channel_idx)
{
	switch(channel_idx)
	{
		case -7:
			return &cpts_channels[3].ts_ch;
			break;
		case -6:
			return &cpts_channels[2].ts_ch;
			break;
		case -5:
			return &cpts_channels[2].ts_ch;
			break;
		case -4:
			return &cpts_channels[1].ts_ch;
			break;
						
		case 0:
			return &ecaps[0].channel.ts_ch;
			break;
		case 2:
			return &ecaps[2].channel.ts_ch;
			break;
		case 4:
			return &dmts[0].channel.ts_ch;
			break;
		case 5:
			return &dmts[1].channel.ts_ch;
			break;
		case 6:
			return &dmts[2].channel.ts_ch;
			break;
		case 7:
			return &dmts[3].channel.ts_ch;
			break;
		default:
			return NULL;
	}
	return NULL;
}

struct icap_channel *get_channel(int timer_idx)
{
	switch(timer_idx)
	{
		case 0:
			return &ecaps[0].channel;
			break;
		case 2:
			return &ecaps[2].channel;
			break;
		case 4:
			return &dmts[0].channel;
			break;
		case 5:
			return &dmts[1].channel;
			break;
		case 6:
			return &dmts[2].channel;
			break;
		case 7:
			return &dmts[3].channel;
			break;
		default:
			return NULL;
	}
	return NULL;
}
int start_icap_logging(int timer_idx, int verbose)
{
	int err;

	struct icap_channel *c;

	c = get_channel(timer_idx);
	if(!c)
	{
		fprintf(stderr,"Inalvid timer idx for channel logging.\n");
		return -1;
	}


	if(c->fdes <= 0)
	{
		fprintf(stderr,"Channel is not opened.\n");
		return -1;
	}

	fprintf(stderr,"Starting icap logger on channel %d.\n",timer_idx);

	if(verbose)
		logger_verbose = 1;

	err = pthread_create(&workers[worker_cnt++], NULL, channel_logger, (void*)c);
	if(err)
	{
		fprintf(stderr,"Cannot start the channel logger.\n");
		return -1;
	}

	return 0;
}


struct dmtimer *get_dmtimer(int dmtimer_idx)
{
	switch(dmtimer_idx)
	{
		case 4:
			return &dmts[0];
			break;
		case 5:
			return &dmts[1];
			break;
		case 6:
			return &dmts[2];
			break;
		case 7:
			return &dmts[3];
			break;
		default:
			return NULL;
	}
	return NULL;
}


int start_npps_generator(int icap_timer_idx, int pwm_timer_idx, uint32_t pps_period_ms, uint32_t hw_prescaler, int verbose_level)
{
	struct nPPS_servo_t *npps;
	struct icap_channel *c;
	struct dmtimer *d;
	int err;

	npps = (struct nPPS_servo_t*)malloc(sizeof(struct nPPS_servo_t));
	if(!npps)
	{
		fprintf(stderr,"Cannot alloc memory for pps descriptor.\n");
		return -1;
	}

	c = get_channel(icap_timer_idx);
	if(!c)
	{
		fprintf(stderr,"Inalvid timer idx for channel logging.\n");
		return -1;
	}

	d = get_dmtimer(pwm_timer_idx);
	if(!d)
	{
		fprintf(stderr,"Invalid dmtimer index.\n");
		return -1;
	}
	
	if(!d->enable_oc)
	{
		fprintf(stderr,"Timer is not in PWM mode.\n");
		return -1;
	}

	if(pps_period_ms < 10 || pps_period_ms > 1000)
	{
		fprintf(stderr,"Unsupported pps period %d. Valid region: 10ms - 1sec.\n",pps_period_ms);
		return -1;
	}

	if(hw_prescaler == 0 || hw_prescaler > 62 || (hw_prescaler%2==1 && hw_prescaler!=1))
	{
		fprintf(stderr,"Invalid hw prescaler: %d. Give an even number (or 1) between 1-62.\n",hw_prescaler);
		return -1;
	}

	if(hw_prescaler != 1 && icap_timer_idx != 0 && icap_timer_idx != 2)
	{
		fprintf(stderr,"Clock dividing is only supported on the eCAP peripherals.\n");
		return -1;
	}

	if(hw_prescaler > 1)
	{
		if(ecap_set_prescaler(&ecaps[icap_timer_idx],hw_prescaler))
		{
			fprintf(stderr,"Cannot set hw prescaler for eCAP.\n");
			return -1;
		}
	}


	npps->feedback_channel = c;	
	npps->pwm_gen = d;
	npps->period_ms = pps_period_ms;
	npps->hw_prescaler = hw_prescaler;
	npps->verbose_level = verbose_level;

	fprintf(stderr,"Starting nPPS generation on %s using icap channel %s as feedback.\nnPPS period: %dms, hw prescale rate: %d.\n",d->name, c->dev_path,pps_period_ms, hw_prescaler);

	err = pthread_create(&workers[worker_cnt++], NULL, npps_worker, (void*)npps);
	if(err)
		fprintf(stderr,"Cannot start the PPS servo thread.\n");
	return 0;
}


int start_pps_generator(int ts_channel_idx, int pwm_timer_idx, int verbose_level)
{
	struct PPS_servo_t *pps;
	struct ts_channel *c;
	struct dmtimer *d;
	int err;

	pps = (struct PPS_servo_t*)malloc(sizeof(struct PPS_servo_t));
	if(!pps)
	{
		fprintf(stderr,"Cannot alloc memory for pps descriptor.\n");
		return -1;
	}

	c = get_ts_channel(ts_channel_idx);
	if(!c)
	{
		fprintf(stderr,"Inalvid timer idx for channel logging.\n");
		return -1;
	}

	d = get_dmtimer(pwm_timer_idx);
	if(!d)
	{
		fprintf(stderr,"Invalid dmtimer index.\n");
		return -1;
	}
	d = &dmts[pwm_timer_idx-4];
	if(!d->enable_oc)
	{
		fprintf(stderr,"Timer is not in PWM mode.\n");
		return -1;
	}


	pps->feedback_channel = c;	
	pps->pwm_gen = d;
	pps->verbose_level = verbose_level;

	fprintf(stderr,"Starting PPS generation on %s.\n",d->name);

	err = pthread_create(&workers[worker_cnt++], NULL, pps_worker, (void*)pps);
	if(err)
		fprintf(stderr,"Cannot start the PPS servo thread.\n");
	return 0;
}