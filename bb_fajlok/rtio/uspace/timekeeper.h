#ifndef __TIMEKEEPER_H_
#define __TIMEKEEPER_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "./servo/print.h"
#include "./servo/config.h"
#include "./servo/servo.h"
#include "./servo/pi.h"
#include "./circ_buf.h"


#define TIMEKEEPER_RT_PRIO			20
#define TIMEKEEPER_LOGGER_RT_PRIO	1


struct sync_point_t
{
	uint64_t local_ts;
	uint64_t global_ts;
	double adj;
	// measured timestamps
	// sync event = CPTS HW PUSH event
	uint64_t ptp_local;		// local timestamp of the sync event
	uint64_t ptp_global;	// ptp timestamp of the sync event
	uint64_t ptp_global_est;
};


#define TIMEKEEPER_SYNC_POINT_COUNT		1024

// struct containing local-global timestamp pairs 
// for the time conversion
struct timekeeper
{
	struct sync_point_t sync_points[TIMEKEEPER_SYNC_POINT_COUNT];
	int sync_point_buffer_size;
	unsigned head, tail;
	uint32_t sync_offset;
	pthread_mutex_t lock;
	uint64_t period;

	struct servo *s;
	enum servo_state state;

	pthread_t logger_thread;
	const char *log_name;
	struct circ_buf *log_buf;
};

struct timekeeper* timekeeper_create(int servo_type, double sync_interval, uint32_t sync_offset, const char *log_name);
void timekeeper_destroy(struct timekeeper *tk);

int timekeeper_add_sync_point(struct timekeeper *tk, uint64_t local_ts, uint64_t ptp_ts);
int timekeeper_convert(struct timekeeper *tk, uint64_t *local_ts, int ts_count);


#endif