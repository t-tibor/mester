#include <linux/module.h>   // Needed by all modules (core header for loading into the kernel)
#include <linux/kernel.h>   // Needed for types, macros, functions for the kernel
#include <linux/init.h>     // Needed for the macros (macros used to mark up functions e.g., __init __exit)
#include <linux/delay.h>    // Using this header for the msleep()/udelay()/ndelay() functions
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/ktime.h>    // Needed for ktime_t, functions
#include <linux/hrtimer.h>  // Needed for hrtimer interfaces
#include <linux/kthread.h>  // Using kthreads for the flashing functionality


#include "hrtimer_delay.h"


static void __iomem *dmt5_regs;
static void __iomem *gpio1_regs;

struct hrtimer hr_timer;
static int hrtimer_should_stop = 0;
static struct task_struct *task; // The pointer to the kernel thread
static struct semaphore sem;
struct timespec64 currTime;
struct timespec64 currTime2;


void print_readable_time(const struct timespec64 *ts, const struct timespec64 *ts2)
{
    printk("[HR]Sec1:%llu, Nsec1:%lu , Sec2:%llu, Nsec2:%lu\n",ts->tv_sec,ts->tv_nsec,ts2->tv_sec,ts2->tv_nsec);

    return;
}


int scheduled_kthread(void *arg)
{
    while(!kthread_should_stop())     // Returns true when kthread_stop() is called
    {
        set_current_state(TASK_INTERRUPTIBLE);
        //schedule();
        if(!down_killable(&sem)) // Waiting for the HRT Callback function
        {
            set_current_state(TASK_RUNNING);
            print_readable_time(&currTime, &currTime2);
            
        }
     }

    printk(KERN_INFO "sched_dmt_kthread: kernel thread has stopped.\n");
    return 0;
}



enum hrtimer_restart hrt_callb_func(struct hrtimer *timer)
{
	unsigned int regdata = 0;

    /* GPIO1 settings, clear the output value of GPIO44 */ 
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata ^ GPIO_44, gpio1_regs + GPIO_DATAOUT);


    currTime = ktime_to_timespec64(ktime_get_real());
    currTime2 = ktime_to_timespec64(ktime_get_real());
    up(&sem); // Release the semaphore, signal to the kernel thread

    if(hrtimer_should_stop)
    {
        //wake_up_process(task);
        return HRTIMER_NORESTART;
    }
    else
    {
    hrtimer_forward(timer,timer->base->get_time(),ktime_set(1,0));

    return HRTIMER_RESTART;
    }
}


static int __init hrtimer_delay_init(void)
{
    unsigned int regdata;
    struct timespec64 startTime;

    printk(KERN_INFO "dmtimer_sched: greetings to the module world!\n");

    /* Mapping the DMTIMER5 registers */
    if((dmt5_regs = ioremap(DMTIMER5_START_ADDR, DMTIMER5_SIZE)) == NULL)
    {
        printk(KERN_ERR "Mapping the DMTIMER5 registers is failed.\n");
        return -1;
    }
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


    sema_init(&sem, 0);


    /* High Resolution Timer settings */
    hrtimer_init(&hr_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    hr_timer.function = &hrt_callb_func;
    currTime = ktime_to_timespec64(ktime_get_real());
    startTime.tv_sec = currTime.tv_sec+2;
    startTime.tv_nsec = 100000UL;

    hrtimer_start(&hr_timer, timespec64_to_ktime(startTime), HRTIMER_MODE_ABS);

       /* Start a kernel thread */
    task = kthread_run(scheduled_kthread, NULL, "time_scheduled_kthread");
    if(IS_ERR(task))
    {
        printk(KERN_ALERT "sched_dmt_kthread: Failed to create the kernel thread.\n");
        return PTR_ERR(task);
    }

    return 0;
}


static void __exit hrtimer_delay_exit(void)
{
    unsigned int regdata;
	int ret;

    printk(KERN_INFO "dmtimer_sched: goodbye module world!\n ");

    kthread_stop(task);

    /* GPIO1 settings */
    // Clear the output value of GPIO44
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);
    //Configure GPIO44 as input
    regdata = ioread32(gpio1_regs + GPIO_OE);
    iowrite32(regdata | GPIO_44, gpio1_regs + GPIO_OE);

	/* Stop the High Resolution Timer */
    hrtimer_should_stop = 1;
    ret = hrtimer_cancel(&hr_timer);
    if(ret)
    {
        printk(KERN_INFO "The timer was still in use...\n");
    }


    iounmap(dmt5_regs);
    iounmap(gpio1_regs);
    dmt5_regs = NULL;
    gpio1_regs = NULL;

    return;
}


module_init(hrtimer_delay_init);
module_exit(hrtimer_delay_exit);

MODULE_DESCRIPTION("Kernel module to test hrtimer latency by GPIO toggle at whole seconds.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
