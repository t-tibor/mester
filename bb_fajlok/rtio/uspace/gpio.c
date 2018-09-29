#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gpio.h"


#define SYNC_GPIO_CNT	26
#define SYNC_GPIO_PATH	"/sys/class/gpio/gpio26/"
#define GPIO_EXPORT_PATH "/sys/class/gpio/export"

#define GPIO_BANK_BASE 				0x44e07000
#define GPIO_IRQ_FORCE_OFFSET	0x24

#define DEVMEM_PATH		"/dev/mem"
static int gpio_mmap_fdes;
static volatile uint8_t *sync_gpio_base = NULL;

int init_sync_gpio()
{
	FILE *f;
	
	f = fopen(SYNC_GPIO_PATH "direction","w");
	if(!f)
	{
		// try to export it
		f = fopen(GPIO_EXPORT_PATH,"w");
		if(!f)
		{
			fprintf(stderr,"Cannot open %s.\n",GPIO_EXPORT_PATH);
			return -1;
		}
		fprintf(f,"%d",SYNC_GPIO_CNT);
		fclose(f);

		// wait for the gpio driver to create the gpio26 folder
		usleep(100000);

		f = fopen(SYNC_GPIO_PATH "direction","w");
		if(!f)
		{
			fprintf(stderr,"Cannot open %s. %m\n",SYNC_GPIO_PATH "direction");
			return -1;
		}
	}

	// set pin direction to input
	fprintf(f,"in");
	fclose(f);

	// setup interrupt sensitivity for rising edge
	f = fopen(SYNC_GPIO_PATH "edge","w");
	if(!f)
	{
		fprintf(stderr,"Cannot open %s.\n",SYNC_GPIO_PATH "edge");
		return -1;
	}
	fprintf(f,"rising");
	fclose(f);

	// open mmap on gpio0 to enable interrupt triggering
	gpio_mmap_fdes = open(DEVMEM_PATH,O_RDWR | O_SYNC);
	if(gpio_mmap_fdes < 0)
	{
		fprintf(stderr,"Cannot open /dev/mem.\n");
		return -1;
	}

	sync_gpio_base = (volatile uint8_t*)mmap(NULL,4096, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_mmap_fdes, GPIO_BANK_BASE);
	if(sync_gpio_base == MAP_FAILED)
	{
		fprintf(stderr,"Cannot map /dev/mem.\n");
		close(gpio_mmap_fdes);
		return -1;
	}

	return 0;
}


void trigger_sync_gpio()
{
	*((volatile uint32_t*)(sync_gpio_base + GPIO_IRQ_FORCE_OFFSET)) = (1ULL<<(SYNC_GPIO_CNT % 32));
}

void close_sync_gpio()
{
	munmap((void*)sync_gpio_base,4096);
	close(gpio_mmap_fdes);
}