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

#include "sched_hrt_kthread.h"


static void __iomem *gpio1_regs;
static int hrtimer_should_stop = 0;
static struct task_struct *task; // The pointer to the kernel thread
static struct semaphore sem;

struct hrtimer hr_timer;
ktime_t hrtTime; // 64-bit resolution

void print_readable_time(const struct timespec *ts)
{
    unsigned long timeVal;
    int sec, hr, min, tmp1, tmp2;

    timeVal = ts->tv_sec;
    sec = timeVal % 60;
    tmp1 = timeVal / 60;
    min = tmp1 % 60;
    tmp2 = tmp1 / 60;
    hr = (tmp2 % 24);

	printk(KERN_INFO "Time: %d:%d:%d.%09ld\n",hr,min,sec,ts->tv_nsec);

    return;
}


enum hrtimer_restart hrt_callb_func(struct hrtimer *timer)
{
    unsigned int regdata;
    struct timespec ts;

    getnstimeofday(&ts); // Returns the time of day in a timespec
    //print_readable_time(&ts);

    // Set the output value of GPIO44
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata | GPIO_44, gpio1_regs + GPIO_DATAOUT);

    up(&sem); // Release the semaphore, signal to the kernel thread

    /* Fire the HRTImer */
    hrtTime = ktime_set(ts.tv_sec + 1L, KTHREAD_1_NSEC); // secs, nanosecs

    if(hrtimer_should_stop)
    {
        //wake_up_process(task);
        return HRTIMER_NORESTART;
    }
    else
    {
        hrtimer_start(&hr_timer, hrtTime, HRTIMER_MODE_ABS);
        //wake_up_process(task);
        return HRTIMER_RESTART;
    }
}

int scheduled_kthread(void *arg)
{
    unsigned int regdata;
    struct sched_param param;

    /* Setting the SCHEDULING POLICY and PRIORITY */
    param.sched_priority = 80;
    if(sched_setscheduler(task, SCHED_FIFO, &param) != 0)
    {
        printk(KERN_ERR "sched_dmt_kthread: couldn't set priority of kthread.\n");
    }

    while(!kthread_should_stop())     // Returns true when kthread_stop() is called
    {
        set_current_state(TASK_INTERRUPTIBLE);
        //schedule();
        if(!down_killable(&sem)) // Waiting for the HRT Callback function
        {
            set_current_state(TASK_RUNNING);

            // Clear the output value of GPIO44
            regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
            iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);
        }
     }

    printk(KERN_INFO "sched_dmt_kthread: kernel thread has stopped.\n");
    return 0;
}


static int __init dmtimer_sched_init(void)
{
    unsigned int regdata;
    struct timespec ts;
    printk(KERN_INFO "dmtimer_sched: greetings to the module world!\n");

    /* Mapping the GPIO1 registers */
    if((gpio1_regs = ioremap(GPIO1_START_ADDR, GPIO1_SIZE)) == NULL)
    {
        printk(KERN_ERR "Mapping the GPIO1 registers is failed.\n");
        return -1;
    }

    /* GPIO1 settings */
    // Clear the output value of GPIO44
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);
    //Configure GPIO44 as output
    regdata = ioread32(gpio1_regs + GPIO_OE);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_OE);

    // Semaphore initialization for sync. between HRT Callb. and kernel thread
    sema_init(&sem, 0);

    /* Start a kernel thread */
    task = kthread_run(scheduled_kthread, NULL, "time_scheduled_kthread");
    if(IS_ERR(task))
    {
        printk(KERN_ALERT "sched_dmt_kthread: Failed to create the kernel thread.\n");
        return PTR_ERR(task);
    }

    getnstimeofday(&ts); // Returns the time of day in a timespec
    //print_readable_time(&ts);

    /* High Resolution Timer settings */
    hrtTime = ktime_set(ts.tv_sec + 1L, KTHREAD_1_NSEC); // secs, nanosecs
    hrtimer_init(&hr_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    hr_timer.function = &hrt_callb_func;

    /* Fire the HRTImer */
    hrtimer_start(&hr_timer, hrtTime, HRTIMER_MODE_ABS);

    return 0;
}


static void __exit dmtimer_sched_exit(void)
{
    unsigned int regdata;
	int ret;

    printk(KERN_INFO "dmtimer_sched: goodbye module world!\n ");

    /* Stop the kernel thread */
    kthread_stop(task);

	/* Stop the High Resolution Timer */
    hrtimer_should_stop = 1;
    ret = hrtimer_cancel(&hr_timer);
    if(ret)
    {
        printk(KERN_INFO "The timer was still in use...\n");
    }

    /* GPIO1 settings */
    // Clear the output value of GPIO44
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);
    //Configure GPIO44 as input
    regdata = ioread32(gpio1_regs + GPIO_OE);
    iowrite32(regdata | GPIO_44, gpio1_regs + GPIO_OE);

    iounmap(gpio1_regs);
    gpio1_regs = NULL;

    return;
}


module_init(dmtimer_sched_init);
module_exit(dmtimer_sched_exit);

MODULE_DESCRIPTION("Kernel module to schedule tasks with the synchronized System Clock.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
