#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SP_COUNT 128 
#define LOG_FILE_NAME "timekeeper_sync_points.txt"

struct ref_point
{
	uint64_t raw;
	struct timespec local_time;
}

struct sync_point
{
	uint64_t raw;
	struct timespec global_time;
}

struct timekeeper
{
	struct ref_point sp[MAX_SP_COUNT];
	
	int last_sync_idx;
	int sync_point_cnt;
	
	int enable_logging;
	FILE *log;
}



// input structures
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct icap_source
{
	int (ts_getter*)(uint64_t *buffer, uint32_t size void* arg);
	int (ovf_getter*)(void *arg);
	void *arg;
}

int icap_get_ts(struct icap_source *icap, uint64_t *buffer, uint32_t size)
{
	return icap->ts_getter(buffer,size, icap->arg);
}

//<------------------- REAL HW SOURCE ------------------->
struct hw_icap_src
{
	struct icap_source icap;
	char *fname;
	int fileno;
}

int hw_icap_src_get_ts(uint64_t *buffer,uint32_t size, void *arg)
{
	struct hw_icap_stc* h = (struct hw_icap_stc*)arg;
	int rcv_cnt;
	
	rcv_cnt = read(h->fileno,buffer,size * sizeof(uint64_t));
	return rcv_cnt / sizeof(uint64_t);		
}

struct hw_icap_src *open_hw_icap_src(const char *fname)
{	
	struct hw_icap_src *i;
	int err=0;
	
	i = (struct hw_icap_stc*)malloc(sizeof(struct hw_icap_src));
	if(!i)
		return NULL;
	
	while(0)
	{
		// open file
		i->fileno = open(fname);
		if(IS_ERR(i->fileno)){
			err = 1; break;
		}
		
		i->fname = strdup(fname);
			
		i->icap.get_ts = hw_icap_src_get_ts;
		i->icap.arg = i;
	}
	if(err)
	{
			close_hw_icap_src(i);
			return NULL;
	}
	return i;	
}

void close_hw_icap_src(struct hw_icap_src *i)
{
	if(!i) return;
	if(i->fname) free(i->fname);
	if(i->fileno > 0) close(i->fileno);
	free(i);
}


//<------------------- VIRTUAL SOURCE ------------------->

struct virt_icap_src
{
	struct icap_source icap;
	uint64_t next_val;
	uint64_t period;
}

int virt_icap_src_get_ts(uint64_t *buffer,uint32_t size, void *arg)
{
	struct virt_icap_src* v = (struct virt_icap_src*)arg;
	if(size < 1) return 0;
	
	*buffer = v->next_val;
	v->next_val += v->period;
	return 1;
}

struct virt_icp_src *open_virt_icap_src(uint64_t first_val, uint64_t period)
{
	struct virt_icap_src *v;
	
	v = (struct virt_icap_src*)malloc(sizeof(struct vir_icap_src));
	if(!v) 
		return NULL;
	
	v->next_val = first_val;
	v->period = period;
	v->icap.get_ts = virt_icap_src_get_ts;
	v->icap.arg = v;
	return v;
}

void close_virt_icap_src(struct virt_icap_src* i)
{
	if(i)
		free(i);
}


// logging the timestamps coming from the capture channels and their timestamp difference
int main()
{
	char *channel_names[6] = {"tim5", "tim6", "tim7", "ecap0","ecap1","ecap2"};
	char *channel_paths[6] = {
								"/dev/timer5_icap",
								"/dev/timer6_icap",
								"/dev/timer7_icap",
								"/dev/ecap0_icap",
								"/dev/ecap1_icap",
								"/dev/ecap2_icap",
	} 

	uint64_t ts[6];

	struct hw_icap_src *ch[6];

	for(int i=0;i<6;i++)
		ch[i] = open_hw_icap_src(channel_paths[i]);


while(1)
{
	printf("\n<------------------------->")
	printf("Timestamps:\ntim5\t\ttim6\t\ttim7\t\tecap0\t\tecap1\t\tecap2\n");
	for(int i=0;i<6;i++)
	{
		icap_get_ts(&ch[i]->icap,&ts[i],1);
		printf("0x%016llx\t",ts[i]);
	}
	printf("\nDifferences:\n");
	for(int i=1;i<6;i++)
		printf("%llu\t",ts[i] - ts[0]);
	printf("\n");
}

}