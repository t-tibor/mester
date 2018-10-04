#include <string.h>
#include <stdlib.h>

#include "circ_buf.h"

// struct circ_buf
// {
// 	void *buffer;
// 	unsigned block_size;
// 	unsigned block_count; // it has to be power of 2
// 	volatile uint32_t head;
// 	volatile uint32_t tail;

// 	int ovf_count;
// 	int write_cnt;
// 	sem_t reader_block;
// };

struct circ_buf *circ_buf_create(unsigned block_count, unsigned block_size)
{
	struct circ_buf *b;
	b = (struct circ_buf*)malloc(sizeof(struct circ_buf));
	if(!b)
		goto err1;

	b->buffer = calloc(block_count, block_size);
	if(!b->buffer)
		goto err2;

	b->block_size = block_size;
	b->block_count = block_count;
	b->head = 0;
	b->tail = 0;
	b->ovf_count = 0;
	b->burst_size = b->block_count/4;
	b->write_cnt=0;
	b->data_avail=0;
	if(sem_init(&b->reader_block,0,0))
		goto err3;

	return b;

	err3:
		free(b->buffer);
	err2:
		free(b);
	err1:
		return NULL;
}

void circ_buf_destroy(struct circ_buf *b)
{
	sem_destroy(&b->reader_block);
	free(b->buffer);
	free(b);
}

int circ_buf_push(struct circ_buf *b, void *elem)
{
	unsigned char head = b->head & (b->block_count-1);
	void *wp = (unsigned char*)b->buffer + head*b->block_size;

	if(circ_buf_is_full(b))
	{
		b->ovf_count++;
		return -1;
	}

	memcpy(wp, elem, b->block_size);

	__sync_synchronize();

	b->head = b->head + 1;

	__sync_synchronize();

	b->write_cnt++;
	if(b->write_cnt >= b->burst_size)
	{
		sem_post(&b->reader_block);
		b->write_cnt=0;
	}

	return 0;
}

int circ_buf_pop(struct circ_buf *b, void *elem)
{
	unsigned char tail = b->tail & (b->block_count-1);
	void *rp = (unsigned char*)b->buffer + tail*b->block_size;

	if(b->data_avail==0)
	{
		// wait for new burst to arrive
		sem_wait(&b->reader_block);
		b->data_avail = b->burst_size;
	}

	if(circ_buf_is_empty(b))
		return -1;

	memcpy(elem,rp,b->block_size);
	b->data_avail--;

	__sync_synchronize();

	b->tail = b->tail + 1;
	return 0;

}

int circ_buf_is_empty(struct circ_buf *b)
{
	return (b->head == b->tail);
}

int circ_buf_is_full(struct circ_buf *b)
{
	return (((b->tail - b->head)&(b->block_count-1)) == 1);
}

int circ_buf_get_cnt(struct circ_buf *b)
{
	return (b->head - b->tail) & (b->block_count-1);

}

int circ_buf_get_ovf(struct circ_buf *b)
{
	return b->ovf_count;
}

int circ_buf_wake_consumer(struct circ_buf *b)
{
	return sem_post(&b->reader_block);
}





//#define UNIT_TEST


#ifdef UNIT_TEST

#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>

void *producer_job(void *arg)
{
	struct circ_buf *b = (struct circ_buf*)arg;

	for(int i=0;i<100000;i++)
	{
		for(int j=0;j<256;j++)
		{
			unsigned char tmp = j;

			while(circ_buf_is_full(b))
			{
				sched_yield();
			}

			circ_buf_push(b,&tmp);
		}


		if((i%10000)==0)
		{
			printf("\rRound: %d, ovf:%d\n.",i,b->ovf_count);
		}

	}

	circ_buf_wake_consumer(b);

	return NULL;
}

void *consumer_job(void *arg)
{
	struct circ_buf *b = (struct circ_buf*)arg;
	int ret;

	for(int i=0;i<1000000;i++)
	{
		for(int j=0;j<256;j++)
		{
			unsigned char tmp;
			ret=circ_buf_pop(b,&tmp);
			if(ret || (tmp != j))
				printf("Circbuffer error ret= %d, @ i=%d, j=%d, got:%d, tail:%d, ovf: %d\n",ret,i,j,tmp,b->tail,b->ovf_count);
		}

		if(b->tail != 0)
			printf("Tail error.\n");
	}
	return NULL;

}

// buffer test
pthread_t producer;
pthread_t consumer;

int main()
{
	struct circ_buf *buf = circ_buf_create(256,1);
	void *ret;
	// run circular buffer test on 2 threads
	pthread_create(&producer,NULL,producer_job,(void*)buf);
	pthread_create(&consumer,NULL,consumer_job,(void*)buf);

	pthread_join(producer,&ret);
	pthread_join(consumer,&ret);

	printf("ovf count: %d.\n",buf->ovf_count);
	printf("test end\n");
	
	return 0;
}

#endif