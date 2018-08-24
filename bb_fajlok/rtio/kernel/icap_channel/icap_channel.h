#ifndef __ICAP_CHANNEL_H
#define __ICAP_CHANNEL_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/completion.h>


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
	int enabled;

	// chardev interface
	int blocking_read;
	struct miscdevice misc;

	// circular buffer for the input data
	struct circular_buffer circ_buf;
	int buffer_overflow;
	struct completion rcv_event;

	s32 offset;
	u64 ts_upper_bits;
	u64 ts_mask;
};

struct icap_channel * icap_create_channel(const char *name, u32 log_buf_size);
void icap_delete_channel(struct icap_channel *icap);

inline void icap_set_offset(struct icap_channel *icap, s32 offset) { icap->offset = offset; }
inline void icap_reset_ts(struct icap_channel *icap) { icap->ts_upper_bits = 0; }
inline void icap_set_ts_mask(struct icap_channel *icap, u64 ts_mask) } {icap->ts_mask = ts_mask;}
inline void icap_clear_buf_ovf(struct icap_channel *icap) { icap->buffer_overflow = 0;}

// producer interface
int icap_add_ts(struct icap_channel *icap, u32 ts);
void icap_signal_timer_ovf(struct icap_channel *icap);

// consumer interface
u32 icap_ts_count(struct icap_channel *icap);
int icap_get_ts(struct icap_channel *icap, u64*buf, size_t size);
int icap_try_get_ts(struct icap_channel *icap, u64*buf, size_t size);


#endif __ICAP_CHANNEL_H