#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "servo/print.h"
#include "servo/config.h"
#include "servo/servo.h"
#include "servo/pi.h"


#include "timekeeper.h"

#define DATA_IN_FILE "../meas/PPS_log/timekeeper_20.log"
#define DATA_OUT_BASE  "../meas/"

#define TIMEKEEPER_LOG "../meas/tk_log.txt"

uint64_t *ptp;
uint64_t *hw;
uint64_t *est;




int file_count_lines(const char *fname)
{
	char buf[256];
	int line_cnt = 0;

	FILE *f = fopen(fname,"r");
	if(!f)
		return -1;

	while(fgets(buf,255,f))
		line_cnt++;
	fclose(f);

	return line_cnt;

}

int load_data()
{
	FILE *fin;
	char line[255];
	int line_cnt = 0;
	int idx = 0;
	int ret;

	int len = file_count_lines(DATA_IN_FILE);
	if(len < 0)
		return -1;

	ptp = (uint64_t*)malloc(len * sizeof(uint64_t));
	hw = (uint64_t*)malloc(len * sizeof(uint64_t));

// READ up the input data
	fin = fopen(DATA_IN_FILE,"r");
	while(fgets(line,255,fin))
	{
		line_cnt++;
		if(strlen(line) <5)
		{
			fprintf(stderr,"Short line @ line %d.\n",line_cnt);
			continue;
		}

		ret = sscanf(line, "%" SCNu64 ",%" SCNu64 "," ,&ptp[idx],&hw[idx]);
		if(ret != 2)
		{
			fprintf(stderr,"Cannot fill both value from line %d.\n",line_cnt);
		}
		idx++;
	}

	printf("First 10 timestamps:\n");
	for(int i=0;i<10;i++)
		printf("%lu\t%lu\n",ptp[i],hw[i]);
	fclose(fin);

	return idx;
}


int export_data(char * fname, uint64_t* ptp, uint64_t* hw, uint64_t* est, int dataCnt)
{
	FILE *f;
	f = fopen(fname,"w");
	if(!f)
		return -1;

	for(int i=0;i<dataCnt;i++)
	{
		fprintf(f,"%lu,%lu,%lu\n",ptp[i],hw[i],est[i]);
	}

	fclose(f);
	return 0;
}


int read_offset(int argc, char ** argv)
{
	char *time;
	int len;
	char suffix;
	uint64_t mult;
	uint64_t offset;
	int ret;

	if(argc < 2) return -1;

	time = argv[1];
	len = strlen(time);
	if(len < 2) return -1;

	//last char is suffix
	suffix = time[len-1];
	switch(suffix)
	{
		case 'n':
			mult = 1;
			break;
		case 'u':
			mult = 1e3;
			break;
		case 'm':
			mult = 1e6;
			break;
		default:
			mult = 1e9;
		break;	
	}

	ret = sscanf(time,"%"SCNu64,&offset);
	if(ret != 1)
		return -1;

	offset *= mult;

	return offset;
}

void gen_fout_name(char *buffer, uint64_t offset)
{
	char  suffexes[3] = {'n','u','m'};
	int s = 0;

	while(offset >= 1000 && (offset%1000 == 0) && s < 2)
	{
		offset /= 1000;
		s++;
	}


	sprintf(buffer, DATA_OUT_BASE "servo_delayed_%" PRIu64 "%cs.txt",offset,suffexes[s]);
}

