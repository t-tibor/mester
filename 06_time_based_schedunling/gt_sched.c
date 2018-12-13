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
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/sched/signal.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>


#define DEBUG

// global variables
#define MISC_NAME 		"sched_gt"
static struct miscdevice misc;

#define WDG_PRIO 			82
#define GOOD_TASK_PRIO	 	81
#define BAD_TASK_PRIO 		80
static struct task_struct *wdg_thread;



static int system_period_ns = 20000000UL;
module_param(system_period_ns, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(system_period_ns, "System period [ns]");


#define TASK_STATE_NORMAL 	0
#define TASK_STATE_WAITING 	1
#define TASK_STATE_RUNNING 	2
#define TASK_STATE_OVERRUN 	3

/*
struct time_slice
{
	u32 from;
	u32 to;
	struct task_struct *user;
	struct list_head list;
};

struct sched_gt_entity
{
	int frequency;
	u32 offset_ns;
	u32 length_ns;
	int task_state;
	struct mutex lock;
	struct hrtimer wdg_timer;
	struct time_slice *slice;
	struct timespec64 last_wake;
};
*/

enum hrtimer_restart wdg_timer_callb_func(struct hrtimer *timer);
static int add_wdg_event(struct task_struct *task);



// bandwidth management
LIST_HEAD(slice_list);
DEFINE_MUTEX(slice_list_lock);


void show_timeslices(void)
{
	struct time_slice *tmp;
	u32 from;
	u32 to;
	struct task_struct *task;

	mutex_lock(&slice_list_lock);

	list_for_each_entry(tmp, &slice_list, list)
	{
		from = tmp->from;
		to = tmp->to;
		task = tmp->user;
		pr_info("[GT SLICES] Used slice by %u - from:%uus, to:%uus\n",task->pid, from/1000,to/1000);
	}

	mutex_unlock(&slice_list_lock);
}



int sleep_until(struct timespec64 *ts)
{
	struct hrtimer_sleeper t;
	enum hrtimer_mode mode;
	u64 slack;
	int ret;
	ktime_t wake_time;

	slack = 50000;				// 50 usec
	mode = HRTIMER_MODE_ABS;
	wake_time = timespec64_to_ns(ts);

	hrtimer_init_on_stack(&t.timer, CLOCK_REALTIME, mode);
	hrtimer_set_expires_range_ns(&t.timer, wake_time, slack);

	// do_nanosleep
	hrtimer_init_sleeper(&t, current);
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		hrtimer_start_expires(&t.timer, mode);

		if (likely(t.task))
			freezable_schedule();

		hrtimer_cancel(&t.timer);
		mode = HRTIMER_MODE_ABS;

	} while (t.task && !signal_pending(current));

	__set_current_state(TASK_RUNNING);
	
	if(t.task != NULL)
	{
		ret = -1;
	}
	else
	{
		ret = 0;
	}

	// do nanosleep end

	destroy_hrtimer_on_stack(&t.timer);

	return ret;
}

struct sched_gt_entity* task2sched_gt_entity(struct task_struct *task)
{
	return &task->gt;
}



int _is_timeslice_free(u32 from, u32 to)
{
	struct time_slice *tmp;
	int ok;

	if(to <= from) return 0;
	if(to > system_period_ns) return 0;

	ok = 1;
	list_for_each_entry(tmp, &slice_list, list)
	{
		if(tmp->to <= from) continue;

		if(tmp->from < to)
		{
			ok = 0;
			break;
		}
		else
		{
			break;
		}
	}

	return ok;
}

int _add_time_slice(struct time_slice *s)
{
	struct time_slice *tmp;
	struct list_head *next_elem;
	u32 from,to;

	from = s->from;
	to = s->to;

	next_elem = &slice_list;
	list_for_each_entry(tmp, &slice_list, list)
	{
		if(tmp->from > from)
		{
			next_elem = &tmp->list;
			break;
		}
	}

	list_add_tail(&s->list,next_elem);
	return 0;
}

int _remove_time_slice(struct time_slice *slice)
{
	if(!slice) return 0;
	list_del(&slice->list);
	return 0;
}

