#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "./servo/print.h"
#include "./servo/config.h"
#include "./servo/servo.h"
#include "./servo/pi.h"

#define DATA_IN_FILE "log.txt"
#define DATA_OUT_FILE "../meas/servo.txt"

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

		ret = sscanf(line, "%" SCNu64 ",%" SCNu64 "\n" ,&ptp[idx],&hw[idx]);
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

int export_data(uint64_t* ptp, uint64_t* hw, uint64_t* est, int dataCnt)
{
	FILE *f;
	f = fopen(DATA_OUT_FILE,"w");
	if(!f)
		return -1;

	for(int i=0;i<dataCnt;i++)
	{
		fprintf(f,"%lu,%lu,%lu\n",ptp[i],hw[i],est[i]);
	}

	fclose(f);
	return 0;
}


int main()
{
	int ret;
	char c;
	int run;

	uint64_t T_next_master;
	uint64_t T_prev, T_next;
	uint64_t t_prev, t_next;
	int dataCnt;
	double tmp;
	double dev ,adj;
	int64_t offset;
	struct servo *s;
	enum servo_state state ;


// load data
	dataCnt = load_data();
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


	T_prev = 0;
	t_prev = 0;
	dev = 0;
	c = 0;
	run = 0;
	for(int i=0;i<dataCnt;i++)
	{
		t_next = hw[i];
		// estimated global time for the next sync point
		T_next = T_prev + (t_next - t_prev)*(1+dev*1e-9);
		// measured global time for the next sync point
		T_next_master = ptp[i];

		offset = ((int64_t)T_next - (int64_t)T_next_master);

		adj = servo_sample(s,offset,T_next,1,&state);

		if(i==5)
			servo_reset(s);

		switch (state) {
		case SERVO_UNLOCKED:
		if(!run) printf("State: UNLOCKED");
			break;
		case SERVO_JUMP:
		if(!run)  printf("State: JUMP");
			dev = -adj;
			T_next -= offset;
			break;
		case SERVO_LOCKED:
		if(!run)  printf("State: LOCKED");
			dev = -adj;
			break;
		}

		if(!run)  printf("\t T_master: %lu, T_est: %lu, offset: %ld, adj: %lf, dev: %lf\n",T_next_master,T_next,offset,adj,dev);

		// save current timestamps
		est[i] = T_next;

		T_prev = T_next;
		t_prev = t_next;

		if(!run) 
		{
			c = getc(stdin);
			if(c=='c') run = 1;
		}


	}

// save data
	ret = export_data(ptp,hw,est,dataCnt);
	if(ret)
		fprintf(stderr,"Cannot save data.\n");

	return 0;

}