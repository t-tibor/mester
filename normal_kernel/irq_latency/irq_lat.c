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

#include "irq_lat.h"


static void __iomem *dmt7_regs;
static void __iomem *gpio1_regs;
// BB 93 HW int. number mapped to 166 Linux IRQ number
unsigned int irq_number = 169;

struct hrtimer hr_timer;
ktime_t callbTime; // 64-bit resolution


/* Interrupt Service Routine (ISR) */
static irqreturn_t tint7_isr(int irq, void *dev_id)
{
    unsigned int regdata = 0;

    /* GPIO1 settings, set the output value of GPIO44 */
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata | GPIO_44, gpio1_regs + GPIO_DATAOUT);

	/* Fire the HRTImer */
    hrtimer_start(&hr_timer, callbTime, HRTIMER_MODE_REL);

    /* Clear the interrupt status */
    iowrite32(DMT_OVERF_IRQ_BIT, dmt7_regs + DMTIMER_IRQSTATUS);

    return IRQ_HANDLED;

} // End of ISR


enum hrtimer_restart hrt_callb_func(struct hrtimer *timer)
{
	unsigned int regdata = 0;

    /* GPIO1 settings, clear the output value of GPIO44 */ 
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);

    return HRTIMER_NORESTART;
}


static int __init irq_lat_init(void)
{
    unsigned int regdata;

    printk(KERN_INFO "dmtimer_sched: greetings to the module world!\n");

    /* Mapping the DMTIMER5 registers */
    if((dmt7_regs = ioremap(DMTIMER7_START_ADDR, DMTIMER7_SIZE)) == NULL)
    {
        printk(KERN_ERR "Mapping the DMTIMER7 registers is failed.\n");
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

    /* High Resolution Timer settings */
    hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    hr_timer.function = &hrt_callb_func;
    callbTime = ktime_set(0, CALLB_1_NSEC); // secs, nanosecs

    /* Register Interrupt Service Routine (ISR) */
    if(request_irq(irq_number, tint7_isr, IRQF_TIMER, "tint7_isr", NULL))
    {
        printk(KERN_ERR "IRQ %d is reserved.\n", irq_number);
        irq_number = -1;
    }
    else
    {
        printk(KERN_INFO "IRQ %d has been registered.\n", irq_number);
    }

    /* DMTIMER5 settings */

    /* Enable IRQ for DMTIMER5 overflow event*/
    regdata = ioread32(dmt7_regs + DMTIMER_IRQENABLE_SET);
    iowrite32(regdata | DMT_OVERF_IRQ_BIT, dmt7_regs + DMTIMER_IRQENABLE_SET);

    return 0;
}


static void __exit irq_lat_exit(void)
{
    unsigned int regdata;
	int ret;

    printk(KERN_INFO "dmtimer_sched: goodbye module world!\n ");

    /* GPIO1 settings */
    // Clear the output value of GPIO44
    regdata = ioread32(gpio1_regs + GPIO_DATAOUT);
    iowrite32(regdata & ~GPIO_44, gpio1_regs + GPIO_DATAOUT);
    //Configure GPIO44 as input
    regdata = ioread32(gpio1_regs + GPIO_OE);
    iowrite32(regdata | GPIO_44, gpio1_regs + GPIO_OE);

	/* Stop the High Resolution Timer */
    ret = hrtimer_cancel(&hr_timer);
    if(ret)
    {
        printk(KERN_INFO "The timer was still in use...\n");
    }

    /* Disable IRQ for DMTIMER5 overflow event*/
    regdata = ioread32(dmt7_regs + DMTIMER_IRQENABLE_CLR);
    iowrite32(regdata | DMT_OVERF_IRQ_BIT, dmt7_regs + DMTIMER_IRQENABLE_CLR);

    /* Unregister Interrupt Service Routine (ISR) */
    if (irq_number >= 0) free_irq(irq_number, NULL); // &devid

    iounmap(dmt7_regs);
    iounmap(gpio1_regs);
    dmt7_regs = NULL;
    gpio1_regs = NULL;

    return;
}


module_init(irq_lat_init);
module_exit(irq_lat_exit);

MODULE_DESCRIPTION("Kernel module to schedule tasks with the DMTimer5.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
