#include "icap_channel.h"

#include <linux/string.h>

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


	// setup misc character device
 	icap->misc.fops = &icap_cdev_fops;
	icap->misc.minor = MISC_DYNAMIC_MINOR;
	icap->misc.name =icap->name;
	if(misc_register(&icap->misc))
	{
		pr_err("Couldn't initialize miscdevice /dev/%s.\n",icap->misc.name);
		goto out2;
	}

	init_waitqueue_head(&icap->rq);

	// init timestamp buffer
	icap->circ_buf.head = icap->circ_buf.tail = 0;
	icap->circ_buf.length = 1<<log_buf_size;
	icap->circ_buf.buffer = (u64*)kzalloc((icap->circ_buf.length) * sizeof(u64) , GFP_KERNEL);
	if(!icap->circ_buf.buffer)
	{
		pr_err("Cannot allocate memory for circular buffer with size: %uB\n",icap->circ_buf.length);
		goto out3;
	}
	icap->buffer_overflow = 0;

	//parameters
	icap->ts_store_state = 1;
	icap->ts_upper_bits = 0;
	icap->ts_mask = 0xFFFFFFFF; // default: 32 bit timestamps are accepted
	spin_lock_init(&icap->param_lock);

	tasklet_init(&icap->bh, icap_channel_bh, (unsigned long)icap);

	return icap;

	out3:
		misc_deregister(&icap->misc);
	out2:
		//kfree(icap->name);
	out1:
		kfree(icap);
	return NULL;
}

void icap_delete_channel(struct icap_channel *icap)
{

	tasklet_disable(icap->bh); // wait for the tasklets to finish
	kfree(icap->circ_buf.buffer);
	misc_deregister(&icap->misc);
	//kfree(icap->name);
	kfree(icap);
}

// parameter setters / getters
void icap_reset_ts(struct icap_channel *icap) 
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->ts_upper_bits ,0);
	spin_unlock_irqrestore(&icap->param, flags);
}

 
void icap_set_ts_mask(struct icap_channel *icap, u64 ts_mask)
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->ts_mask ,ts_mask);
	spin_unlock_irqrestore(&icap->param, flags);
}

void icap_channel_ts_store_enable(struct icap_channel *icap)
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->ts_store_state,1);
	spin_unlock_irqrestore(&icap->param, flags);
}
void icap_channel_ts_store_disable(struct icap_channel *icap)
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->ts_store_state,0);
	spin_unlock_irqrestore(&icap->param, flags);

	// wake up the sleeping processes
	wake_up_interruptible(&icap->rq);
}

int icap_channel_is_ts_store_enabled(struct icap_channel *icap)
{
	int ret;
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	ret = icap->ts_store_state;
	spin_unlock_irqrestore(&icap->param, flags);
	return ret;
}

void icap_clear_buf_ovf(struct icap_channel *icap)
{
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	WRITE_ONCE(icap->buffer_overflow, 0);
	spin_unlock_irqrestore(&icap->param, flags);
}

int icap_get_buf_ovf(struct icap_channel *icap)
{
	int ovf;
	unsigned long flags;
	spin_lock_irqsave(&icap->param_lock,flags);
	ovf = icap->buffer_overflow;
	spin_unlock_irqrestore(&icap->param, flags);
	return ovf;
}

// producer interface
int icap_add_ts(struct icap_channel *icap, u32 ts, int ovf)
{
	u64 t;

	if(!icap->ts_store_state) return -2;

	t = (icap->ts_upper_bits) | (u64)((ts) & icap->ts_mask);

	if((ovf) && (ts < (icap->ts_mask >> 1)))
	{
		t += (u64)(icap->ts_mask)+1;
	}

	head = icap->circ_buf.head;
	tail = READ_ONCE(icap->circ_buf.tail);

	if (CIRC_SPACE(icap->circ_buf.head, icap->circ_buf.tail, icap->circ_buf.length) >= 1) 
	{
		/* insert one item into the buffer */
		icap->circ_buf.buffer[head] = t;

		smp_store_release(&icap->circ_buf.head,
				  (head + 1) & (icap->circ_buf.length - 1));

		tasklet_schedule(&icap->bh);
		return 0;
	}
	else
	{
		WRITE_ONCE(icap->buffer_overflow,icap->buffer_overflow+1);
		return -1;
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
u32 icap_ts_count(struct icap_channel *icap)
{
	u32 head, tail, size;
    int str_len;

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
	try_module_get(THIS_MODULE);
	return 0;
}

static int icap_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}

#define ICAP_IOCTL_MAGIC	'9'

#define ICAP_IOCTL_GET_OVF			_IOR(ICAP_IOCTL_MAGIC,1,int)
#define ICAP_IOCTL_CLEAR_OVF		_IO(ICAP_IOCTL_MAGIC, 2)
#define ICAP_IOCTL_RESET_TS			_IO(ICAP_IOCTL_MAGIC, 3)
#define ICAP_IOCTL_SET_TS_BITNUM	_IOW(ICAP_IOCTL_MAGIC,4,int)
#define ICAP_IOCTL_STORE_EN			_IO(ICAP_IOCTL_MAGIC, 5)
#define ICAP_IOCTL_STORE_DIS		_IO(ICAP_IOCTL_MAGIC, 6)
#define ICAP_IOCTL_FLUSH			_IO(ICAP_IOCTL_MAGIC, 7)

#define ICAP_IOCTL_MAX				7


static long timer_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct icap_channel *icap = container_of(misc_dev, struct icap_channel, misc);
	long ret = 0;
	int err;


	if (_IOC_TYPE(cmd) != ICAP_IOCTL_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > ICAP_IOCTL_MAX) return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	if(mutex_lock_interruptible(icap->channel_lock))
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
			icap_set_ts_mask(((1ULL) << arg)-1);
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
			dev_err(&timer->pdev->dev,"Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}

	mutex_unlock(icap->channel_lock);

	return ret;
}

static ssize_t timer_cdev_read (struct file *pfile, char __user *buff, size_t len, loff_t *ppos)
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
	while(get_ts_count(icap) == 0)
	{
		eof = !icap_channel_is_ts_store_enabled(icap);
		mutex_unlock(&icap->channel_lock);

		if(pfile->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if(eof) return 0;

		if(wait_event_interruptible(icap->rq,((get_ts_count(icap) == 0) && (icap_channel_is_ts_store_enabled(icap))) ))
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
	if (data_to_read >= 1) 
	{
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
				ret = -EFAULT;
				goto out;
			}

			smp_store_release(&circ->tail,
				  (tail + step2_length) & (circ->length - 1));
		}

		ret = data_to_read * sizeof(u64);
	}

out:
	mutex_unlock(&icap->channel_lock);
	return ret;

}


struct file_operations icap_cdev_fops = 
{
	.open = icap_cdev_open,
	.release = icap_cdev_close,
	.read = icap_dcev_read,
	.unlocked_ioctl = icap_cdev_ioctl
};

