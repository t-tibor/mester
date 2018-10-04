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

#include <linux/pm_runtime.h>

#include "../event_mux/event_mux.h"
#include "../icap_channel/icap_channel.h"


// DMTimer register offsets
#define TIMER_IRQSTATUS_RAW_OFFSET	0x24
#define TIMER_IRQSTATUS_OFFSET 		0x28
#define 	TCAR_IT_FLAG			(1<<2)
#define 	OVF_IT_FLAG				(1<<1)
#define 	MAT_IT_FLAG				(1<<0)
#define TIMER_IRQENABLE_SET_OFFSET	0x2C
#define TIMER_IRQENABLE_CLR_OFFSET	0x30

#define TIMER_TCAR1_OFFSET			0x50
#define TIMER_TCAR2_OFFSET			0x58

/*
TODO:
	check timer clock source

*/

// type declarations

struct DMTimer_priv
{
	char *name;
	u32 idx;
	int irq;
	struct clk *fclk;

	void __iomem *io_base;				// kernel virtual address
	unsigned long regspace_phys_base;	// physical base address
	u32 regspace_size;				// memory region size

	struct miscdevice misc;
	struct platform_device *pdev;

	uint32_t ovf_counter;
	wait_queue_head_t wq;	// wait queue for ovf events

	char * icap_channel_name;
	struct icap_channel *icap;
};

int irq_enable_ovf_wake = 0;
module_param(irq_enable_ovf_wake, int, S_IRUGO | S_IWUGO);
MODULE_PARM_DESC(irq_enable_ovf_wake, "Enable waking from hard irq context at timer overflow events");


// <------------------- CLOCK SOURCE SETTINGS ------------------->
#define CLK_DISABLE		0x00
#define CLK_ENABLE		0x01

static int timer_set_clk_state(struct DMTimer_priv *timer, u32 arg)
{
	int ret = 0;

	if(unlikely(arg > 1) || unlikely(!timer) || IS_ERR(timer->fclk))
		return -EINVAL;

	switch(arg)
	{
		case CLK_DISABLE:
			clk_disable(timer->fclk);
			break;
		case CLK_ENABLE:
			ret = clk_enable(timer->fclk);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

#define CLOCK_SOURCE_SYSCLK		0x01
#define	CLOCK_SOURCE_TCLKIN		0x02
static int timer_set_clksource(struct DMTimer_priv *timer, u32 source)
{
	const char *parent_name;
	struct clk *parent;
	int parent_num  = 0;
	int ret;

	if (unlikely(!timer) || IS_ERR(timer->fclk))
			return -EINVAL;

	switch(source)
	{
		case CLOCK_SOURCE_SYSCLK:
			parent_name = "sys_clkin_ck";
			break;
		case CLOCK_SOURCE_TCLKIN:
			parent_name = "tclkin_ck";
			break;
		default:
			dev_err(&timer->pdev->dev,"%s:%d:Invalid clock source to select: %d.\n",__FUNCTION__,__LINE__,source);
			return -EINVAL;
			break;
	}


	if ((parent_num = clk_hw_get_num_parents(__clk_get_hw(timer->fclk))) < 2)
	{
		dev_err(&timer->pdev->dev,"[%s:%d] Selected timer has only %d parents (expected at least 2).\n",__FUNCTION__,__LINE__,parent_num);
		return -EINVAL;
	}


	parent = clk_get(&timer->pdev->dev, parent_name);
	if (IS_ERR(parent)) {
		dev_err(&timer->pdev->dev,"[%s:%d] %s not found\n", __FUNCTION__,__LINE__, parent_name);
		return -EINVAL;
	}

	ret = clk_set_parent(timer->fclk, parent);
	if (ret < 0)
		dev_err(&timer->pdev->dev,"[%s:%d] Failed to set %s as parent\n", __FUNCTION__,__LINE__,parent_name);

	clk_put(parent);

	return ret;
}

// <------------------------  Timer character device operations ------------------------>

static ssize_t timer_cdev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int timer_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}


static const struct vm_operations_struct timer_physical_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int timer_cdev_mmap(struct file *filep, struct vm_area_struct *vma)
{
	int ret;
	struct miscdevice *misc_dev = (struct miscdevice*) (filep->private_data);
	struct DMTimer_priv *timer= container_of(misc_dev, struct DMTimer_priv, misc);

	// the offset is not used
	unsigned long physical = timer->regspace_phys_base;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = timer->regspace_size;

	if (vsize > psize)
	{
		dev_warn(&timer->pdev->dev,"[%s:%d] Requested memoryregion is too big. (0x%lx) Size of the remapped memory region: 0x%lx\n",__FUNCTION__,__LINE__,vsize,psize);
	}


	vma->vm_ops = &timer_physical_vm_ops;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, psize, vma->vm_page_prot);
	if(ret)
	{
		dev_warn(&timer->pdev->dev,"[%s:%d] Cannot remap memory region. ret:%d, phys:0x%lx\n",__FUNCTION__,__LINE__,ret,physical);
		return -EAGAIN;
	}
	return 0;
}