int release_bandwidth(struct sched_gt_entity *gt)
{
	int i;

	if(gt->slices)
	{
		mutex_lock(&slice_list_lock);
		for(i = 0; i < gt->frequency; i++)
		{
			_remove_time_slice(&gt->slices[i]);
		}
		mutex_unlock(&slice_list_lock);
		kfree(gt->slices);
		gt->slices = NULL;
	}
	return 0;
}

int alloc_bandwidth(struct task_struct *task)
{
	struct time_slice *slice;
	u32 frequency;
	u32 offset;
	u32 length;
	u32 task_period_ns;
	u32 from,to;
	unsigned i, ok;

	struct sched_gt_entity *gt = task2sched_gt_entity(task);
	
	frequency = gt->frequency;

	if(frequency == 0)
	{
		pr_err("Invalid 0 frequency.\n");
		return -EINVAL;
	}

	task_period_ns = system_period_ns / frequency;

	offset = gt->offset_ns;
	length = gt->length_ns;

	if(length == 0)
	{
		pr_err("Invalid 0 length.\n");
		return -EINVAL;
	}

	if(gt->slices)
	{
		pr_err("Task has already allocated timeslices.\n");
		return -EINVAL;
	}

	mutex_lock(&slice_list_lock);
	ok = 1;
	for(i=0;i<frequency && ok;i++)
	{
		from = offset + i*task_period_ns;
		to = offset + i*task_period_ns + length;
		if(!_is_timeslice_free(from, to))
		{
			ok = 0;
			pr_info("Cannot allocate timeslice from: %u, to: %u.\n", from, to);
		}
	}

	if(ok)
	{
		gt->slices = (struct time_slice*)kmalloc(sizeof(struct time_slice) * frequency,GFP_KERNEL);
		if(!gt->slices)
		{
			pr_err("Cannot allocate memory for slice array.\n");
			return -1;
		}

		// alloc bandwidth
		for(i=0;i<frequency;i++)
		{
			slice = &gt->slices[i];
			slice->from = offset + i*task_period_ns;
			slice->to = offset + i*task_period_ns + length;
			slice->user = task;
			INIT_LIST_HEAD(&slice->list);
			_add_time_slice(slice);
			pr_info("Bandwidth allocated: from: %u, to:%u.\n",slice->from, slice->to);
		}
		
	}

	mutex_unlock(&slice_list_lock);

	show_timeslices();

	return (ok==1) ? 0 : -EINVAL;
}




int _enter_high_prio_mode(struct task_struct *task)
{
	struct sched_param param;
	param.sched_priority = GOOD_TASK_PRIO;
    if(sched_setscheduler(task, SCHED_FIFO, &param) != 0)
    {
        pr_err("Cannot setup normal thread priority.\n");
        return -1;
    }
    return 0;
}

int _enter_bad_task_mode(struct task_struct *task)
{
	struct sched_gt_entity *gt;
	struct sched_param param;
	// downgrade task priority
	gt = task2sched_gt_entity(task);
 	param.sched_priority = BAD_TASK_PRIO;
    if(sched_setscheduler(task, SCHED_RR, &param) != 0)
    {
        return -1;
    }	
    gt->task_state = TASK_STATE_OVERRUN;
    return 0;

}

int _enter_normal_mode(struct task_struct *task)
{
	struct sched_gt_entity *gt;
	struct sched_param param;
	gt = task2sched_gt_entity(task);

	param.sched_priority = 0;
	if(sched_setscheduler(task, SCHED_NORMAL, &param) != 0)
    {
        return -1;
    }	
    gt->task_state = TASK_STATE_NORMAL;
    return 0;
}


enum hrtimer_restart wdg_timer_callb_func(struct hrtimer *timer)
{
	struct sched_gt_entity *gt = container_of(timer, struct sched_gt_entity, wdg_timer);
	struct task_struct *task = container_of(gt,struct task_struct, gt);

	// mark task as bad, and request its depriorisation
	add_wdg_event(task);
	return HRTIMER_NORESTART;
}



