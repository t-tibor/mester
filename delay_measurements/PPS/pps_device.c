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
#include <linux/pps_kernel.h> // For pps device
#include <linux/of.h>
#include <linux/of_irq.h>

#include "pps_device.h"


static void __iomem *dmt5_regs;
static void __iomem *cm_regs;


// BB 93 HW int. number mapped to 166 Linux IRQ number
unsigned int irq_number = 167;

static struct pps_device *pps;

static struct pps_source_info pps_info = {
    .name   =   "DMTimer",
    .path   =   "",
    .mode   =   PPS_CAPTUREASSERT | PPS_CANWAIT | PPS_OFFSETASSERT | PPS_ECHOASSERT | PPS_TSFMT_TSPEC,
    .owner  =   THIS_MODULE 
};


/* Interrupt Service Routine (ISR) */
static irqreturn_t tint5_isr(int irq, void *dev_id)
{
    struct pps_event_time ts;

    pps_get_ts(&ts);

    pps_event(pps, &ts, PPS_CAPTUREASSERT, NULL);

    /* Clear the interrupt status */
    iowrite32(DMT_OVERF_IRQ_BIT, dmt5_regs + DMTIMER_IRQSTATUS);

    return IRQ_HANDLED;

} // End of ISR



static int __init pps_device_init(void)
{
    unsigned int regdata;
    struct device_node *dn;

    printk(KERN_INFO "dmtimer_sched: greetings to the module world!\n");

    /* search for the DMTimer5*/
    dn = of_find_node_by_path("/ocp/timer@48046000");
    if(!dn)
    {
        pr_err("Cannot find DMTimer5 device tree node.");
    }

    /* Registering the pps device*/
    pps = pps_register_source(&pps_info, PPS_CAPTUREASSERT);
    if (pps == NULL)
    {
        pr_err("Cannot register PPS device.");
        return -ENOMEM;
    }


    /* Mapping the DMTIMER5 registers */
    if((dmt5_regs = ioremap(DMTIMER5_START_ADDR, DMTIMER5_SIZE)) == NULL)
    {
        printk(KERN_ERR "Mapping the DMTIMER5 registers is failed.\n");
        return -1;
    }
    if((cm_regs = ioremap(CM_REGS_START, CM_REGS_SIZE)) == NULL)
    {
        printk(KERN_ERR "Mapping the CM registers is failed.\n");
        return -1;
    }



    if(dn) irq_number = irq_of_parse_and_map(dn, 0);

    /* Register Interrupt Service Routine (ISR) */
    if(request_irq(irq_number, tint5_isr, IRQF_TIMER, "tint5_isr", NULL))
    {
        printk(KERN_ERR "IRQ %d is reserved.\n", irq_number);
        irq_number = -1;
    }
    else
    {
        printk(KERN_INFO "IRQ %d has been registered.\n", irq_number);
    }

    /* DMTIMER5 settings */

    // enable clock for DMTimer5
    iowrite32(CM_PER_TIMER_ENABLE, cm_regs + CM_PER_TIMER5_CLKCTRL);
    iowrite32(CLK_M_OSC, cm_regs + CLKSEL_TIMER5_CLK);
    
    /* Enable IRQ for DMTIMER5 overflow event*/
    regdata = ioread32(dmt5_regs + DMTIMER_IRQENABLE_SET);
    iowrite32(regdata | DMT_OVERF_IRQ_BIT, dmt5_regs + DMTIMER_IRQENABLE_SET);

    return 0;
}


static void __exit pps_device_exit(void)
{
    unsigned int regdata;

    printk(KERN_INFO "dmtimer_sched: goodbye module world!\n ");


    /* Disable IRQ for DMTIMER5 overflow event*/
    regdata = ioread32(dmt5_regs + DMTIMER_IRQENABLE_CLR);
    iowrite32(regdata | DMT_OVERF_IRQ_BIT, dmt5_regs + DMTIMER_IRQENABLE_CLR);

    /* Unregister Interrupt Service Routine (ISR) */
    if (irq_number >= 0) free_irq(irq_number, NULL); // &devid

    iounmap(dmt5_regs);
    dmt5_regs = NULL;
    return;
}


module_init(pps_device_init);
module_exit(pps_device_exit);

MODULE_DESCRIPTION("PPS device from synchronised DMTIMER5");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
