#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#define PRU_NUM	0	// using PRU0

int main (void)
{
	int n, ret;
	if(getuid()!=0)
	{
		printf("You must run this program as root. Exiting.\n");
		return EXIT_FAILURE;
	}
	printf("PRU_TASK program is loaded and running...\n");

	/* Initialize structure used by prussdrv_pruintc_intc */
	/* PRUSS_INTC_INITDATA is found in pruss_intc_mapping.h */
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	/* Allocate and initialize memory */
	prussdrv_init ();
	ret = prussdrv_open(PRU_EVTOUT_0);
	if(ret){
		printf("Failed to open the PRU-ICSS, have you loaded the overlay?\n");
		return EXIT_FAILURE;
	}
	/* prussdrv: Opens an event out and initializes memory mapping.
	 * The input is a pru_evtout_num (PRU_EVTOUT_0 - PRU_EVTOUT_7)
	 * corresponding to Host2 - Host9 of the PRU INTC.
     */

	/* Map PRU's interrupts */
	prussdrv_pruintc_init(&pruss_intc_initdata);

	/* Load the memory data file */
	prussdrv_load_datafile(PRU_NUM, "./data.bin");

	/* Load and execute binary on PRU */
	prussdrv_exec_program(PRU_NUM, "./text.bin");

   
	/* Wait for event completion from PRU, returns the PRU_EVTOUT_0 number */
	//n = prussdrv_pru_wait_event (PRU_EVTOUT_0);
	/* This assumes the PRU generates an interrupt
	 * connected to event out 0 immediately before halting
	 */

	//printf("PRU_TASK program completed, event number %d.\n", n);

	/* Disable PRU and close memory mappings */
	//prussdrv_pru_disable(PRU_NUM);
	prussdrv_exit ();

	return EXIT_SUCCESS;
}