int task_enter_sched_gt(struct task_struct *task)
{
	int ret;
	struct sched_gt_entity *gt;

	ret = alloc_bandwidth(task);

	if(!ret)
	{
		gt = task2sched_gt_entity(task);
		mutex_lock(&gt->lock);
		ret = _enter_bad_task_mode(task);
		mutex_unlock(&gt->lock);
	}
    return ret;
}

int task_leave_sched_gt(struct task_struct *task)
{
	struct sched_gt_entity *gt;
	int ret;

	gt = task2sched_gt_entity(task);

	release_bandwidth(gt);

	mutex_lock(&gt->lock);
	ret = _enter_normal_mode(task);
	mutex_unlock(&gt->lock);

	return ret;
}




void sched_gt_entity_init(struct sched_gt_entity *sg)
{
	if(!sg) return;
	sg->frequency = 0;
	sg->period_ns = 0;
	sg->offset_ns = 0;
	sg->length_ns = 0;
	sg->task_state = TASK_STATE_NORMAL;
	sg->slices = NULL;
	sg->last_wake.tv_sec = 0;
	sg->last_wake.tv_nsec = 0;
	mutex_init(&sg->lock);
	hrtimer_init(&sg->wdg_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	sg->wdg_timer.function = wdg_timer_callb_func;
}

void sched_gt_entity_deinit(struct sched_gt_entity *sg)
{
	hrtimer_cancel(&sg->wdg_timer);

	sg->frequency = 0;
	sg->period_ns = 0;
	sg->offset_ns = 0;
	sg->length_ns = 0;
	sg->task_state = TASK_STATE_NORMAL;
	sg->slices = NULL;
	sg->last_wake.tv_sec = 0;
	sg->last_wake.tv_nsec = 0;
	mutex_destroy(&sg->lock);
}




struct timespec64 calc_next_wake_time(struct sched_gt_entity *gt)
{ 
	struct timespec64 now, delta, wake_time;
	ktime_t t;
	u64 t2,t3;
	u32 mod;
	u32 task_period_ns;

	getnstimeofday64(&now);
	timespec64_add_ns(&now,200000ULL);
	delta = timespec64_sub(now,gt->last_wake);

	task_period_ns = gt->period_ns;

	if( timespec64_to_ns(&delta) > 10*task_period_ns)
	{
		//recalculate
		t = timespec64_to_ns(&now);
		t2 = (u64)t;
		mod = do_div(t2,system_period_ns);
		// t - start of the current system period
		t3 = mod;
		mod = do_div(t3, task_period_ns);

		t -= mod;
		t += task_period_ns;
		t += (u64)gt->offset_ns;
		wake_time = ns_to_timespec64(t);
	}
	else
	{
		wake_time = gt->last_wake;
		while( timespec64_compare(&wake_time, &now) < 0)
		{
			timespec64_add_ns(&wake_time,task_period_ns);
		}
	}
	
	return wake_time;
}

ktime_t calc_wdg_wake_time(struct sched_gt_entity *gt, struct timespec64 ts)
{
	ktime_t t;
	t = ktime_set(ts.tv_sec, ts.tv_nsec); // secs, nanosecs
	t += (s64)gt->length_ns;
	return t;
}

int task_sched_gt_wait_for_next(void)
{
	struct sched_gt_entity *gt;
	struct timespec64 ts;
	ktime_t wdg_time;

	gt = task2sched_gt_entity(current);
	
	// go to waiting state
	mutex_lock(&gt->lock);

	if(gt->task_state == TASK_STATE_OVERRUN)
	{
		_enter_high_prio_mode(current);
	}
	gt->task_state = TASK_STATE_WAITING;
	mutex_unlock(&gt->lock);
	hrtimer_cancel(&gt->wdg_timer);

	
	// wait until the next start
	ts = calc_next_wake_time(gt);
#ifdef DEBUG
	//pr_info("Going to sleep until: %llds,%ldnsec.\n",ts.tv_sec, ts.tv_nsec);
#endif
	sleep_until(&ts);
	gt->last_wake = ts;
#ifdef DEBUG
	//pr_info("Task woken.\n");
#endif

	// change state and start watchdog timer
	wdg_time = calc_wdg_wake_time(gt,ts);
#ifdef DEBUG
	//pr_info("Watchdog time: %lld.\n",wdg_time);
#endif
	mutex_lock(&gt->lock);
	gt->task_state = TASK_STATE_RUNNING;
	hrtimer_start(&gt->wdg_timer, wdg_time, HRTIMER_MODE_ABS);
	mutex_unlock(&gt->lock);
	return 0;
}





/*------------------------- WATCHDOG KTHREAD ----------------------*/
DEFINE_SPINLOCK(wdg_buffer_lock);
DECLARE_COMPLETION(wdg_event_in);

static struct task_struct *wdg_buffer[16];
volatile int ridx, widx;

volatile int wdg_exit; 

int wdg_thread_init(void)
{
	memset((void*)wdg_buffer,0,sizeof(wdg_buffer));
	widx = ridx = 0;
	wdg_exit = 0;
	return 0;
}


static int add_wdg_event(struct task_struct *task)
{
	int tmp, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&wdg_buffer_lock,flags);
	
	tmp = (widx+1) % (sizeof(wdg_buffer)/sizeof(wdg_buffer[0]));
	if(tmp == ridx)
	{
		//full
		ret = -1;
	}
	else
	{
		wdg_buffer[widx] = task;
		widx = tmp;
		ret = 0;	
	}

	spin_unlock_irqrestore(&wdg_buffer_lock,flags);

	if(!ret)
	{
		complete(&wdg_event_in);
	}

	return ret;
}

