#include "icap_channel.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uaccess.h>

#include <linux/miscdevice.h>
#include <asm/ioctl.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/interrupt.h>


// TODO
// rethink channel deletion (maybe there are still readers)



struct file_operations icap_cdev_fops;
void icap_channel_bh(unsigned long arg);

struct icap_channel * icap_create_channel(const char *name, u32 log_buf_size)
{
	struct icap_channel *icap;

	icap = kzalloc(sizeof(struct icap_channel), GFP_KERNEL);
	if(!icap) return NULL;

	icap->name = name;
	if(!icap->name) goto out1;

	mutex_init(&icap->channel_lock);


	// init timestamp buffer
	icap->circ_buf.head = icap->circ_buf.tail = 0;
	icap->circ_buf.length = 1ULL<<log_buf_size;
	icap->circ_buf.buffer = (u64*)kzalloc((icap->circ_buf.length) * sizeof(u64) , GFP_KERNEL);
	if(!icap->circ_buf.buffer)
	{
		pr_err("Cannot allocate memory for circular buffer with size: %uB\n",icap->circ_buf.length);
		goto out2;
	}

	init_waitqueue_head(&icap->rq);

	icap->buffer_overflow = 0;

	//parameters
	icap->ts_store_state = 1;
	icap->ts_upper_bits = 0;
	icap->ts_mask = 0xFFFFFFFF; // default: 32 bit timestamps are accepted
	spin_lock_init(&icap->param_lock);

	tasklet_init(&icap->bh, icap_channel_bh, (unsigned long)icap);


	// setup misc character device
 	icap->misc.fops = &icap_cdev_fops;
	icap->misc.minor = MISC_DYNAMIC_MINOR;
	icap->misc.name =icap->name;
	if(misc_register(&icap->misc))
	{
		pr_err("Couldn't initialize miscdevice /dev/%s.\n",icap->misc.name);
		goto out3;
	}


	return icap;

	out3:
		tasklet_disable(&icap->bh); // wait for the tasklets to finish
		kfree(icap->circ_buf.buffer);
	out2:
	out1:
		kfree(icap);
	return NULL;
}

void icap_delete_channel(struct icap_channel *icap)
{

	misc_deregister(&icap->misc);
	tasklet_disable(&icap->bh); // wait for the tasklets to finish
	kfree(icap->circ_buf.buffer);
	kfree(icap);
}

// parameter setters / getters
void icap_reset_ts(struct icap_channel *icap) 
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	icap->ts_upper_bits = 0;
	spin_unlock_irqrestore(&icap->param_lock, flags);
}

 
void icap_set_ts_mask(struct icap_channel *icap, u64 ts_mask)
{
	WRITE_ONCE(icap->ts_mask,ts_mask);
}

void icap_channel_ts_store_enable(struct icap_channel *icap)
{
	WRITE_ONCE(icap->ts_store_state,1);
}
void icap_channel_ts_store_disable(struct icap_channel *icap)
{
	WRITE_ONCE(icap->ts_store_state,0);
	// wake up the sleeping processes
	wake_up_interruptible(&icap->rq);
}

int icap_channel_is_ts_store_enabled(struct icap_channel *icap)
{
	return icap->ts_store_state;
}

void icap_clear_buf_ovf(struct icap_channel *icap)
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->buffer_overflow, 0);
	spin_unlock_irqrestore(&icap->param_lock, flags);
}

int icap_get_buf_ovf(struct icap_channel *icap)
{
	int ovf;
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	ovf = icap->buffer_overflow;
	spin_unlock_irqrestore(&icap->param_lock, flags);
	return ovf;
}

// producer interface
int icap_add_ts(struct icap_channel *icap, u32 ts, int ovf)
{
	u64 t;
	u32 ts_mask;
	u32 head,tail;

	if(!icap->ts_store_state) return -1;

	ts_mask = icap->ts_mask;
	t = (icap->ts_upper_bits) | (u64)((ts) & ts_mask);

	if((ovf) && (ts < (ts_mask >> 1)))
	{
		t += (u64)(ts_mask)+1;
	}

	head = icap->circ_buf.head;
	tail = icap->circ_buf.tail;

	if (CIRC_SPACE(icap->circ_buf.head, icap->circ_buf.tail, icap->circ_buf.length) >= 1) 
	{
		/* insert one item into the buffer */
		icap->circ_buf.buffer[head] = t;
		icap->circ_buf.head = (head + 1) & (icap->circ_buf.length - 1);
		tasklet_schedule(&icap->bh);
		return 0;
	}
	else
	{
		icap->buffer_overflow++;
		return -2;
	}
}

void icap_channel_bh(unsigned long arg)
{
	struct icap_channel *icap = (struct icap_channel*)arg;
	wake_up_interruptible(&icap->rq);
}

void icap_signal_timer_ovf(struct icap_channel *icap)
{
	icap->ts_upper_bits += ((u64)(icap->ts_mask))+1;
}

// consumer interface
u32 icap_get_ts_count(struct icap_channel *icap)
{
	u32 head, tail, size;

	head = READ_ONCE(icap->circ_buf.head);
	tail = READ_ONCE(icap->circ_buf.tail);
	size = CIRC_CNT(head,tail,icap->circ_buf.length);

    return size;
}