int wait_for_next_ovf(struct DMTimer_priv *timer)
{
	uint32_t ovf_start = READ_ONCE(timer->ovf_counter);

	if(wait_event_interruptible(timer->wq, (READ_ONCE(timer->ovf_counter) != ovf_start)))
		return -ERESTARTSYS;

	return 0;
}


#define TIMER_IOCTL_MAGIC	'-'

#define IOCTL_SET_CLOCK_STATE		_IO(TIMER_IOCTL_MAGIC,1)
#define IOCTL_SET_CLOCK_SOURCE 		_IO(TIMER_IOCTL_MAGIC,2)
#define IOCTL_SET_ICAP_SOURCE 		_IO(TIMER_IOCTL_MAGIC,3)
#define IOCTL_GET_CLK_FREQ			_IO(TIMER_IOCTL_MAGIC,4)
#define IOCTL_WAIT_OVF				_IO(TIMER_IOCTL_MAGIC,5)

#define TIMER_IOCTL_MAX				5


static long timer_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct DMTimer_priv *timer = container_of(misc_dev, struct DMTimer_priv, misc);
	int ret = 0;

	if (_IOC_TYPE(cmd) != TIMER_IOCTL_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > TIMER_IOCTL_MAX) return -ENOTTY;

	switch(cmd)
	{
		case IOCTL_SET_CLOCK_STATE:
			ret = timer_set_clk_state(timer,arg);
			break;
		case IOCTL_SET_CLOCK_SOURCE:
			ret = timer_set_clksource(timer,arg);
			break;
			
		case IOCTL_SET_ICAP_SOURCE:
			ret = event_mux_set_dmtimer_event(timer->idx,arg);
			break;
		case IOCTL_GET_CLK_FREQ:
			ret = clk_get_rate(timer->fclk);
			break;
		case IOCTL_WAIT_OVF:
			ret = wait_for_next_ovf(timer);
			break;
		default:
			dev_err(&timer->pdev->dev,"Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}
	return ret;
}


static struct file_operations timer_cdev_fops = 
{
	.open = timer_cdev_open,
	.release = timer_cdev_close,
	.mmap = timer_cdev_mmap,
	.unlocked_ioctl = timer_cdev_ioctl
};



static irqreturn_t timer_irq_handler(int irq, void *data)
{
	struct DMTimer_priv *timer= (struct DMTimer_priv*)data;
	// clear interrupt line and save the input capture flag
	u32 irq_status;
	u32 ts;

	irq_status = readl(timer->io_base + TIMER_IRQSTATUS_OFFSET);


	if(irq_status & TCAR_IT_FLAG)
	{
		ts = readl(timer->io_base + TIMER_TCAR1_OFFSET);

		icap_add_ts(timer->icap,ts,irq_status & OVF_IT_FLAG);
	}

	// clear interrupt flag
	writel(irq_status,timer->io_base + TIMER_IRQSTATUS_OFFSET);


	if(irq_status & OVF_IT_FLAG)
	{
		WRITE_ONCE(timer->ovf_counter, timer->ovf_counter+1);
		icap_signal_timer_ovf(timer->icap);
	}

	if(timer->irq_enable_ovf_wake == 1)
		wake_up_interruptible(&timer->wq);

	return IRQ_HANDLED;
}


static int timer_probe(struct platform_device *pdev)
{
	const char *timer_name;
	int timer_idx;
	struct resource *mem, *irq;
	struct DMTimer_priv *timer;
	int ret;
	char *irq_name;
	uint8_t use_icap_channel;

	// read up timer index
	of_property_read_string_index(pdev->dev.of_node, "ti,hwmods", 0, &timer_name);
	sscanf(timer_name, "timer%d", &timer_idx);
	if(timer_idx < 4 || timer_idx > 7) 
	{
		pr_err("Invalid timer index found in the device tree: %d\n",timer_idx);
		return -EINVAL;
	}

	// read up channel state
	if (of_get_property(pdev->dev.of_node, "ext,enable-icap-channel", NULL)) 
	{
		use_icap_channel = 1;
	}
	else
	{
		use_icap_channel = 0;
	}


	// timer is valid, so lets get its resources
	irq = platform_get_resource(pdev,IORESOURCE_IRQ,0);
	if(unlikely( NULL  ==  irq))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get interrupt.\n",__func__,__LINE__);
		return -ENODEV;
	}
	mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(unlikely(NULL == mem))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get timer memory.\n",__func__,__LINE__);
		return -ENODEV;
	}

	// alloc private descriptor struct
	timer = devm_kzalloc(&pdev->dev,sizeof(struct DMTimer_priv),GFP_KERNEL);
	if(!timer)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__LINE__);
		return -ENOMEM;
	}
	timer->pdev = pdev;
	timer->idx = timer_idx;
	timer->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "DMTimer%d",timer_idx);

	// remapping registers
	timer->io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(timer->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the timer memory range.\n",__func__,__LINE__);
 		return PTR_ERR(timer->io_base);
 	}
 	timer->regspace_size = resource_size(mem);
 	timer->regspace_phys_base = mem->start;

 	// get timer clock
 	timer->fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(timer->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the timer.\n",__FUNCTION__,__LINE__);
 		return -ENODEV;
 	}

 	// setup misc character device
 	timer->misc.fops = &timer_cdev_fops;
	timer->misc.minor = MISC_DYNAMIC_MINOR;
	timer->misc.name =timer->name;
	timer->misc.mode = S_IRUGO | S_IWUGO;
	if(misc_register(&timer->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",timer->misc.name);
		return -ENODEV;
	}


	if(1 == use_icap_channel)
	{

		timer->icap_channel_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_icap","dmtimer",timer_idx);
		timer->icap = icap_create_channel(timer->icap_channel_namel,13);
		if(!timer->icap)
		{
			dev_err(&pdev->dev,"Cannot initialize icap channel: /dev/%s.\n",timer->misc.name);
			misc_deregister(&timer->misc);
			return -ENOMEM;
		}

		 // remapping irq
	 	timer->irq = irq->start;
	 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq",
						  dev_name(&pdev->dev),timer_idx);
	 	ret = devm_request_irq(&pdev->dev,timer->irq,timer_irq_handler,0,irq_name,timer);
	 	if(unlikely(ret))
	 	{
	 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__LINE__,timer->irq,ret);
	 		icap_delete_channel(timer->icap);
	 		misc_deregister(&timer->misc);
			return -ENXIO;
	 	}
	 }
	 else
	 {
	 	// icap channel and interrupt handling is not selected for this device

	 	timer->icap_channel_name = NULL;
	 	timer->icap = NULL;
	 	timer->irq = -1;
	 }

	timer->ovf_counter = 0;
	init_waitqueue_head(&timer->wq);

	dev_info(&pdev->dev,"%s initialization succeeded.\n",timer->name);
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d, icap channel %s.\n",timer->regspace_phys_base,timer->regspace_size,timer->irq, (timer->icap) ? "enabled" : "disabled");

 	platform_set_drvdata(pdev,timer);


 	// start the timer interface clock
 	pm_runtime_enable(&pdev->dev);
 	if(pm_runtime_get_sync(&pdev->dev) < 0)
 	{
 		dev_err(&pdev->dev,"[%s:%d] Cannot resume the timer.\n",__FUNCTION__,__LINE__);
 	}

 	return 0;

}

static int timer_remove(struct platform_device *pdev)
{
	struct DMTimer_priv *timer;
	timer = platform_get_drvdata(pdev);

	// stop the timer interface clock
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if(timer->irq > 0)
	{
		devm_free_irq(&pdev->dev, timer->irq, timer);
	}
	if(timer->icap)
	{
		icap_delete_channel(timer->icap);
	}

	misc_deregister(&timer->misc);

	// all other resourcees are freed managed
	return 0;
}				





static struct of_device_id timer_of_match[] = {
	{ .compatible = "ti,am335x-timer", },
	{ }
};



static struct platform_driver timer_platform_driver = {
	.probe = timer_probe,
	.remove = timer_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RTIO_DMTimer",
		.of_match_table = timer_of_match,
	},
};


module_platform_driver(timer_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone timers.");