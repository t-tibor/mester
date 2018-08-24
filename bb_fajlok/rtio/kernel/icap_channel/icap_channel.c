#include "icap_channel.h"

#include <linux/string.h>

struct file_operations icap_cdev_fops;

struct icap_channel * icap_create_channel(const char *name, u32 log_buf_size)
{
	struct icap_channel *icap;

	icap = kzalloc(sizeof(struct icap_channel), GFP_KERNEL);
	if(!icap) return NULL;

	icap->name = kstrdup(name, GFP_KERNEL);
	if(!icap->name) goto out1;

	icap->enabled = 1;

	icap->blocking_read = 0;

	// setup misc character device
 	icap->misc.fops = &icap_cdev_fops;
	icap->misc.minor = MISC_DYNAMIC_MINOR;
	icap->misc.name =icap->name;
	if(misc_register(&icap->misc))
	{
		pr_err("Couldn't initialize miscdevice /dev/%s.\n",icap->misc.name);
		goto out2;
	}

	// init timestamp buffer
	icap->circ_buf.length = (1<<log_buf_size);
	icap->circ_buf.head = icap->circ_buf.tail = 0;
	icap->circ_buf.buffer = kzalloc( (icap->circ_buf.length) * sizeof(u64),GFP_KERNEL);
	if(!icap->circ_buf.buffer)
	{
		pr_err("Cannot allocate memory for circular buffer wiht size: %u\n",icap->circ_buf.length);
		goto out3;
	}
	icap->buffer_overflow = 0;
	init_completion(&icap->icap->rcv_event);

	icap->offset = 0;
	icap->ts_upper_bits = 0;
	icap->ts_mask = 0xFFFFFFFF; // default: 32 bit timestamps are accepted

	return icap;

	out3:
		misc_deregister(&icap->misc);
	out2:
		kfree(icap->name);
	out1:
		kfree(icap);
	return NULL;
}

void icap_delete_channel(struct icap_channel *icap)
{
	kfree(icap->circ_buf.buffer);
	misc_deregister(&icap->misc);
	kfree(icap->name);
	kfree(icap);
}


// producer interface
int icap_add_ts(struct icap_channel *icap, u32 ts)
{
	u64 t = icap->ts_upper_bits | (u64)ts;
	if(!icap->enabled) return -2;

	head = icap->circ_buf.head;
	tail = READ_ONCE(icap->circ_buf.tail);

	if (CIRC_SPACE(icap->circ_buf.head, icap->circ_buf.tail, icap->circ_buf.length) >= 1) 
	{
		/* insert one item into the buffer */
		icap->circ_buf.buffer[head] = t;

		smp_store_release(&icap->circ_buf.head,
				  (head + 1) & (icap->circ_buf.length - 1));
		return 0;

	}
	else
	{
		WRITE_ONCE(icap->buffer_overflow,icap->buffer_overflow+1);
		return -1;
	}
}

void icap_signal_timer_ovf(struct icap_channel *icap)
{
	icap->ts_upper_bits += ((u64)(icap->ts_mask))+1;
}

// consumer interface
u32 icap_ts_count(struct icap_channel *icap)
{
	u32 head, tail, size;
    int str_len;

	head = READ_ONCE(icap->circ_buf.head);
	tail = READ_ONCE(icap->circ_buf.tail);
	size = CIRC_CNT(head,tail,icap->circ_buf.length);

    return size;
}

int icap_get_ts(struct icap_channel *icap, u64*buf, size_t size)
{

}

int icap_try_get_ts(struct icap_channel *icap, u64*buf, size_t size)
{
	
}

// misc deevice interface
struct file_operations icap_cdev_fops = 
{

};