int main(int argc, char **argv)
{
	int ret;
	char c;
	int run;

	uint64_t T_next_master;
	uint64_t T_next;
	uint64_t t_next;
	int dataCnt;
	double tmp;
	double dev ,adj;
	int64_t offset;
	struct servo *s;
	enum servo_state state ;
	int64_t servo_sync_offset;
	char fout[256];
	double dt;

// read controller offset from command line
	servo_sync_offset = read_offset(argc, argv);
	if(servo_sync_offset == -1)
	{
		printf("Invalid offset.\n");
		return -1;
	}
	gen_fout_name(fout,servo_sync_offset);
	printf("Applying servo offset: %" PRIu64 "ns\n",servo_sync_offset);

// load data
	dataCnt = load_data();
	if(dataCnt < 0)
	{
		fprintf(stderr,"Cannot import data.\n");
		return -1;
	}
	est = (uint64_t*)malloc(dataCnt*sizeof(uint64_t));


// Create servo
	print_set_level(PRINT_LEVEL_MAX);
	print_set_verbose(1);
	s = servo_create(CLOCK_SERVO_PI, 0, MAX_FREQUENCY,0);
	state = SERVO_UNLOCKED;

	if(!s)
		fprintf(stderr,"Cannot create servo.\n");
	servo_reset(s);

	tmp = (hw[1] - hw[0])/1e9;
	fprintf(stderr,"Setting interval to: %lf\n",tmp);
	servo_sync_interval(s,tmp);


// Create timekeeper
	struct timekeeper *tk = timekeeper_create(CLOCK_SERVO_PI,tmp, servo_sync_offset,TIMEKEEPER_LOG);
// feed data into the servo
	uint64_t	T_sync;
	uint64_t t_sync;

	dev = 0;
	c = 0;
	run = 0;


	T_sync = 0;
	t_sync = servo_sync_offset;

	for(int i=0;i<dataCnt;i++)
	{
		t_next = hw[i];
		T_next_master = ptp[i];

		timekeeper_add_sync_point(tk, t_next, T_next_master);

		dt = (double)(t_next - t_sync) * dev*1e-9;
		T_next = T_sync + (t_next - t_sync);
		T_next += (uint64_t)(dt);
		
		offset = ((int64_t)T_next - (int64_t)T_next_master);

		dt = ((double)(t_next + servo_sync_offset - t_sync)) * dev * 1e-9;
		T_sync +=(t_next + servo_sync_offset - t_sync);
		T_sync += (uint64_t)(dt);
		t_sync = t_next + servo_sync_offset;

/*
		// estimated global time for the next sync point
		T_next = T_prev + (t_next - t_prev)*(1+dev*1e-9);
		// measured global time for the next sync point
		T_next_master = ptp[i];
		offset = ((int64_t)T_next - (int64_t)T_next_master);
*/


		adj = servo_sample(s,offset,T_next,1,&state);


		if(!run)  printf("\t T_master: %lu, T_est: %lu, offset: %ld, adj: %lf, dev: %lf\n",T_next_master,T_next,offset,adj,dev);

		switch (state) {
		case SERVO_UNLOCKED:
		if(!run) printf("State: UNLOCKED");
			break;
		case SERVO_JUMP:
		if(!run)  printf("State: JUMP");
			dev = -adj;
			T_sync -= offset;
			break;
		case SERVO_LOCKED:
		if(!run)  printf("State: LOCKED");
			dev = -adj;
			break;
		}

		

		// save current timestamps
		est[i] = T_next;


		if(!run) 
		{
			c = getc(stdin);
			if(c=='c') run = 1;
		}


	}


// save data
	ret = export_data(fout, ptp,hw,est,dataCnt);
	if(ret)
	{
		fprintf(stderr,"Cannot save data.\n");
		return -1;
	}

	printf("Output data saved to file: %s\n",fout);


// run timekeeper covert test
	uint64_t test_hw[100];
	uint64_t test_glob[100];
	unsigned idx = (tk->head-10) & (tk->sync_point_buffer_size-1);
	for(int i=0;i<10;i++)
	{
		for(int j=0;j<10;j++)
		{
			test_hw[10*i+j] = tk->sync_points[idx].ptp_local + j * 20000000;
			test_glob[10*i+j] = tk->sync_points[idx].ptp_local + j * 20000000;
		}

		idx = (idx + 1) & (tk->sync_point_buffer_size-1);
	}

	timekeeper_convert(tk,test_glob,100);

	FILE * tk_test = fopen("../meas/tk_test.log","w");
	for(int i=0;i<100;i++)
		fprintf(tk_test,"%" PRIu64 " ,%"PRIu64"\n",test_hw[i],test_glob[i]);
	fclose(tk_test);


	timekeeper_destroy(tk);
	return 0;

}