#ifndef __ICAP_CHANNEL_H
#define __ICAP_CHANNEL_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/miscdevice.h>

struct circular_buffer
{
	u64 *buffer;
	u32 head;
	u32 tail;
	u32 length;
};

struct icap_channel{
	const char* name;

	// chardev interface
	struct mutex channel_lock;
	struct miscdevice misc;
	wait_queue_head_t rq;

	// circular buffer for the input data
	struct circular_buffer circ_buf;
	
	// channel parameters
	struct spinlock param_lock;
	int buffer_overflow;
	u64 ts_upper_bits;
	u32 ts_mask;
	u8 ts_store_state;

	// interrupt bottom half
	struct tasklet_struct bh;
};

struct icap_channel * icap_create_channel(const char *name, u32 log_buf_size);
void icap_delete_channel(struct icap_channel *icap);

// parameter setters / getters
void icap_reset_ts(struct icap_channel *icap);
void icap_set_ts_mask(struct icap_channel *icap, u64 ts_mask);
void icap_ts_store_enable(struct icap_channel *icap);
void icap_ts_store_disable(struct icap_channel *icap);
int icap_channel_is_ts_store_enabled(struct icap_channel *icap);
void icap_clear_buf_ovf(struct icap_channel *icap);
int icap_get_buf_ovf(struct icap_channel *icap);

// producer interface
int icap_add_ts(struct icap_channel *icap, u32 ts, int ovf);
void icap_signal_timer_ovf(struct icap_channel *icap);

// consumer interface
u32 icap_get_ts_count(struct icap_channel *icap);


#endif //__ICAP_CHANNEL_H