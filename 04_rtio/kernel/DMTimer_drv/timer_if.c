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


#ifdef USE_XENOMAI
#include <rtdm/driver.h>
#include <rtdm/udd.h>
#endif

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

	char * icap_channel_name;
	struct icap_channel *icap;

	#ifdef USE_XENOMAI
	struct udd_device udd;
	int udd_registered;
	#endif
};


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


#define TIMER_IOCTL_MAGIC	'-'

#define IOCTL_SET_CLOCK_STATE		_IO(TIMER_IOCTL_MAGIC,1)
#define IOCTL_SET_CLOCK_SOURCE 		_IO(TIMER_IOCTL_MAGIC,2)
#define IOCTL_SET_ICAP_SOURCE 		_IO(TIMER_IOCTL_MAGIC,3)
#define IOCTL_GET_CLK_FREQ			_IO(TIMER_IOCTL_MAGIC,4)

#define TIMER_IOCTL_MAX				4


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



/********************* RTDM driver user interface ************************/

#ifdef USE_XENOMAI
static int xeno_open(struct rtdm_fd *fd, int oflags)
{
	return 0;
}

static void xeno_close(struct rtdm_fd *fd)
{

}


int xeno_irq_handler(struct udd_device *arg)
{
	struct DMTimer_priv *timer = container_of(arg,struct DMTimer_priv, udd);

	u32 irq_status;
	// clear pending irq request
	irq_status = readl(timer->io_base + TIMER_IRQSTATUS_OFFSET);
	writel(irq_status,timer->io_base + TIMER_IRQSTATUS_OFFSET);

	return RTDM_IRQ_HANDLED;
}
#endif


// normal linux irq handler
static irqreturn_t icap_irq_handler(int irq, void *data)
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

	return IRQ_HANDLED;
}


// basic mmap-able character device to use from normal linux user space
int init_basic_interface(struct DMTimer_priv *timer)
{
	struct platform_device *pdev = timer->pdev;

	timer->misc.fops = &timer_cdev_fops;
	timer->misc.minor = MISC_DYNAMIC_MINOR;
	timer->misc.name =timer->name;
	timer->misc.mode = S_IRUGO | S_IWUGO;
	if(misc_register(&timer->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",timer->misc.name);
		return -ENODEV;
	}
	return 0;
}

void destroy_basic_interface(struct DMTimer_priv *timer)
{
	misc_deregister(&timer->misc);
}

// init icap channel to the timer
int init_icap_interface(struct DMTimer_priv *timer)
{
	struct platform_device *pdev = timer->pdev;
	char *irq_name;
	int ret;

	timer->icap_channel_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_icap","dmtimer",timer->idx);
	timer->icap = icap_create_channel(timer->icap_channel_name,13);
	if(!timer->icap)
	{
		dev_err(&pdev->dev,"Cannot initialize icap channel: /dev/%s.\n",timer->misc.name);
		return -ENOMEM;
	}

	// remapping irq
 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq", dev_name(&pdev->dev),timer->idx);
 	ret = devm_request_irq(&pdev->dev,timer->irq,icap_irq_handler,0,irq_name,timer);
 	if(unlikely(ret))
 	{
 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__LINE__,timer->irq,ret);
 		icap_delete_channel(timer->icap);
 		timer->icap = NULL;
		return -ENXIO;
 	}
 	return 0;
}

static void destroy_icap_interface(struct DMTimer_priv *timer)
{
	struct platform_device *pdev = timer->pdev;

	if(!timer->icap) return;

	devm_free_irq(&pdev->dev, timer->irq, timer);
	icap_delete_channel(timer->icap);

	timer->icap = NULL;
}


#ifdef USE_XENOMAI
// init additional rtdm udd interface
int init_xeno_interface(struct DMTimer_priv *timer)
{

	struct udd_device *udd = &timer->udd;
	int ret;

	// add interface caééback functions
	udd->device_flags 	= RTDM_NAMED_DEVICE;
	udd->device_name 	= timer->name;
	udd->ops.close 		= xeno_close;
	udd->ops.open 		= xeno_open;
	udd->ops.interrupt 	= xeno_irq_handler; 

	// add memory regions
	udd->mem_regions[0].name = "timer_regs";
	udd->mem_regions[0].addr = timer->regspace_phys_base;
	udd->mem_regions[0].len  = timer->regspace_size;
	udd->mem_regions[0].type = UDD_MEM_PHYS;
	// let the udd core handle the interrupts
	udd->irq = timer->irq;

	ret = udd_register_device(udd);
	if(ret)
	{
		dev_err(&timer->pdev->dev, "%s:%d Cannot initialize UDD interface for dmtimer %d.\n", __func__,__LINE__,timer->idx);
	}
	else
	{
		timer->udd_registered = 1;
	}

	return ret;

}