void icap_flush_buffer(struct icap_channel *icap)
{
	u32 head;
	head = READ_ONCE(icap->circ_buf.head);
	WRITE_ONCE(icap->circ_buf.tail, head);
}


static ssize_t icap_cdev_open(struct inode *inode, struct file *pfile)
{
	if(!try_module_get(THIS_MODULE))
	{
		pr_err("Cannot increase module ref counter.\n");
		return -ENOENT;
	}
	return 0;
}

static int icap_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}

#define ICAP_IOCTL_MAGIC	'9'

#define ICAP_IOCTL_GET_OVF			_IO(ICAP_IOCTL_MAGIC,1)
#define ICAP_IOCTL_CLEAR_OVF		_IO(ICAP_IOCTL_MAGIC,2)
#define ICAP_IOCTL_RESET_TS			_IO(ICAP_IOCTL_MAGIC,3)
#define ICAP_IOCTL_SET_TS_BITNUM	_IO(ICAP_IOCTL_MAGIC,4)
#define ICAP_IOCTL_STORE_EN			_IO(ICAP_IOCTL_MAGIC,5)
#define ICAP_IOCTL_STORE_DIS		_IO(ICAP_IOCTL_MAGIC,6)
#define ICAP_IOCTL_FLUSH			_IO(ICAP_IOCTL_MAGIC,7)

#define ICAP_IOCTL_MAX				7


static long icap_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct icap_channel *icap = container_of(misc_dev, struct icap_channel, misc);
	long ret = 0;


	if (_IOC_TYPE(cmd) != ICAP_IOCTL_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > ICAP_IOCTL_MAX) return -ENOTTY;

	if(mutex_lock_interruptible(&icap->channel_lock))
		return -ERESTARTSYS;

	switch(cmd)
	{
		case ICAP_IOCTL_GET_OVF:
			ret = icap_get_buf_ovf(icap);
			break;
		case ICAP_IOCTL_CLEAR_OVF:
			icap_clear_buf_ovf(icap);
			break;
		case ICAP_IOCTL_RESET_TS:
			icap_reset_ts(icap);
			break;
		case ICAP_IOCTL_SET_TS_BITNUM:
			icap_set_ts_mask(icap,((1ULL) << arg)-1);
			break;
		case ICAP_IOCTL_STORE_EN:
			icap_channel_ts_store_enable(icap);
			break;
		case ICAP_IOCTL_STORE_DIS:
			icap_channel_ts_store_disable(icap);
			break;
		case ICAP_IOCTL_FLUSH:
			icap_flush_buffer(icap);
			break;
		default:
			pr_err("Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}

	mutex_unlock(&icap->channel_lock);

	return ret;
}

static ssize_t icap_cdev_read (struct file *pfile, char __user *buff, size_t len, loff_t *ppos)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct icap_channel * icap = container_of(misc_dev, struct icap_channel, misc);
	struct circular_buffer *circ = &icap->circ_buf;

	u32 head, tail, data_cnt, buff_capa, data_to_read, data_to_end, step1_length, step2_length;
	int eof;
	ssize_t ret;

	// wait data to arrive
	if(mutex_lock_interruptible(&icap->channel_lock))
		return -ERESTARTSYS;
	while(icap_get_ts_count(icap) == 0)
	{
		eof = !icap_channel_is_ts_store_enabled(icap);
		mutex_unlock(&icap->channel_lock);

		if(pfile->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if(eof) return 0;

		if(wait_event_interruptible(icap->rq,((icap_get_ts_count(icap) == 0) && (icap_channel_is_ts_store_enabled(icap))) ))
			return -ERESTARTSYS;

		if(mutex_lock_interruptible(&icap->channel_lock))
			return -ERESTARTSYS;
	}

	
	// get data count from the circular buffer
	head = smp_load_acquire(&circ->head);
	tail = circ->tail;
	data_cnt = CIRC_CNT(head,tail,circ->length);
	buff_capa = len/sizeof(u64);
	data_to_read = min(data_cnt, buff_capa);
	data_to_end = CIRC_CNT_TO_END(head,tail,circ->length);
	step1_length = min(data_to_read,data_to_end);
	step2_length = data_to_read - step1_length;


	ret = 0;
	if(WARN_ON(data_to_read == 0))
	{
		goto out;
	}

	// worst case we need to copy the data in 2 steps, if the data block is at the end of the buffer
	if(copy_to_user(buff,&circ->buffer[tail],step1_length*sizeof(u64)))
	{
		ret = -EFAULT;
		goto out;
	}

	smp_store_release(&circ->tail,
			  (tail + step1_length) & (circ->length - 1));


	if(step2_length > 0)
	{
		if(copy_to_user(buff+step1_length*sizeof(u64),&circ->buffer[tail],step2_length*sizeof(u64)))
		{
			ret = step1_length * sizeof(u64);
			goto out;
		}

		smp_store_release(&circ->tail,
			  (tail + step2_length) & (circ->length - 1));
	}

	ret = data_to_read * sizeof(u64);

out:
	mutex_unlock(&icap->channel_lock);
	return ret;

}


struct file_operations icap_cdev_fops = 
{
	.open = icap_cdev_open,
	.release = icap_cdev_close,
	.read = icap_cdev_read,
	.unlocked_ioctl = icap_cdev_ioctl
};

