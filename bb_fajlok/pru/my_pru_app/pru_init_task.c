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

	/* Load the memory data file */
	prussdrv_load_datafile(PRU_NUM, "./data.bin");

	/* Load and execute binary on PRU */
	prussdrv_exec_program(PRU_NUM, "./text.bin");

	prussdrv_exit ();

	printf("PRU_TASK program is loaded and running...\n");

	return EXIT_SUCCESS;
}
