#include "channel.h"

	struct hw_icap_src *ch[6];

	char *channel_names[6] = {"tim5", "tim6", "tim7", "ecap0","ecap1","ecap2"};
	char *channel_paths[6] = {
								"/dev/timer5_icap",
								"/dev/timer6_icap",
								"/dev/timer7_icap",
								"/dev/ecap0_icap",
								"/dev/ecap1_icap",
								"/dev/ecap2_icap",
	} 

int main()
{
	/* Steps:
	1, Open up the input channels
	2, Trigger offset measurement
	3, Compensate the channels for offset.
	4, Read global time timestamps and compensate the local clock.
	*/

	for(int i=0;i<6;i++)
		ch[i] = open_hw_icap_src(channel_paths[i]);
	


}