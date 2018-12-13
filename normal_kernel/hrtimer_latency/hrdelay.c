#include <linux/module.h>   // Needed by all modules (core header for loading into the kernel)
#include <linux/kernel.h>   // Needed for types, macros, functions for the kernel
#include <linux/init.h>     // Needed for the macros (macros used to mark up functions e.g., __init __exit)
#include <linux/delay.h>    // Using this header for the msleep()/udelay()/ndelay() functions
#include <asm/io.h>
#include <linux/kthread.h>  // Using kthreads for the flashing functionality
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/semaphore.h>
#include <linux/ktime.h>		// Needed for ktime_t, functions
#include <linux/hrtimer.h>		// Needed for hrtimer interfaces
#include <linux/time.h>			// timespec
#include <linux/timekeeping.h>	// getnstimeofday
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/slab.h>


static int bin_cnt = 200;
module_param(bin_cnt, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(bin_cnt, "Bin count for the histogram.");



static int delay_shift = 16;
module_param(delay_shift, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(delay_shift, "Bitshift to use on the delay.");

static int period_ms = 100;
module_param(period_ms, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(period_ms, "HRTimer wake period.");




u32 overrun_cnt;
u32 min, max;
u32 underrun_cnt;
int max_getter_delay;
u32 *bins;

void print_result(void)
{
    int i;
    printk("[HRdelay] bin_cnt: %u, delay_shift: %u, period_ms: %u\n",bin_cnt, delay_shift, period_ms);
    printk("[HRdelay] min: %u, max: %u, underrun_cnt: %u, overrun_cnt: %u\n",min,max,underrun_cnt, overrun_cnt);
    printk("[HRdelay] getter delay: %d\n",max_getter_delay);
    for(i=0;i<bin_cnt;i++)
    {
        printk("[HRdelay] [bin%i] %u\n",i,bins[i]);
    }

}



// expected delay: 0-20msec
// resolution: 0.1msec


volatile int goon;
struct hrtimer hr_timer;

ktime_t wake_time;


enum hrtimer_restart hrt_callb_func(struct hrtimer *timer)
{
    struct timespec ts;
    ktime_t now;
    u64 offset;
    s64 latency;

    ktime_t now2;
    s64 dd;

    getnstimeofday(&ts);
    now = ktime_set(ts.tv_sec, ts.tv_nsec);

    getnstimeofday(&ts);
    now2 = ktime_set(ts.tv_sec, ts.tv_nsec);
    dd = ktime_sub(now2,now);
    if(dd > max_getter_delay) max_getter_delay = dd;


    latency = ktime_sub(now,wake_time);
    
    if(latency <= 0)
    {
        // woken too eraly
        underrun_cnt++;
    }
    else
    {
        offset = (u64)latency;

        if(offset > max) max = offset;
        if(offset < min) min = offset;

        offset >>= delay_shift;
        if(offset >= bin_cnt)
        {
            overrun_cnt++;
        }
        else
        {
            bins[offset]++;
        }
    }


    wake_time = ktime_add_ns(wake_time, period_ms*1000000ULL);

    if(!goon)
    {
        return HRTIMER_NORESTART;
    }
    else
    {
        hrtimer_start(&hr_timer, wake_time, HRTIMER_MODE_ABS);
        return HRTIMER_RESTART;
    }
}



static int __init hrdelay_init(void)
{
    struct timespec ts;
    printk(KERN_INFO "hrdelay: greetings to the module world!\n");

    // allocate histogram
    bins = (u32*)kmalloc(bin_cnt * sizeof(u32), GFP_KERNEL);
    if(!bins)
    {
        printk("Cannot allocate memory.\n");
        return -ENOMEM;
    }
    memset(bins,0,bin_cnt * sizeof(u32));



    getnstimeofday(&ts); // Returns the time of day in a timespec
    /* High Resolution Timer settings */
    wake_time = ktime_set(ts.tv_sec, ts.tv_nsec);
    wake_time = ktime_add_ns(wake_time, 100000000LL); //first wake is 100ms away
    hrtimer_init(&hr_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    hr_timer.function = &hrt_callb_func;


    overrun_cnt = 0;
    underrun_cnt = 0;
    min = 100000000;
    max = 0;
    max_getter_delay = 0;
    goon = 1;
    /* Fire the HRTImer */
    hrtimer_start(&hr_timer, wake_time, HRTIMER_MODE_ABS);

    return 0;
}


static void __exit hrdelay_exit(void)
{
	int ret;
    printk(KERN_INFO "hrdelay: goodbye module world!\n ");

    goon = 0;
    msleep(period_ms + 1);

    ret = hrtimer_cancel(&hr_timer);
    if(ret)
    {
        printk(KERN_INFO "The timer was still in use...\n");
    }

    print_result();

    kfree(bins);
    return;
}


module_init(hrdelay_init);
module_exit(hrdelay_exit);

MODULE_DESCRIPTION("Kernel module to measure hrtimer callback latency.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
