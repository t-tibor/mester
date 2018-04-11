#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "gpio_uapi.h"


// register definitions

// USED GPIO PIN:
//	GPIO1 / GPIO44 on P8_12


#define GPIO_BASE_ADDR 	0x4804C000 //GPIO1 start address
#define GPIO_END_ADDR	0x4804CFFF  		
#define GPIO_ADDR_LENGTH (GPIO_END_ADDR - GPIO_BASE_ADDR)


#define GPIO_OE_OFFSET      		0x134   
#define GPIO_DATAOUT_OFFSET	 		0x13C
#define GPIO_SETDATAOUT_OFFSET		0x194
#define GPIO_CLEARDATAOUT_OFFSET  	0x190

#define GPIO_44             (1<<12) 


volatile uint32_t *gpio_base;
volatile uint32_t *gpio_out_en;
volatile uint32_t *gpio_set_dataout;
volatile uint32_t *gpio_clear_dataout;
volatile uint32_t *gpio_dataout;

int fd;

// GPIO handler functions
void gpio_init()
{
	

    if((fd	= open("/dev/mem", O_RDWR | O_SYNC)) < 0)
	{
		perror("Opening /dev/mem");
	}

	gpio_base = (uint32_t*)mmap(0, GPIO_ADDR_LENGTH, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASE_ADDR);
    if(gpio_base == MAP_FAILED)
    {
    	perror("Unable to map GPIO");
    	return;
    }

    // output enable register
    gpio_out_en = gpio_base + GPIO_OE_OFFSET/4;
	*gpio_out_en = (~GPIO_44) & *gpio_out_en; // config GPIO as output

	// dataout set register
	gpio_set_dataout = gpio_base + GPIO_SETDATAOUT_OFFSET/4;
	// dataout clear register
	gpio_clear_dataout = gpio_base + GPIO_CLEARDATAOUT_OFFSET/4;

	gpio_dataout = gpio_base + GPIO_DATAOUT_OFFSET/4;
}

void gpio_set()
{
	*gpio_set_dataout = GPIO_44;
}
void gpio_clear()
{
	*gpio_clear_dataout = GPIO_44;
}

void gpio_toggle()
{
	uint32_t val = *gpio_dataout;
	val ^= GPIO_44;
	*gpio_dataout = val;
}

void gpio_deinit()
{
	*gpio_out_en = GPIO_44 | *gpio_out_en;

	munmap((void *)gpio_base, GPIO_ADDR_LENGTH);

	gpio_base = NULL;
	gpio_set_dataout = NULL;
	gpio_clear_dataout = NULL;
	gpio_dataout = NULL;

    close(fd);
}