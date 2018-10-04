#ifndef __CIRC_BUF_H_
#define __CIRC_BUF_H_

#include <semaphore.h>


struct circ_buf
{
	void *buffer;
	unsigned block_size;
	unsigned block_count; // it has to be power of 2
	volatile unsigned char head;
	volatile unsigned char  tail;

	int ovf_count;

	int burst_size;;
	int write_cnt;
	int data_avail;
	

	sem_t reader_block;
};

struct circ_buf *circ_buf_create(unsigned  block_count, unsigned block_size);
void circ_buf_destroy(struct circ_buf *b);

int circ_buf_push(struct circ_buf *b, void *elem);
int circ_buf_pop(struct circ_buf *b, void *elem);

int circ_buf_is_empty(struct circ_buf *b);
int circ_buf_is_full(struct circ_buf *b);
int circ_buf_get_cnt(struct circ_buf *b);

int circ_buf_get_ovf(struct circ_buf *b);
int circ_buf_wake_consumer(struct circ_buf *b);
#endif