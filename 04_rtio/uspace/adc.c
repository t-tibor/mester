#include <string.h>
#include <stdio.h>

#include "adc.h"

int adc_set_channel_buffering(unsigned channel, int state)
{
	char tmp[256];
	FILE *f;

	// input checking
	if(channel > 6)
	{
		fprintf(stderr,"Invalid adc channel (%d).\n",channel);
		return -1;
	}

	tmp[255] = 0;
	snprintf(tmp,255, "%s/scan_elements/in_voltage%d_en",ADC_IIO_PATH_BASE,channel);
	f = fopen(tmp,"w");
	if(!f)
	{
		fprintf(stderr,"Cannot open the selected scan element. (%s)\n",tmp);
		return -1;
	}

	if(fprintf(f,"%d", (state != 0) ? 1 : 0) < 1)
	{
		fprintf(stderr,"Cannot enable/disable the selected adc channel.\n");
		fclose(f);
		return -1;
	}

	return fclose(f);
}


int adc_enable_channel_buffering(unsigned channel)
{
	return adc_set_channel_buffering(channel, 1);	
}

int adc_disable_channel_buffering(unsigned channel)
{
	return adc_set_channel_buffering(channel,0);
}

int adc_set_buffer_size(unsigned size)
{
	char tmp[256];
	FILE *f;

	tmp[255] = 0;
	snprintf(tmp,255, "%s/buffer/length",ADC_IIO_PATH_BASE);
	f = fopen(tmp,"w");
	if(!f)
	{
		fprintf(stderr,"Cannot open .../buffer/length file. (%s)\n",tmp);
		return -1;
	}


	if(fprintf(f,"%d", size) < 1)
	{
		fprintf(stderr,"Cannot set the buffer size.\n");
		fclose(f);
		return -1;
	}

	return fclose(f);
}

int adc_set_buffering(int state)
{
	char tmp[256];
	FILE *f;

	tmp[255] = 0;
	snprintf(tmp,255, "%s/buffer/enable",ADC_IIO_PATH_BASE);
	f = fopen(tmp,"w");
	if(!f)
	{
		fprintf(stderr,"Cannot open .../buffer/enable file. (%s)\n",tmp);
		return -1;
	}

	if(fprintf(f,"%d", ((state != 0) ? 1 : 0) ) < 1)
	{
		fprintf(stderr,"Cannot enable/disable buffering.\n");
		fclose(f);
		return -1;
	}

	return fclose(f);
}

int adc_start_buffering()
{
	return adc_set_buffering(1);
}
int adc_stop_buffering()
{
	return adc_set_buffering(0);
}

// chardev interface
FILE *adc_open_buffer()
{
	return fopen(ADC_CHARDEV_PATH, "rb");
}

int adc_close_buffer(FILE **adc)
{
	int retval;

	retval = fclose(*adc);
	*adc = NULL;

	return retval;
}


int adc_read_buffer(FILE *adc, uint16_t *buffer, uint32_t size)
{
	if(!adc)
		return 0;

	return fread((void*)buffer, 2, size, adc);
}


const char * hw_event_source_strs[5] = {"PRU", "TIMER4", "TIMER5", "TIMER6", "TIMER7"};
int adc_set_hw_event_source(enum adc_hw_event_source_t trigger)
{
	char tmp[256];
	FILE *f;

	tmp[255] = 0;
	snprintf(tmp,255, "%s/hw_trigger_source",ADC_IIO_PATH_BASE);
	f = fopen(tmp,"w");
	if(!f)
	{
		fprintf(stderr,"Cannot open .../hw_trigger_source file. (%s)\n",tmp);
		return -1;
	}

	if(fprintf(f,"%s", hw_event_source_strs[trigger]) < 1)
	{
		fprintf(stderr,"Cannot set hw trigger source.\n");
		fclose(f);
		return -1;
	}

	return fclose(f);

}