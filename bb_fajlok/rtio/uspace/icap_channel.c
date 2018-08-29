#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl>

#include "icap_channel.h"

#define assert(x) {if(!(x)) {fprintf(stderr,"Assertion error. File:%s, Line:%d\n",__FILE__,__LINE__); while(1) sleep(1); }}

inline uint32_t read32(volatile uint8_t *base, int offset)
{
	volatile uint32_t *a = (volatile uint32_t*)(base + offset);
	return *a;
}

inline void write32(volatile uint8_t *base, int offset, uint32_t val)
{
	volatile uint32_t *a = (volatile uint32_t*)(base + offset);
	*a = val;
}

inline uint16_t read16(volatile uint8_t *base, int offset)
{
	volatile uint16_t *a = (volatile uint16_t*)(base + offset);
	return *a;
}

inline void write16(volatile uint8_t *base, int offset, uint16_t val)
{
	volatile uint16_t *a = (volatile uint16_t*)(base + offset);
	*a = val;
}

inline uint8_t read8(volatile uint8_t *base, int offset)
{
	volatile uint8_t *a = (volatile uint16_t*)(base + offset);
	return *a;
}

inline void write8(volatile uint8_t *base, int offset, uint8_t val)
{
	volatile uint16_t *a = (volatile uint8_t*)(base + offset);
	*a = val;
}



// timer interface
enum timer_type_t {TIMER_TYPE_DMTIMER, TIMER_TYPE_ECAP,TIMER_TYPE_PWM};
enum timer_clk_source_t {SYSCLK=1, TCLKIN=2};

struct uio
{
	char *path;
	int fdes;
	volatile uint8_t *base;
}

struct dmtimer
{
	char 								*name;
	enum timer_clk_source clk_t 		clk_source;

	struct uio					dev;
	uint32_t							load;
	uint32_t							match;

	int 								enable_oc;
	int 								enable_icap;
	int 								pin_dir; //1:in, 0:out
}

struct ecap_timer
{
	char 								*name;
	struct uio					dev;
	uint32_t							event_div;

}

#define DMTIMER_CNT 3

struct dmtimer dmt[DMTIMER_CNT] = {
	{	.name = "dmtimer5",
		.clk_source = SYSCLK;
		.dev = 
			{
				.path = "/dev/dmtimer5",
			},
		.load = 0,
		.match = 0,
		.enable_oc = 0,
		.enable_icap = 1,
		.pin_dir = 1
	},

	{	.name = "dmtimer6",
		.clk_source = SYSCLK;
		.dev = 
			{
				.path = "/dev/dmtimer7",
			},
		.load = 0,
		.match = 0,
		.enable_oc = 0,
		.enable_icap = 1,
		.pin_dir = 1
	},

	{	.name = "dmtimer7",
		.clk_source = SYSCLK;
		.dev = 
			{
				.path = "/dev/dmtimer7",
			},
		.load = 0,
		.match = 0,
		.enable_oc = 0,
		.enable_icap = 1,
		.pin_dir = 1
	}
};

#define ECAP_CNT 3
struct ecap_timer ecap[ECAP_CNT] = {
	{
		.name="ecap0",
		.dev = 
			{
				.path = "/dev/ecap0",
			},
		.event_div = 1
	}
	{
		.name="ecap1",
		.dev = 
			{
				.path = "/dev/ecap1",
			},
		.event_div = 1
	}
	{
		.name="ecap2",
		.dev = 
			{
				.path = "/dev/ecap2",
			},
		.event_div = 1
	}
};

int timer_enable_clk(struct uio *dev)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(dev->fdes, TIMER_IOCTL_SET_CLOCK_STATE, TIMER_CLK_ENABLE);
}
int timer_set_clk_source(struct uio *dev, enum timer_clk_source clk)
{
	int ret;
	assert(dev);
	assert(dev->fdes > 0)

	switch(clk)
	{
		case SYSCLK:
			ret = ioctl(fdes,DMTIMER_IOCTL_SET_CLOCK_SOURCE,TIMER_CLOCK_SOURCE_SYSCLK);
			break;

		case TCLKIN:
			ret = ioctl(fdes, DMTIMER_IOCTL_SET_CLOCK_SOURCE,TIMER_CLOCK_SOURCE_TCLKIN);
			break;

		default:
			fprintf(stderr,"Invalid clocksource: %d\n",clk)
			ret = -1;
	}
	return ret;
}



int timer_set_icap_source(struct uio *dev, int source)
{
	assert(dev);
	assert(dev->fdes > 0);
	return ioctl(fdes,TIMER_IOCTL_SET_ICAP_SOURCE,source);
}


