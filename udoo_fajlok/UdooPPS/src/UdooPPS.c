#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "UdooPPS.h"

int main(int argc, char *argv[])
{
    volatile void *rand_enet_addr;
    volatile unsigned int *enet_timer_control;
    volatile unsigned int *enet_timer_period;
    volatile unsigned int *enet_timer_value;
    volatile unsigned int *enet_timer_inc;
    volatile unsigned int *enet_timer_corr;
    volatile unsigned int *enet_timer_offs;
    volatile unsigned int *enet_tcsr1;
    volatile unsigned int *enet_tccr1;
    volatile unsigned int *enet_ecr1;

    int fd = open("/dev/mem", O_RDWR);

    printf("Mapping Enet: %X - %X (size: %X)\n", ENET_1_START_ADDR, ENET_1_END_ADDR, ENET_1_SIZE);
    rand_enet_addr = mmap(0, ENET_1_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ENET_1_START_ADDR);
    if(rand_enet_addr == MAP_FAILED)
    {
    	printf("Unable to map Enet\n");
    	exit(1);
    }

    enet_timer_control	= rand_enet_addr + ENET1_ATCR;
    enet_timer_value	= rand_enet_addr + ENET1_ATVR;
    enet_timer_period	= rand_enet_addr + ENET1_ATPER;
    enet_timer_inc		= rand_enet_addr + ENET1_ATINC;
    enet_timer_corr		= rand_enet_addr + ENET1_ATCOR;
    enet_timer_offs		= rand_enet_addr + ENET1_ATOFF;
    enet_tcsr1			= rand_enet_addr + ENET1_TCSR1;
    enet_tccr1			= rand_enet_addr + ENET1_TCCR1;
    enet_ecr1			= rand_enet_addr + ENET1_ECR;

   /***************************************************************/


#ifndef DEBUG_1

   // Set PINPER: Enables event signal output assertion on period event
   *enet_timer_control = *enet_timer_control | 0x00000080;

   while((*enet_tcsr1) & ENET_TCSR_TMODE) // Timer channel have to be disabled first
   {
	   // Disable Timer Channel
	   *enet_tcsr1 = *enet_tcsr1 & ENET_TCSR_TMODE_dis;	// TDRE bit might be compromised
   }

   /* Initialize the compare register value */
   *enet_tccr1 = 100000000; // 67108864 / 0x2faf080

   /* Setting the Timer mode (event) and enable the Timer channel*/
   //*enet_tcsr1 = *enet_tcsr1 | ENET_TCSR_en_high_pulse;
   //*enet_tcsr1 = *enet_tcsr1 | ENET_TCSR_en_toggle;
   *enet_tcsr1 = *enet_tcsr1 | ENET_TCSR_en_set_clear;
   //*enet_tcsr1 = *enet_tcsr1 | ENET_TCSR_enable_irq | ENET_TCSR_en_sw_only;

   printf("ENET_TCSR Enabled!!!\n");
   //printf("ENET1_TCSR1:\t0x%08x\n",*enet_tcsr1);
   //printf("ENET1_TCCR1:\t0x%08x\n\n",*enet_tccr1);


#endif
#ifdef DEBUG_1
   printf("ENET1_ATCR:\t0x%08x\n",*enet_timer_control);
   printf("ENET1_ATPER:\t0x%08x\n",*enet_timer_period);
   printf("ENET1_ATINC:\t0x%08x\n",*enet_timer_inc);
   printf("ENET1_ATCOR:\t0x%08x\n\n",*enet_timer_corr);

   printf("ENET1_TCSR1:\t0x%08x\n",*enet_tcsr1);
   printf("ENET1_TCCR1:\t0x%08x\n\n",*enet_tccr1);

   printf("PADMUX_SD1_DATA1:\t0x%08x\n\n",*padmux_sd1_data1);

#endif


	/*
	while(...);
	// TDRE bit might be compromised
	*enet_tcsr1 = *enet_tcsr1 & ENET_TCSR_TMODE_dis & ENET_TCSR_disable_irq;
	printf("ENET_TCSR mode and irq Disabled!!!\n");	*/


    munmap((void *)rand_enet_addr, ENET_1_SIZE);
    close(fd);

    return EXIT_SUCCESS;
}