static struct task_struct* get_wdg_event(void)
{
	struct task_struct* task;
	unsigned long flags;

	spin_lock_irqsave(&wdg_buffer_lock,flags);
	
	if(ridx == widx)
	{
		task = NULL;
	}
	else
	{
		task = wdg_buffer[ridx++];
		ridx = ridx % (sizeof(wdg_buffer)/sizeof(wdg_buffer[0]));	
	}
	
	spin_unlock_irqrestore(&wdg_buffer_lock,flags);

	return task;
}


int wdg_thread_fn(void *arg)
{
	struct sched_param param;
	struct task_struct *bad_task;
	struct sched_gt_entity *gt;
	int success;


    /* Setting the SCHEDULING POLICY and PRIORITY */
    param.sched_priority = WDG_PRIO;
    if(sched_setscheduler(wdg_thread, SCHED_FIFO, &param) != 0)
    {
        pr_err("Cannot setup watchdog thread priority.\n");
    }

	 while(!kthread_should_stop() && !wdg_exit)
	 {
	 	wait_for_completion_interruptible(&wdg_event_in);
	 	bad_task = get_wdg_event();
	 	if(!bad_task) continue;

	 	#ifdef DEBUG
	 	//pr_info("Downgrading thread.\n");
	 	#endif

	 	gt = task2sched_gt_entity(bad_task);
	 	success = 1;
	 	mutex_lock(&gt->lock);
	 	if(gt->task_state == TASK_STATE_RUNNING)
	 	{
	 		success = !_enter_bad_task_mode(bad_task);
	 	}
	 	mutex_unlock(&gt->lock);

	 	if(!success)
			pr_err("Cannot downgrade bad task priority.\n");
	 }

	 #ifdef DEBUG
	 pr_info("Watchdog thread exiting...\n");
	 #endif

	 return 0;
}



/*----------------------- MISC DEVICE INTERFACE ------------------*/


static ssize_t sched_gt_open(struct inode *inode, struct file *pfile)
{
	struct sched_gt_entity *gt;

	try_module_get(THIS_MODULE);
	gt = task2sched_gt_entity(current);
	sched_gt_entity_init(gt);
	return 0;
}

static int sched_gt_close (struct inode *inode, struct file *pfile)
{
	struct sched_gt_entity *gt;
	gt = task2sched_gt_entity(current);
	task_leave_sched_gt(current);
	sched_gt_entity_deinit(gt);
	module_put(THIS_MODULE);
	return 0;
}

