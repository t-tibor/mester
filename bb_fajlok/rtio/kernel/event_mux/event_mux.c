#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/of_address.h>

#include <linux/miscdevice.h>


// char dev interface
static struct miscdevice misc;
static struct file_operations dev_fops;


///////////////////////////////////////////////
// Control module registers
///////////////////////////////////////////////
#define CONTROL_MODULE_BASE 	0x44E10000
#define TIMER_EVT_CAPT_OFFSET	0xFD0
#define ECAP_EVT_CAPT_OFFSET	0xFD4
#define ADC_EVT_CAPT_OFFSET		0xFD8

#define CM_SECTION_START		((CONTROL_MODULE_BASE) + (TIMER_EVT_CAPT_OFFSET))
#define CM_SECTION_LENGTH		12			// 12 byte region



volatile void __iomem *timer_evt_capt_reg = NULL;
volatile void __iomem *ecap_evt_capt_reg = NULL;
volatile void __iomem *adc_evt_capt_reg = NULL;

//////////////////////////////////////////////
// ioctl commands
//////////////////////////////////////////////
#define IOCTL_SET_DMTIMER_EVT 	0
#define IOCTL_SET_ECAP_EVT		1
#define IOCTL_SET_ADC_EVT		2



static ssize_t dev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
	return 0;
}


static int dev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}

static long dev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	uint32_t reg_val = (uint32_t) arg;
	uint32_t tmp1, tmp2;

	switch(cmd)
	{
		case IOCTL_SET_DMTIMER_EVT:
			tmp1 = ioread32(timer_evt_capt_reg);
			iowrite32(reg_val, timer_evt_capt_reg);
			tmp2 = ioread32(timer_evt_capt_reg);
			pr_info("Val: 0x%x, before write: 0x%x, after write: 0x%x",reg_val,tmp1,tmp2);
		break;
		case IOCTL_SET_ECAP_EVT:
			iowrite32(reg_val, ecap_evt_capt_reg);
			pr_info("Writing to reg: %p, val:0x%x",ecap_evt_capt_reg,reg_val);
		break;
		case IOCTL_SET_ADC_EVT:
			iowrite32(reg_val, adc_evt_capt_reg);
			pr_info("Writing to reg: %p, val:0x%x",adc_evt_capt_reg,reg_val);
		break;
		default:
		pr_err("Unknown ioctl command: %d\n",cmd);
	}
	return 0;

}


static struct file_operations dev_fops =
{
		.owner = THIS_MODULE,
		.open = dev_open,
		.release = dev_close,
		.unlocked_ioctl = dev_ioctl
};


static int __init event_mux_init(void)
{

	// alloc memory region in the Control module memory space
	if(request_mem_region(CM_SECTION_START, CM_SECTION_LENGTH, "Control module dmtimer, ecap, adc event mux") == NULL)
	{
		pr_err("Cannot request the memory region.");
		goto err;
	}

	timer_evt_capt_reg = ioremap(CM_SECTION_START, CM_SECTION_LENGTH);
	if(!timer_evt_capt_reg)
	{
		pr_err("Cannot remap the memroy region.");
		goto err1;
	}
	ecap_evt_capt_reg = timer_evt_capt_reg + 4;
	adc_evt_capt_reg = timer_evt_capt_reg + 8;
	

	pr_info("Event mux memory region remapped.");

	misc.fops = &dev_fops;
	misc.minor = MISC_DYNAMIC_MINOR;
	misc.name = "event_mux";
	if(misc_register(&misc))
	{
		pr_err("Couldn't initialize miscdevice /dev/event_mux.\n");
		goto err2;
	}
	pr_info("Misc device initialized: /dev/event_mux.\n");



	return 0;
err2:
	iounmap(timer_evt_capt_reg);
err1:
	release_mem_region(CM_SECTION_START, CM_SECTION_LENGTH);
err:
	return -ENODEV;
}

static void __exit event_mux_exit(void)
{
	misc_deregister(&misc);
	if(timer_evt_capt_reg)
	{
		iounmap(timer_evt_capt_reg);
		release_mem_region(CM_SECTION_START, CM_SECTION_LENGTH);
	}
}

module_init(event_mux_init);
module_exit(event_mux_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBon timers.");