static void destroy_xeno_interface(struct DMTimer_priv *timer)
{
	if(timer->udd_registered == 1)
	{
		udd_unregister_device(&timer->udd);
		timer->udd_registered = 0;
	}
}

#endif



static int parse_dt(struct platform_device *pdev, struct DMTimer_priv *timer)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct resource *mem, *irq;
	struct clk *fclk;
	void __iomem *io_base;	
	int timer_idx;
	const char *timer_name;

	of_property_read_string_index(of_node, "ti,hwmods", 0, &timer_name);
	sscanf(timer_name, "timer%d", &timer_idx);
	if(timer_idx < 4 || timer_idx > 7) 
	{
		dev_err(&pdev->dev,"%s:%d: Invalid timer index found in the device tree: %d\n",__func__, __LINE__, timer_idx);
		return -EINVAL;
	}


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

 	// get timer clock
 	fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(timer->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the timer.\n",__FUNCTION__,__LINE__);
 		return -ENODEV;
 	}

 	//remapping registers
	io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(timer->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the timer memory range.\n",__func__,__LINE__);
 		return -ENODEV;
 	}

	// fill private descriptor
	timer->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "DMTimer%d",timer_idx);
	timer->idx = timer_idx;
	timer->irq = irq->start;
	timer->fclk = fclk;

	timer->io_base = io_base;
	timer->regspace_phys_base = mem->start;
	timer->regspace_size = resource_size(mem);
	timer->pdev = pdev;
	return 0;
}




static int timer_probe(struct platform_device *pdev)
{
	struct DMTimer_priv *timer;

	uint8_t icap_mode;
	uint8_t xeno_pwm_mode;
	int ret;


	icap_mode = 0;
	xeno_pwm_mode = 0;
	// read up dmtimer mode
	if (of_get_property(pdev->dev.of_node, "ext,icap-mode", NULL)) 
		icap_mode = 1;

	if (of_get_property(pdev->dev.of_node, "ext,xenomai-pwm", NULL)) 
		xeno_pwm_mode = 1;

	if((1 == xeno_pwm_mode) && (1 == icap_mode))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot set both icap and xeno-pwm mode on dmtimer.\n",__func__,__LINE__);
		return -EINVAL;
	}

	// alloc private descriptor struct
	timer = (struct DMTimer_priv*)devm_kzalloc(&pdev->dev,sizeof(struct DMTimer_priv),GFP_KERNEL);
	if(!timer)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__LINE__);
		return -ENOMEM;
	}
	
	ret = parse_dt(pdev,timer);
	if(ret)
		return -ENODEV;
	

	ret = init_basic_interface(timer);
	if(ret)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot init basic DMtimer interface for dmtimer %d.\n",__func__,__LINE__,timer->idx);
		return -ENODEV;
	}

	ret = 0;
	if(1 == icap_mode)
	{
		ret = init_icap_interface(timer);
	}
	else if(1 == xeno_pwm_mode)
	{
		#ifdef USE_XENOMAI
		ret = init_xeno_interface(timer);
		#else
		pr_warn("DMTimer set to XENOMAI PWM mode, but the driver does not support it. Rebuild it with the 'USE_XENOMAI' define.\n");
		#endif
	}

	if(ret)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot init extra interface for dmtimer %d.\n",__func__,__LINE__,timer->idx);
	}



	dev_info(&pdev->dev,"%s initialization succeeded.\n",timer->name);
	#ifdef USE_XENOMAI
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d, icap channel %s, pwm channel %s.\n",timer->regspace_phys_base,timer->regspace_size,timer->irq, (timer->icap) ? "enabled" : "disabled", (timer->udd_registered) ? "enabled" : "disabled");
	#else
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d, icap channel %s.\n",timer->regspace_phys_base,timer->regspace_size,timer->irq, (timer->icap) ? "enabled" : "disabled");
	#endif

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

	destroy_icap_interface(timer);

#ifdef USE_XENOMAI
	destroy_xeno_interface(timer);
#endif

	destroy_basic_interface(timer);

	// all other resourcees are freed managed
	return 0;
}				



static struct of_device_id timer_of_match[] = {
	{ .compatible = "my,am335x-timer", },
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