int init_uio(struct uio *dev)
{
	assert(dev);

	dev->fdes = open(dev->path,O_RDWR);
	if(dev->fdes == -1)
	{
		fprintf(stderr,"Cannot open cdev file: %s\n",t->cdev);
		dev->base = NULL;
		return -1;
	}


	// mmap the register space
	dev->base = (uint8_t*)mmap(NULL,1024, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fdes, 0);
	if(dev->base = MAP_FAILED)
	{
		fprintf(stderr,"Cannot mmap the character device %s\n",dev->path);
		close(dev->fd);
		return -1;
	}

	return 0;
}



int init_timers()
{
	int err = 0;
	int ret;
	uint32_t tmp;

	// init dmtimers
	for(int i=0;i<DMTIMER_CNT;i++)
	{
		struct dmtimer *t = dmt[i];

		ret = init_uio(&t->dev);
		assert(ret == 0);

		// test the mapping
		tmp = read32(t->dev.base,DMTIMER_TIDR); assert(tmp = 0x40000100UL);
		// set up the timer clock source
		ret = timer_set_clk(&t->dev,t->clk); assert(ret == 0);
		// enable timers clock
		ret = timer_enable_clk(&t->dev); assert(ret);

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
	}

	for(int i=0;i<ECAP_CNT;i++)
	{
		struct ecap_timer *e = ecap[i];

		ret = init_uio(&e->dev);
		assert(ret);

		// test the mapping
		tmp = read32(e->dev.base,ECAP_REVID); assert(tmp = 0x44D22100);
		// enable timers clock
		ret = timer_enable_clk(&e->dev); assert(ret);

		write32(e->dev.base,ECAP_TSCTR,0x00);
		assert(e->event_div % 2 == 0);
		assert(e->event_div-1 < 62)
		write32(e->dev.base,ECAP_ECCTL1,ECCTL1_CAPLDEN |( (uint32_t)(e->event_div/2) << ECCTL1_PRESCALE_OFFSET));
		write32(e->dev,ECAP_ECCTL2, ECCTL2_TSCTRSTOP | (3<<ECCTL2_STOP_WRAP_OFFSET)); //use all the 4 registers
	}

	// TODO enable interrupts
}




// icap interface

uint64_t first_sync_tick;

#define CHANNEL_NUM

struct icap_channel
{
	char *name;
	struct uio dev;
	int32_t offset;
	int timer_idx;
}

struct icap_channel channels[CHANNEL_NUM] = {

	{
		.name = "ch_dmtimer5",
		.dev = {.path = "/dev/dmtimer5_icap_channel"},
		.timer_idx = 0
	},

	{
		.name = "ch_dmtimer6",
		.dev = {.path = "/dev/dmtimer6_icap_channel"},
		.timer_idx = 1
	},

	{
		.name = "ch_dmtimer7",
		.dev = {.path = "/dev/dmtimer7_icap_channel"},
		.timer_idx = 0
	},

	{
		.name = "ch_ecap0",
		.dev = {.path = "/dev/ecap0_icap_channel"},
		.timer_idx = 0
	},

	{
		.name = "ch_ecap1",
		.dev = {.path = "/dev/ecap1_icap_channel"},
		.timer_idx = 0
	},

	{
		.name = "ch_ecap2",
		.dev = {.path = "/dev/ecap2_icap_channel"},
		.timer_idx = 0
	},

};



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
		return -1;

	return bit;
}

int init_channels()
{
	int ret;
	for(int i=0;i<CHANNEL_NUM;i++)
	{
		struct icap_channel *c = channels[i];
		fprintf(stderr,"Configuring cpture channel %s.\n",c->name);
		ret = init_uio(&c->dev);
		assert(ret);

		ret = ioctl(c->fdes,ICAP_IOCTL_CLEAR_OVF); assert(ret);
		ret = ioctl(c->fdes,ICAP_IOCTL_RESET_TS); assert(ret);
		ret = ioctl(c->fdes,ICAP_IOCTL_STORE_EN); assert(ret);
		ret = ioctl(c->fdes,ICAP_IOCTL_FLUSH); assert(ret);

		if(c->timer_idx < DMTIMER_CNT)
		{
			int bitcnt;
			//dmtimer may be programmed for shorter periods
			struct dmtimer *t = dmt[c->timer_idx];
			bitcnt = get_effective_bit_cnt(t>load);
			assert(bitcnt == -1);
			fprintf(stderr,"Setting channel bit count to %d.\n",bitcnt);
			ret = ioctl(c->fdes,ICAP_IOCTL_SET_TS_BITNUM,bitcnt); assert(ret);
		}

	}

	init_timers();

}