#include <inttypes.h>
#include <string.h>

#include "timekeeper.h"

//#define VERBOSE 

struct timekeeper* timekeeper_create(int servo_type, double sync_interval, uint32_t sync_offset, const char *log_name)
{
	struct timekeeper *ret;
	enum servo_type st;

	ret = (struct timekeeper*)malloc(sizeof(struct timekeeper));
	if(!ret)
		return NULL;

	switch(servo_type)
	{
		case 0:
			st = CLOCK_SERVO_PI;
			break;
		case 1:
			st = CLOCK_SERVO_LINREG;
			break;
		default:
			st = CLOCK_SERVO_PI;
	}
	ret->s = servo_create(st, 0, MAX_FREQUENCY,0);
	if(!ret->s)
	{
		free(ret);
		return NULL;
	}
	ret->state = SERVO_UNLOCKED;
	servo_sync_interval(ret->s, sync_interval);
	servo_reset(ret->s);

	if(pthread_mutex_init(&ret->lock, NULL) != 0)
	{
		servo_destroy(ret->s);
		free(ret);
		return NULL;
	}

	ret->sync_offset = sync_offset;
	ret->head = 0;
	ret->tail = 0;
	ret->sync_point_buffer_size = TIMEKEEPER_SYNC_POINT_COUNT;

	memset(&ret->sync_points[0],0,sizeof(struct sync_point_t));
	ret->sync_points[0].local_ts = sync_offset;

	if(log_name)
	{
		ret->log = fopen(log_name,"w");
	}
	return ret;
}

void timekeeper_destroy(struct timekeeper* tk)
{
	if(!tk) return;
	if(tk->log)
		fclose(tk->log);

	pthread_mutex_destroy(&tk->lock);
	servo_destroy(tk->s);
	free(tk);
}



// local_ts is supposed to be an increasingly ordered list
int timekeeper_convert(struct timekeeper *tk, uint64_t *local_ts, int ts_count)
{
	unsigned  head, tail;
	unsigned idx;
	uint64_t sync_loc, sync_glob;
	double adj;
	double dt;
	int64_t delta;

	// get pointers
	// they are velid for 10 secs
	pthread_mutex_lock(&tk->lock);
	head = tk->head;
	tail = tk->tail;
	pthread_mutex_unlock(&tk->lock);


	idx = head;
	for(int i=ts_count-1;i>=0;i--)
	{
		while((local_ts[i] < tk->sync_points[idx].local_ts) && (idx != tail))
		{
			idx = (idx - 1) & (tk->sync_point_buffer_size-1);
		}
		if(idx == tail) return -1;

		sync_loc = tk->sync_points[idx].local_ts;
		sync_glob = tk->sync_points[idx].global_ts;
		adj = tk->sync_points[idx].adj;

		delta = (int64_t)(local_ts[i] - sync_loc);
		dt = (double)(delta)*adj;
		local_ts[i] = sync_glob + (local_ts[i] - sync_loc);
		local_ts[i] += (int64_t)dt;
	}
	return 0;
}


int timekeeper_add_sync_point(struct timekeeper *tk, uint64_t local_ts, uint64_t ptp_ts)
{
	struct sync_point_t *last_sp = &tk->sync_points[tk->head];
	struct sync_point_t *new_sp;
	uint64_t glob_est;
	double adj;
	int64_t offset;
	int64_t delta;
	double dt;


	//calculate estimated global time for the given local timestamp
	delta = (int64_t)(local_ts - last_sp->local_ts);
	dt = (double)(delta)*last_sp->adj;
	glob_est = last_sp->global_ts + (local_ts - last_sp->local_ts);
	glob_est += (int64_t)dt;
	//fprintf(stderr,"Exta for est : %"PRId64"\n",(int64_t)dt);
	offset = glob_est - ptp_ts;
	// feed the offset between the estimation and the real timestamp into the servo
	adj = servo_sample(tk->s,offset,local_ts,1,&tk->state);


	// compose the new sync point, that is 1ms after the local_ts (trying to make sure, that the sync point is in the future)
	int new_head = (tk->head+1) & (tk->sync_point_buffer_size-1);
	new_sp = &tk->sync_points[new_head];
	// save the measured timestamps for logging
	new_sp->ptp_local = local_ts;
	new_sp->ptp_global = ptp_ts;
	// save the sync point timestamps
	new_sp->local_ts = local_ts + tk->sync_offset;

	delta =  (int64_t)(new_sp->local_ts - last_sp->local_ts); 
	dt = (double)(delta)*last_sp->adj;
	new_sp->global_ts = last_sp->global_ts + (new_sp->local_ts - last_sp->local_ts);
	new_sp->global_ts += (int64_t)dt;
	//fprintf(stderr,"Exta for new sp : %"PRId64"\n",(int64_t)dt);

	switch (tk->state) {
	case SERVO_UNLOCKED:
		new_sp->adj = last_sp->adj;
		break;
	case SERVO_JUMP:
		new_sp->adj = -adj*1e-9;
		new_sp->global_ts -= offset;
		break;
	case SERVO_LOCKED:
		new_sp->adj = -adj*1e-9;
		break;
	}

	
	int tail = tk->tail, new_tail;
	// if empty place is less than 30, then increase the tail
	if(((new_head-tail+1 + tk->sync_point_buffer_size) & (tk->sync_point_buffer_size-1))  > tk->sync_point_buffer_size - 30)
		new_tail = (new_head+30) & (tk->sync_point_buffer_size-1);
	else
		new_tail = tail;

	// oldest timestamp is kept for approx. 10 secs before overwriting it.
	// so timekeeper_convert function has to finish in less than 10 secs.

	pthread_mutex_lock(&tk->lock);
	tk->head = new_head;
	tk->tail = new_tail;
	pthread_mutex_unlock(&tk->lock);

#ifdef VERBOSE
	fprintf(stdout,"New sync point:\n"
					"\tlocal time:           %"PRIu64"\n"
					"\testimated glob. time: %"PRIu64"\n"
					"\tptp time:             %"PRIu64"\n"
					"\toffset:               %"PRId64"\n"
					"\tadj:                  %lf\n"
					"\tprev adj:             %lf\n"
					"\tsync local:  %"PRIu64"\n"
					"\tsync global: %"PRIu64"\n"
					"\tServo state: %d\n" ,new_sp->ptp_local, glob_est, new_sp->ptp_global, offset, adj,-last_sp->adj*1e9, new_sp->local_ts, new_sp->global_ts,tk->state);
#endif

	fprintf(tk->log,"%"PRIu64",%"PRIu64",%"PRIu64",%lf\n",ptp_ts,local_ts,glob_est,adj);

	return 0;
}