#define SCHED_GT_IOCTL_MAGIC	'@'
#define IOCTL_SET_OFFSET			_IO(SCHED_GT_IOCTL_MAGIC,1)
#define IOCTL_SET_LENGTH 			_IO(SCHED_GT_IOCTL_MAGIC,2)
#define IOCTL_SET_FREQUENCY 		_IO(SCHED_GT_IOCTL_MAGIC,3)
#define IOCTL_ENTER_SCHED_GT 		_IO(SCHED_GT_IOCTL_MAGIC,4)
#define IOCTL_LEAVE_SCHED_GT 		_IO(SCHED_GT_IOCTL_MAGIC,5)
#define IOCTL_WAIT_NEXT 			_IO(SCHED_GT_IOCTL_MAGIC,6)
#define TIMER_IOCTL_MAX				6	
static long sched_gt_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct task_struct *task;
	struct sched_gt_entity *gt;

	if (_IOC_TYPE(cmd) != SCHED_GT_IOCTL_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > TIMER_IOCTL_MAX) return -ENOTTY;
	task = current;
	gt = task2sched_gt_entity(task);

	switch(cmd)
	{
		case IOCTL_SET_OFFSET:
			if(gt->task_state == TASK_STATE_NORMAL)
			{
				gt->offset_ns = arg*1000000UL;	
				ret = 0;
			}
			else
			{
				ret = -EINVAL;
			}

			break;
		case IOCTL_SET_LENGTH:
			if(gt->task_state == TASK_STATE_NORMAL)
			{
				gt->length_ns = arg*1000000UL;
				ret = 0;
			}
			else
			{
				ret = -EINVAL;
			}
			break;
		case IOCTL_SET_FREQUENCY:
			if(gt->task_state == TASK_STATE_NORMAL)
			{
				gt->frequency = arg;
				gt->period_ns = (u32)system_period_ns / (u32)arg;
				ret = 0;
			}
			else
			{
				ret = -EINVAL;
			}
			break;
		case IOCTL_ENTER_SCHED_GT:
			if(gt->task_state == TASK_STATE_NORMAL)
			{
				ret = task_enter_sched_gt(task);
			}
			else
			{
				ret = -EINVAL;
			}
			break;
		case IOCTL_LEAVE_SCHED_GT:
			if(gt->task_state != TASK_STATE_NORMAL)
			{
				ret = task_leave_sched_gt(task);
				if(ret)
				{
					pr_err("Cannot leave sched gt mode \n.");
					ret = -EINVAL;
				}
			}
			else
			{
				ret = -EINVAL;
			}
			break;
		case IOCTL_WAIT_NEXT:
			if(gt->task_state != TASK_STATE_NORMAL)
			{
				task_sched_gt_wait_for_next();
				ret = 0;
			}
			else
			{
				ret = -EINVAL;
			}
			break;
		default:
			pr_err("Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}
	return ret;
}

static struct file_operations sched_gt_fops = 
{
	.open = sched_gt_open,
	.release = sched_gt_close,
	.unlocked_ioctl = sched_gt_ioctl
};




int __init sched_gt_init(void)
{
	int ret;
	// initialize misc device
	misc.fops 	= &sched_gt_fops;
	misc.minor 	= MISC_DYNAMIC_MINOR;
	misc.name 	= MISC_NAME;
	misc.mode 	= S_IRUGO | S_IWUGO;
	if(misc_register(&misc))
	{
		pr_err("Couldn't initialize miscdevice /dev/%s.\n",misc.name);
		ret =  -ENODEV;
		goto err0;
	}


	wdg_thread_init();
	wdg_thread = kthread_run(wdg_thread_fn,NULL,"sched_gt_watchdog");
	if(IS_ERR(wdg_thread))
	{
		pr_err("Watchdog thread failed to start.\n");
		ret = PTR_ERR(wdg_thread);
		goto err1;
	}

	return 0;


	err1:
	misc_deregister(&misc);
	err0:
	return ret;
}

void __exit sched_gt_exit(void)
{
	pr_info("Stopping watchdog kthread.\n");
	wdg_exit = 1;
	complete(&wdg_event_in);
	kthread_stop(wdg_thread);
	pr_info("Watchdig kthread stopped.\n");

	misc_deregister(&misc);
}





module_init(sched_gt_init);
module_exit(sched_gt_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("Statc time based scheduling helper.");