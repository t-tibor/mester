#ifndef __ADC_H_
#define __ADC_H_

#include <stdlib.h>
#include <stdio.h>
#include "icap_channel.h"

#define ADC_IIO_PATH_BASE		"/sys/bus/iio/devices/iio:device0"
#define ADC_CHARDEV_PATH 		"/dev/iio:device0"


// sysfs interface
int adc_enable_channel_buffering(unsigned channel);
int adc_disable_channel_buffering(unsigned channel);

int adc_set_buffer_size(unsigned size);
int adc_start_buffering();
int adc_stop_buffering();

// chardev interface
FILE *adc_open_buffer();
int adc_close_buffer(FILE **adc);
int adc_read_buffer(FILE *adc, uint16_t *buffer, uint32_t size);

// adc trigger
enum adc_hw_event_source_t {ADC_TRRIGGER_PRU = 0, ADC_TRIGGER_TIMER4, ADC_TRIGGER_TIMER5, ADC_TRIGGER_TIMER6, ADC_TRIGGER_TIMER7};
int adc_set_hw_event_source(enum adc_hw_event_source_t trigger);

#endif